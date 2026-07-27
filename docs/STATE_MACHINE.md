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
    │                              ├── ascent + shallow depth + AGT GPS ──► RECOVERY
    └──────────────────────────────┴── guarded failsafe ──► RECOVERY
```

- Boot and `reset` enter `PRE_DIVE`.
- A Lua `STATE` value from 1 through 3 moves `PRE_DIVE` to `DIVING`.
- Lua `STATE=4` moves the mission state to `RECOVERY`, but cannot directly cut
  Pi power.
- While Lua reports ascent (`STATE>=3`), fresh shallow autopilot depth plus AGT
  GPS provides a backup transition to `RECOVERY`.
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
- Navigator/Pi, camera, and lights all remain powered while surface
  qualification and graceful shutdown are pending.
- Only after qualification, BlueOS acknowledgement, and final grace does the
  power relay open and turn all nonessential loads off.
- A missing ACK or stale/invalid qualification input keeps all loads powered.

## Safe surface power handshake

A single `STATE=4` does not authorize cutoff. All of these must remain true for
`SURFACE_QUALIFY_MS`:

- at least `SURFACE_RECOVERY_MESSAGES` consecutive fresh Lua `STATE=4` reports;
- fresh finite autopilot depth no deeper than
  `RECOVERY_DEPTH_THRESHOLD_M`;
- evidence that this boot observed depth at least `DIVE_DEPTH_THRESHOLD_M`;
- a fresh, valid fix from the AGT's own GNSS receiver.

After qualification:

1. AGT repeatedly publishes `PWR_SHDN=1`.
2. BlueOS component `1/191` finishes shutdown preparation and publishes
   `PWR_ACK=1`.
3. AGT waits `POWER_SHUTDOWN_FINAL_GRACE_MS` (30 seconds) so BlueOS
   `systemctl poweroff` can complete.
4. AGT opens the NC power relay only if every qualification input stayed valid.

Premature ACKs are rejected. Any transient or stale qualification before cutoff
cancels the request and ACK. BlueOS must acknowledge a later request again.
Unsigned elapsed-time subtraction keeps all bounded timers safe across
`millis()` rollover.

## Power cycle behavior

Cutting power is an active operation: the NC power relay conducts whenever the
coil is off, so an unpowered, reset, or crashed AGT leaves the Pi, camera, and
lights powered. Every power cycle therefore brings the payload back up.

Nothing about the cutoff decision is persisted. `MissionData_init()` clears the
depth high-water mark and the Lua state, and `StateMachine_init()` returns to
`PRE_DIVE` with `surfaceQualified`, `shutdownRequested`, and
`shutdownAcknowledged` all false. Because the proof that a dive happened is that
RAM-only high-water mark, the vehicle must dive and reach recovery again before
the AGT can cut power a second time.

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
  independent surface qualification;
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

BlueOS must verify both required bits before enabling v0.3 release or power
integration.

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
