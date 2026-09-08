# State Machine Architecture

## Overview

The next/0.3 AGT firmware has three mission states:

1. `PRE_DIVE`
2. `DIVING`
3. `RECOVERY`

The ArduSub Lua mission remains in charge of the dive. The AGT independently
guards release and Pi power, but mission state, release state, and power state
are intentionally separate.

## State transitions

```text
PRE_DIVE ── Lua STATE=1..3 ──► DIVING
    ▲                              │
    │ reset                        ├── Lua STATE=4 ──► RECOVERY
    │                              ├── ascent + sustained shallow depth ──► RECOVERY
    └──────────────────────────────┴── guarded failsafe ──► RECOVERY
```

- Boot and `reset` enter `PRE_DIVE`.
- A Lua `STATE` value from 1 through 3 moves `PRE_DIVE` to `DIVING`.
- Lua `STATE=4` moves the mission state to `RECOVERY`, but cannot directly cut
  Pi power.
- While Lua reports ascent (`STATE>=3`), fresh shallow autopilot depth sustained
  for `SURFACE_QUALIFY_MS` provides a backup transition to `RECOVERY`. There is
  no GPS term: this backstop exists for a wedged script, and acquisition after
  surfacing has taken as long as 38 minutes, so a fix requirement disabled it in
  exactly the conditions it was written for.
- Leak, sustained critical voltage, and heartbeat-loss monitors are evaluated
  only while `DIVING`, after the dive-entry grace. They can enter `RECOVERY`
  for communications but cannot actuate ballast release.

All trusted autopilot mission, depth, voltage, heartbeat, and release MAVLink
inputs must come from system/component `1/1`.

## State behavior

### PRE_DIVE

- Navigator/Pi, camera, and lights remain powered through the NC power relay.
- GPS, MAVLink, Meshtastic, optional Iridium test, and readiness LEDs operate.
- Release remains independent; a persisted active release is not cleared by
  boot or mission reset.

### DIVING

- All nonessential loads remain powered.
- Lua controls the mission and may command LEDs and `RELAY`.
- AGT monitors fresh autopilot voltage, leak state, and heartbeat for guarded
  release fallbacks.
- Iridium recovery reporting is disabled.

### RECOVERY

- Strobe and recovery communications are enabled.
- Navigator/Pi, camera, and lights remain powered for Lua's three-minute
  surface-logging dwell.
- Only after that dwell, BlueOS acknowledgement, and final grace does the power
  relay open and turn all nonessential loads off.
- A missing ACK keeps all loads powered. Once a valid post-dive `STATE=4`
  arrives, transport loss or a later state change cannot cancel the handshake.

## Safe surface power handshake

A single fresh `STATE=4` authorizes the dwell only after the AGT has observed
the RAM-only `PRE_DIVE -> DIVING` mission sequence. That report is the sole
surface/shutdown authority and latches
`SURFACE_LOGGING_DWELL_MS` (180 seconds), during which the payload remains
powered and Lua continues sending telemetry into the active MCAP.

Depth and GPS are deliberately absent from the cutoff vote. The independent
shallow-depth/liveness backstop still enters `RECOVERY` so the strobe can
operate if Lua is wedged, but it cannot start the power dwell. Automatic
Iridium reporting starts in `RECOVERY` regardless of cutoff, so a backstop-only
or deck abort still locates the vehicle. Payload shutdown remains a separate
ack-gated decision.

After the dwell:

1. AGT repeatedly publishes `PWR_SHDN=1`.
2. BlueOS component `1/191` finishes shutdown preparation and publishes
   `PWR_ACK=1`.
3. AGT waits `POWER_SHUTDOWN_FINAL_GRACE_MS` (30 seconds) so BlueOS
   `systemctl poweroff` can complete.
4. AGT opens the NC power relay. The ACK latches this countdown, so the expected
   loss of MAVLink during Linux shutdown cannot cancel it.

Premature ACKs are rejected. Once the valid `STATE=4` latches authorization,
only a reset clears it. Unsigned elapsed-time subtraction keeps both timers
safe across rollover.

## Iridium reporting in RECOVERY

Automatic Iridium reporting starts as soon as the AGT is in `RECOVERY`,
whether or not the payload-power handshake has completed. Payload cutoff
remains ack-gated: no `PWR_ACK` means the Pi stays up. `ISBDCallback` parses
inbound MAVLink (`PWR_ACK`, `STATE`, voltage, leak bookkeeping), republishes
`PWR_SHDN`, and keeps the recovery/Iridium LED animation running during a
blocking SBD session.

