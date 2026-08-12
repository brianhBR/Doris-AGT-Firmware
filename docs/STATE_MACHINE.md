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
- Leak, sustained critical voltage, and heartbeat-loss release failsafes are
  evaluated only while `DIVING`, after the dive-entry grace.
- `release_now` and valid Iridium release commands are explicit operator paths
  and may latch release independently of mission state.

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
- A missing ACK keeps all loads powered. Stale/reverted Lua state cancels before
  ACK, but transport loss after ACK cannot cancel the latched final countdown.

## Safe surface power handshake

A single `STATE=4` does not authorize cutoff. The AGT must have observed the
RAM-only `PRE_DIVE -> DIVING` mission sequence, then receive at least
`SURFACE_RECOVERY_MESSAGES` (3) consecutive fresh Lua `STATE=4` reports. Those
reports are the sole surface/shutdown authority. They start
`SURFACE_LOGGING_DWELL_MS` (180 seconds), during which the payload remains
powered and Lua continues sending telemetry into the active MCAP.

Depth and GPS are deliberately absent from the cutoff vote. The independent
shallow-depth/liveness backstop still enters `RECOVERY` so the strobe can
operate if Lua is wedged, but it cannot start the power dwell. Automatic
Iridium reporting now waits for payload cutoff, so a backstop-only recovery
does not start a blocking modem session. This keeps surface detection and
payload shutdown as separate decisions.

After the dwell:

1. AGT repeatedly publishes `PWR_SHDN=1` while fresh `STATE=4` continues.
2. BlueOS component `1/191` finishes shutdown preparation and publishes
   `PWR_ACK=1`.
3. AGT waits `POWER_SHUTDOWN_FINAL_GRACE_MS` (30 seconds) so BlueOS
   `systemctl poweroff` can complete.
4. AGT opens the NC power relay. The ACK latches this countdown, so the expected
   loss of MAVLink during Linux shutdown cannot cancel it.

Premature ACKs are rejected. Stale or reverted Lua state before ACK resets the
dwell and cancels the request. Only a power cycle clears an accepted ACK.
Unsigned elapsed-time subtraction keeps both timers safe across rollover.

## Iridium reporting in RECOVERY

Automatic Iridium reporting starts only after the BlueOS handshake, final
30-second grace, and physical payload-relay cutoff are complete. A synchronous
Iridium session can block firmware execution for many minutes in poor
conditions; running it first previously delayed the three-minute dwell and
clean shutdown. Manual operator tests remain available before cutoff.

The schedule continues aging while shutdown completes. After cutoff, a located
report that is already due goes out immediately, then repeats every
`iridiumInterval`. Without a fix, the first unlocated report becomes due
`IRIDIUM_NOFIX_FIRST_MS` (2 minutes) after entering `RECOVERY`, and repeats
every `IRIDIUM_NOFIX_REPEAT_MS` (30 minutes).

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

## Release controller

The AGT drives GPIO35, mirroring the Navigator relay that Lua drives for the
same request. Only one of the two outputs may be wired to the actuator:

- finite `RELAY=1` from autopilot `1/1` latches release ON;
- repeated ON commands are harmless;
- manual, Iridium, and guarded `DIVING` failsafes use the same controller;
- the active marker is stored in a bounded EEPROM record and reapplied after
  reboot;
- release does not automatically stop after 1500 seconds;
- explicit Lua `RELAY=0` is accepted only after `RELEASE_MIN_HOLD_SEC` and
  confirmed Lua recovery;
- release state never causes Pi power cutoff.

During MCU reset and early boot GPIO35 is inactive until the persisted marker is
validated and reapplied. Corrupt, unknown, or out-of-bounds EEPROM records fail
safe to release OFF.

## MAVLink compatibility/status protocol

All names fit the 10-byte `NAMED_VALUE_FLOAT.name` field.

| Name | Direction/source | Meaning |
|------|------------------|---------|
| `AGT_CAP` | AGT `1/192` → BlueOS | Capability bitmask for compatibility gating |
| `REL_STAT` | AGT `1/192` → BlueOS | Actual latched GPIO35 state |
| `PWR_SHDN` | AGT `1/192` → BlueOS | Qualified graceful-shutdown request |
| `PWR_ACK` | BlueOS `1/191` → AGT | Shutdown preparation complete |
| `RELAY` | autopilot `1/1` → AGT | Guarded release request |

`AGT_CAP` currently defines:

- bit 0 (`AGT_CAP_RELEASE_OWNER`): AGT owns GPIO35 release control;
- bit 1 (`AGT_CAP_SAFE_SURFACE_POWER`): AGT implements the safe surface
  `PWR_SHDN`/`PWR_ACK` handshake.

BlueOS verifies bit 1 before acknowledging shutdown. Bit 0 is evaluated
separately when checking whether the AGT is an available release path; shutdown
does not require AGT release ownership.

## Persistence and reset

- Mission state and surface qualification are not persisted.
- Pi power always initializes ON.
- Active release is persisted separately and is not cleared by mission reset.
- `NO_RELAYS` builds track the same logical state without driving relay pins and
  use a distinct EEPROM magic value so bench simulation cannot arm a production
  image.

## Configuration

Safety thresholds and protocol constants are defined in `include/config.h`.
Operational intervals and feature enables remain in `SystemConfig`.