The first report goes out immediately whether or not GPS has a fix. Successful
located and unlocated reports then repeat on the same configured
`iridiumInterval`. A failed session retries after `IRIDIUM_RETRY_BACKOFF_MS`
and is not treated as sent. A fix arriving after an unlocated report triggers
an immediate located upgrade.

Each reporting session makes at most two 90-second SBD attempts. The interval
is measured from the end of the session, so a long transaction does not cause
an immediate catch-up transmission.

Both report types use DORIS ASCII protocol B:

```text
B,+033.12345,-118.12345,07,245,2238,14.8,3.2,00
```

The fields are protocol version, signed latitude and longitude with five
decimals, ground speed in decimeters per second, course in degrees, maximum
mission depth in meters, battery voltage, minimum pressure-sensor temperature,
and a status byte. Speed, course, and depth are rounded to integers. The status
byte is reserved as `00` until its bit assignments are defined.

Without a fix, the navigation fields are
`+000.00000,+000.00000,00,000`; no stale pre-dive position is used. When a fix
finally arrives, the located report is sent immediately rather than waiting
out the interval.

The repeat interval is long deliberately. Iridium and GPS share one antenna via
`antennaToIridium()` / `antennaToGPS()`, and there is already history of SBD
sessions disturbing GPS warm start, so reporting every few minutes through a
38-minute wait would interrupt acquisition repeatedly and could make the fix
take longer still.

The report carries no position at all, not even a last-known-good one: a fix
from before the dive would read as though it were where the vehicle surfaced,
and a drifting vehicle can be a long way from it.

## Power cycle behavior

Cutting power is an active operation: the NC power relay conducts whenever the
coil is off, so an unpowered, reset, or crashed AGT leaves the Pi, camera, and
lights powered. Every power cycle therefore brings the payload back up.

Nothing about the cutoff decision is persisted. `MissionData_init()` clears the
Lua state, and `StateMachine_init()` returns to
`PRE_DIVE` with `surfaceQualified`, `shutdownRequested`, and
`shutdownAcknowledged` all false. Because the observed `DIVING -> RECOVERY`
sequence is RAM-only, the vehicle must run another dive before the AGT can cut
power a second time; replayed `STATE=4` after boot is insufficient.

That is deliberate for the on-deck case. After a recovery the autopilot has
already cleared `DORIS_START`, so a power cycle brings Lua up in `CONFIG`
reporting `STATE=-1`. The AGT sees no recovery signal, leaves the payload
powered indefinitely, and the operator can download data or configure a new
mission without the system shutting down underneath them.

The accepted cost is that an AGT reset during an unattended surface wait ends
the power saving for that deployment: the payload comes back up and stays up,
because the AGT can no longer prove a dive occurred.

## Release ownership

The Navigator is the sole ballast-release controller. Lua may continue
publishing `RELAY` for the Navigator, but AGT firmware does not consume that
message, drive GPIO35, persist a release latch, or advertise a release path.
AGT sensor monitors can enter `RECOVERY` for strobe and communications behavior;
they cannot actuate the physical release.

## MAVLink compatibility/status protocol

All names fit the 10-byte `NAMED_VALUE_FLOAT.name` field.

| Name | Direction/source | Meaning |
|------|------------------|---------|
| `AGT_CAP` | AGT `1/192` → BlueOS | Capability bitmask for compatibility gating |
| `PWR_SHDN` | AGT `1/192` → BlueOS | Qualified graceful-shutdown request |
| `PWR_ACK` | BlueOS `1/191` → AGT | Shutdown preparation complete |

`AGT_CAP=2` sets only bit 1 (`AGT_CAP_SAFE_SURFACE_POWER`) for the
`PWR_SHDN`/`PWR_ACK` handshake. Release-owner bit 0 is intentionally clear.

## Persistence and reset

- Mission state and surface qualification are not persisted.
- Pi power always initializes ON.
- No release state is stored by the AGT.
- `NO_RELAYS` builds track payload-power state without driving GPIO4.

## Configuration

Safety thresholds and protocol constants are defined in `include/config.h`.
Operational intervals and feature enables remain in `SystemConfig`.
