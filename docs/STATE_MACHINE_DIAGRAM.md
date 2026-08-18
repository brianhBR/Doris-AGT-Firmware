# State Machine Diagrams

Visual reference for the AGT state machine exactly as implemented on
`next/0.3.x`. Every transition below is traceable to a specific line of code;
citations are given as `file:line`. Where the code and the prose documentation
disagree, the code wins and the discrepancy is recorded under
[Findings](#findings).

Two independent state machines are involved and must not be confused:

- the **AGT state machine** (`SystemState`, three states) that this document
  describes;
- the **Lua mission state** on ArduSub, which the AGT only *observes* as a
  numeric `NAMED_VALUE_FLOAT` named `STATE`.

## Lua mission state numbering

The AGT never sees Lua's state names, only the number. The mapping is fixed by
`doris.lua:90-95` in the `blueos-doris-0.6` repository and is published every
`UPDATE_INTERVAL_MS` (500 ms, `doris.lua:70`) by `update_telemetry`
(`doris.lua:718`):

| Value | Lua constant | Notes |
|-------|--------------|-------|
| -1 | `STATE_CONFIG` | Lua's initial value on script start |
| 0 | `STATE_MISSION_START` | |
| 1 | `STATE_DESCENT` | |
| 2 | `STATE_ON_BOTTOM` | |
| 3 | `STATE_ASCENT` | |
| 4 | `STATE_RECOVERY` | terminal within one Lua script run, `doris.lua:1588` |

The AGT accepts the value only from system/component `1/1`, only if finite,
only if within 0.1 of an integer, and only if in the inclusive range -1 to 4
(`src/modules/mavlink_interface.cpp:470-475`).

## 1. Main state machine

```mermaid
stateDiagram-v2
    direction LR

    [*] --> PRE_DIVE

    PRE_DIVE --> DIVING : follow Lua, STATE is 1, 2 or 3
    PRE_DIVE --> RECOVERY : follow Lua, STATE is 4
    DIVING --> RECOVERY : follow Lua, STATE is 4
    DIVING --> RECOVERY : AGT backup detect, STATE at least 3 plus shallow depth sustained and moving
    DIVING --> RECOVERY : any DIVING failsafe fires
    RECOVERY --> PRE_DIVE : follow Lua, STATE at most 0
    RECOVERY --> PRE_DIVE : serial command reset

    note left of PRE_DIVE
        Entered by StateMachine_init at boot, state_machine.cpp:25
        enterState PRE_DIVE, state_machine.cpp:240-246
          nonessentialsPowered = true
          RelayController_setPowerManagement true, power relay conducts
          surfaceQualified, shutdownRequested, shutdownAcknowledged all false
        Release latch is NOT cleared here
    end note

    note right of DIVING
        Guard, state_machine.cpp:164-167
          enterDiving is ignored unless current state is PRE_DIVE
        enterState DIVING, state_machine.cpp:247-250
          nonessentialsPowered = true, power relay conducts
        main.cpp:390-391 explicitly blocks the STATE at most 0 reset
        while DIVING, so a parameter glitch cannot abort a dive
    end note

    note right of RECOVERY
        enterState RECOVERY, state_machine.cpp:251-256
          nonessentialsPowered = true, power relay STILL conducts
        Entering RECOVERY never cuts Pi power by itself
        main.cpp:404 and main.cpp:415 zero lastIridiumSend so the first
        Iridium report goes out immediately
    end note
```

Transition sources, line by line:

| From | To | Trigger | Code |
|------|----|---------|------|
| boot | `PRE_DIVE` | unconditional | `state_machine.cpp:25` via `main.cpp:142` |
| `PRE_DIVE` | `DIVING` | `dorisState` in 1..3 | `main.cpp:396-399` |
| `PRE_DIVE` | `RECOVERY` | `dorisState` at least 4 | `main.cpp:402-405` |
| `DIVING` | `RECOVERY` | `dorisState` at least 4 | `main.cpp:402-405` |
| `DIVING` | `RECOVERY` | `dorisState` at least 3 AND fresh `depth_m` below `RECOVERY_DEPTH_THRESHOLD_M` (1.5 m), sustained `SURFACE_QUALIFY_MS` and spanning at least `SURFACE_DEPTH_LIVENESS_M` | `StateMachine_updateSurfaceBackstop` |
| `DIVING` | `RECOVERY` | any failsafe reaches `enterState` | `state_machine.cpp:191-193` |
| `RECOVERY` | `PRE_DIVE` | `dorisState` at most 0 | `main.cpp:390-393` |
| `RECOVERY` | `PRE_DIVE` | serial `reset` | `main.cpp:528-531` |

Which mechanism owns which arrow:

- **Following Lua**: the `dorisState` comparisons in `checkStateTransitions()`
  (`main.cpp:383-418`). These use the raw last-received value from
  `MissionData_getDorisState()` with **no freshness check**.
- **AGT independent detection**: only the `DIVING` to `RECOVERY` backup at
  `main.cpp:408-417`, which requires sustained shallow, moving autopilot depth.
  Because `dorisState` at least 4 is already handled one block above, this
  backup is only *additive* for `dorisState == 3` (ascent), and it cannot
  authorize payload power cutoff.
- **Failsafes**: `StateMachine_triggerFailsafe()` (`state_machine.cpp:181-194`),
  detailed in diagram 2.

Transitions that deliberately do **not** exist:

- `RECOVERY` to `DIVING` — `enterDiving()` refuses any source state other than
  `PRE_DIVE` (`state_machine.cpp:164-167`), and `main.cpp:397-398` only calls it
  from `PRE_DIVE`.
- `DIVING` to `PRE_DIVE` — explicitly excluded at `main.cpp:390-391`.
- `PRE_DIVE` to `RECOVERY` by failsafe — `triggerFailsafe` only calls
  `enterState` when already `DIVING` (`state_machine.cpp:191`).

## 2. Failsafe paths

All automatic failsafes are evaluated in `StateMachine_update()`
(`state_machine.cpp:44-82`), which is called once per loop from `main.cpp:197`.

```mermaid
flowchart TD
    A[StateMachine_update, state_machine.cpp:44] --> B{currentState is DIVING<br>AND timeInState at least<br>DIVE_HEARTBEAT_GRACE_MS 90 s}
    B -- no --> C[clear criticalVoltageTimingActive<br>return, no failsafe possible<br>line 49-53]
    B -- yes --> D{md.leak_detected}
    D -- yes --> E[triggerFailsafe FAILSAFE_LEAK<br>line 57-59]
    D -- no --> F{autopilot voltage fresh within 3 s<br>AND voltage above 0<br>AND voltage at most 11.0 V}
    F -- yes --> G{sustained at least 10000 ms<br>hardcoded, line 70}
    G -- yes --> H[triggerFailsafe FAILSAFE_LOW_VOLTAGE<br>line 70-73]
    G -- no --> I[keep timing]
    F -- no --> J[reset the 10 s timer<br>line 74-76]
    I --> K
    J --> K{hasHadHeartbeat<br>AND age of last autopilot heartbeat<br>at least FAILSAFE_HEARTBEAT_TIMEOUT_MS 120 s}
    K -- yes --> L[triggerFailsafe FAILSAFE_NO_HEARTBEAT<br>line 78-81]
    K -- no --> M[no action this loop]
```

Per-source summary:

| `FailsafeSource` | Fires in | Qualifying condition | Timer | Effect on state |
|------------------|----------|----------------------|-------|-----------------|
| `FAILSAFE_NONE` | n/a | sentinel only, never triggered | n/a | none |
| `FAILSAFE_LEAK` | `DIVING` only | `md.leak_detected` true | 90 s dive-entry grace, then immediate | to `RECOVERY` |
| `FAILSAFE_LOW_VOLTAGE` | `DIVING` only | fresh autopilot voltage, greater than 0, at most `BATTERY_CRITICAL_VOLTAGE` 11.0 V | 90 s grace, then sustained 10 s | to `RECOVERY` |
| `FAILSAFE_NO_HEARTBEAT` | `DIVING` only | at least one heartbeat ever received, then age at least 120 s | 90 s grace | to `RECOVERY` |

`md.leak_detected` is only ever set by the serial test command `set_leak`
(`main.cpp:552-557`). No MAVLink message writes it.

Legacy Iridium MT release commands are acknowledged as unsupported and ignored;
the Navigator is the only release authority.

## 3. Surface power-cutoff sub-state machine

`StateMachine_updateSurfacePower()` is called every loop from `main.cpp`. Lua's
terminal state is the sole shutdown authority; neither depth nor GPS votes in
this sub-state machine.

```mermaid
stateDiagram-v2
    direction TB

    [*] --> WAITING_FOR_LUA

    WAITING_FOR_LUA --> LOGGING_DWELL : observed dive plus one fresh STATE 4 report
    LOGGING_DWELL --> AWAITING_ACK : SURFACE_LOGGING_DWELL_MS elapsed, 180 s
    AWAITING_ACK --> FINAL_GRACE : PWR_ACK equal to 1 from BlueOS 1 slash 191
    FINAL_GRACE --> POWER_CUT : POWER_SHUTDOWN_FINAL_GRACE_MS elapsed, 30 s
    POWER_CUT --> [*]

    note right of WAITING_FOR_LUA
        surfaceQualified = false
        shutdownRequested = false
        The power relay is NOT touched here
    end note

    note right of LOGGING_DWELL
        payload stays powered
        Lua STATE 4 and telemetry continue
        BlueOS MCAP remains active
    end note

    note right of AWAITING_ACK
        shutdownRequested = true, state_machine.cpp:116
        PWR_SHDN republished at 1 Hz by
        MAVLinkInterface_sendSafetyStatus, mavlink_interface.cpp:315-316
    end note

    note right of POWER_CUT
        RelayController_setPowerManagement false
        nonessentialsPowered = false
        Power relay coil energizes, NC contact opens, loads off
        Automatic Iridium reporting is now allowed
    end note
```

The transport and mission-sequence checks are:

1. `status.currentState == STATE_RECOVERY`;
2. `status.previousState == STATE_DIVING` — this boot observed a real mission,
   so replayed recovery packets after reset cannot cut power;
3. `MissionData_isDorisStateFresh()` — Lua `STATE` received within
   `MISSION_DATA_FRESHNESS_MS` (3000 ms);
4. `md.doris_state == 4` — exactly Lua `STATE_RECOVERY`.

The first report meeting those conditions latches `surfaceQualified` for the
rest of the boot. MAVLink loss or a later non-4 report cannot reset the
three-minute dwell or shutdown request. Once ACK is accepted,
`shutdownAcknowledged` also latches and the function advances the 30-second
final grace.

MAVLink messages involved, all `NAMED_VALUE_FLOAT`:

| Name | Direction | Source sysid/compid | Meaning | Code |
|------|-----------|---------------------|---------|------|
| `STATE` | inbound | autopilot `1/1` | Lua mission state | `mavlink_interface.cpp:470-475` |
| `PWR_SHDN` | outbound | AGT `1/192` | 1 while `shutdownRequested`, republished every `POWER_STATUS_INTERVAL_MS` (1 s) | `mavlink_interface.cpp:315-316` |
| `PWR_ACK` | inbound | BlueOS `1/191` only | value in 0.9..1.1 calls `StateMachine_acknowledgeShutdown()` | `mavlink_interface.cpp:491-499` |
| `AGT_CAP` | outbound | AGT `1/192` | capability bitmask `0x2` (safe surface power only) | `mavlink_interface.cpp` |

`StateMachine_acknowledgeShutdown()` (`state_machine.cpp:124-134`) rejects an
ACK unless both `shutdownRequested` and `surfaceQualified` are already true, so
a premature or replayed `PWR_ACK` cannot pre-arm the sequence. A repeated ACK
after the first is idempotent and does not restart the grace timer.

Once `POWER_CUT` is reached the relay is never re-closed by this function.
Power is restored only by a reset/state entry or by a boot. It is also the
point where `StateMachine_canTransmitIridium()` becomes true. This ordering
prevents a blocking satellite session from delaying the logging dwell,
BlueOS ACK, or electrical cutoff.

## 4. Release ownership

The Navigator is the only ballast-release controller. AGT firmware ignores
Lua's `RELAY` named value, never configures GPIO35, stores no release latch in
EEPROM, and leaves release-owner capability bit 0 clear. AGT failsafe monitoring
may enter `RECOVERY` for communications but cannot actuate release.

## 5. Boot and power-cycle behavior

```mermaid
flowchart TD
    A[Battery reattached or AGT held in reset] --> B[GPIO4 undriven]
    B --> C[Power relay coil de-energized<br>NC contact closed<br>Pi, camera and lights POWERED]
    C --> E[setup]
    E --> F[setupPins<br>D4 OUTPUT LOW<br>payload remains powered]
    F --> G[loadConfiguration, main.cpp:139]
    G --> H[MissionData_init, main.cpp:140<br>RAM reset: max_depth_m 0, depth_valid false,<br>doris_state -1,<br>heartbeat_valid false, leak_detected false]
    H --> I[RelayController_init, main.cpp:141]
    I --> J[drive payload-power relay to conduct<br>loads POWERED]
    J --> L[StateMachine_init, main.cpp:142]
    L --> M[currentState PRE_DIVE<br>nonessentialsPowered true<br>setPowerManagement true again<br>surfaceQualified, shutdownRequested,<br>shutdownAcknowledged all false<br>state_machine.cpp:24-42]
    M --> N[Normal loop]
```

What is persisted versus what is reset:

| Item | Storage | Survives power cycle |
|------|---------|----------------------|
| `SystemConfig` intervals and feature enables | EEPROM below 256 | Yes |
| `SystemState` | RAM | No, always `PRE_DIVE` |
| `max_depth_m` | RAM | No, reset to 0 at `mission_data.cpp:11` |
| `doris_state` | RAM | No, reset to -1 |
| `surfaceQualified`, `shutdownRequested`, `shutdownAcknowledged` | RAM | No, all false |
| `nonessentialsPowered` and the physical power relay | RAM plus GPIO | No, forced back ON three times during `setup` |

Relay wiring implications while the AGT is unpowered or held in reset
(`config.h:145-155`):

- `RELAY_POWER_MGMT_NC` is `true` and `RELAY_COIL_ACTIVE_HIGH` is `true`. An
  undriven or LOW GPIO4 leaves the coil off, the normally-closed contact
  closed, and the Pi, camera and lights **powered**. Cutting power is an active
  operation that requires a live AGT holding GPIO4 HIGH.

An AGT crash, brownout, or reset therefore restores payload power. Release is
outside the AGT firmware and remains the Navigator's responsibility.

## 6. Per-state side effects

| | `PRE_DIVE` | `DIVING` | `RECOVERY` |
|---|---|---|---|
| Iridium periodic report | Blocked | Blocked | Blocked while payload power is on; after cutoff, protocol B sends the already-due located or zero-navigation report, then follows `IridiumSchedule_*` |
| Iridium manual test | Allowed via `iridium_test` or MAVLink 31013, not state gated (`main.cpp:225`) | Allowed, not state gated | Allowed |
| NeoPixel mode | `LED_MODE_READY` if `MissionData_isArmed()`, else `LED_MODE_ERROR` (`main.cpp:375-380`) | `LED_MODE_LUA` if a Lua LED command is fresh, else `LED_MODE_DIVING` (`main.cpp:366-373`) | `LED_MODE_RECOVERY` strobe (`main.cpp:361-364`) |
| Power relay on state entry | Driven to conduct (`state_machine.cpp:242`) | Driven to conduct (`state_machine.cpp:249`) | Driven to conduct (`state_machine.cpp:255`) |
| Power relay can be opened | No | No | Only through diagram 3 |
| Release relay | Navigator only; AGT has no release output | Navigator only | Navigator only |
| MAVLink outbound | Heartbeat 1 Hz, `AGT_CAP`, `PWR_SHDN` 1 Hz, GPS 5 Hz | Same | Same |
| Failsafe evaluation | Skipped (`state_machine.cpp:49-53`) | Active after `DIVE_HEARTBEAT_GRACE_MS` | Skipped |

## Findings

Reported for awareness only; nothing here has been changed.

### Documentation that contradicts the code

1. **`CHANGELOG.md:29-31` and `CHANGELOG.md:60`** still describe the obsolete
   `PRE_MISSION` / `SELF_TEST` / `MISSION` / `RECOVERY` machine, including a
   `SELF_TEST` to `MISSION` transition on depth greater than 2 m. No such
   states exist in `SystemState`.
2. **`src/main.cpp:12-15`**, the file header comment, claims
   `PRE_DIVE -> DIVING: depth > threshold (vehicle went underwater)`. The
   actual transition at `main.cpp:396-399` is driven purely by the Lua `STATE`
   value; depth is never consulted for dive entry.
3. **`include/config.h:117`** documents `DIVE_DEPTH_THRESHOLD_M` as
   `Depth > this: PRE_DIVE -> DIVING`. It is used only once, at
   `state_machine.cpp:94`, as the `max_depth_m` proof-of-dive gate for surface
   power qualification.
4. **`include/config.h:120`** documents `DIVE_HEARTBEAT_GRACE_MS` as ignoring
   only the heartbeat timeout. The early return at `state_machine.cpp:49-53`
   suppresses the leak and critical-voltage failsafes as well, so no failsafe
   of any kind can fire during the first 90 s of a dive.
### Dead or unreachable code

6. **`DIVE_MIN_DURATION_MS`** (`config.h:119`) is defined but referenced
   nowhere in `src/`. There is no minimum dive duration before the
   `DIVING` to `RECOVERY` transition, contrary to the comment on that line and
   to `docs/STATE_MACHINE.md` implying an ordered sequence.
7. **`BATTERY_LOW_VOLTAGE`** (`config.h:111`) is unreferenced. Only
   `BATTERY_CRITICAL_VOLTAGE` is used.
8. **`MAVLinkInterface_update()`** (`mavlink_interface.cpp:575-587`) is never
   called; `main.cpp` uses `processSerialInput()` instead, which does its own
   `mavlink_parse_char` loop.
10. **`StateMachine_shouldShutdownNonessentials()`** and
    **`StateMachine_isRecoveryStrobe()`** are referenced only by
    `test/test_state_machine/`. Production LED selection reads
    `StateMachine_getState()` directly.
11. **`MissionData_isAutopilotFailsafe()`**, **`hasUnhealthySensors()`**,
    **`isMissionReady()`**, **`hasDorisState()`**, **`getPrearmStatus()`** and
    **`update_voltage()`** are unused outside tests. `SYS_STATUS` sensor health
    and the Lua `PREARM` value are therefore ingested and stored but never
    acted on.
12. **`IridiumManager_sendDorisReport()`** and **`IridiumManager_checkMT()`**
    are never called; the binary `doris_protocol` include is commented out at
    `main.cpp:36`. As a result `DORIS_CMD_SEND_REPORT`, `DORIS_CMD_RESET_STATE`,
    `DORIS_CMD_REBOOT`, `DORIS_CMD_ENABLE_IRIDIUM` and
    `DORIS_CMD_DISABLE_IRIDIUM` have no active handler. `DORIS_CMD_RELEASE` is
    explicitly ignored because release is Navigator-owned.
13. **`DorisMissionState`** in `doris_protocol.h:36-41` enumerates a four-value
    mission state including `DORIS_STATE_FAILSAFE` that does not correspond to
    the current `SystemState`. It is never populated.
14. **`LED_MODE_STANDBY`** is only ever the initial value of `currentMode` and
    the value set by `NeoPixelController_clear()`. `updateLEDState()` never
    selects it, so it is visible only for the first loop iteration.
15. **`SUPPRESS_DEBUG_TEXT` is defined** (`config.h:13`), so every
    `DebugPrint`/`DebugPrintln` compiles to a no-op. The entire serial command
    console — `help`, `status`, `gps`, `debug`, `reset`, and `set_leak` —
    produces no output in the default
    build. The commands still execute.

### Behavioral gaps worth reviewing

16. **Resolved: `RELAY` path flood.** AGT no longer handles Lua's Navigator
    release named value, so it emits no release `STATUSTEXT` traffic.
17. **`checkStateTransitions()` never checks freshness.** It reads
    `MissionData_getDorisState()` raw (`main.cpp:384`). If the autopilot link
    dies while the last received value was 4, the AGT stays in `RECOVERY`
    indefinitely; if it died at 1..3 during `PRE_DIVE`, the AGT would enter
    `DIVING` on a stale value. `updateSurfacePower` does check freshness, so
    the power cutoff itself is protected, but the mission state is not.
18. **The `RECOVERY` to `PRE_DIVE` reset is hard to reach in practice.** Lua's
    `STATE_RECOVERY` is terminal within a script run (`doris.lua:1588`), and the
    cancel-to-`CONFIG` path at `doris.lua:995` explicitly excludes state 4. A
    `dorisState` of at most 0 therefore only appears after a Lua script restart.
19. **`MissionData_getDorisState()` returns -1 before any `STATE` is received**
    (`mission_data.cpp:24`). This is harmless at boot because the AGT is already
    `PRE_DIVE`, but it means the "follow Lua to `PRE_DIVE`" rule cannot
    distinguish "Lua says CONFIG" from "we have never heard from Lua".
20. **`COMMAND_LONG` handling has no source identity check.**
    `mavlink_interface.cpp:503-568` acts on `MAVLINK_CMD_REBOOT` (31014),
    `MAVLINK_CMD_IRIDIUM_TEST` (31013), `MAVLINK_CMD_LED_CONTROL` and
    `MAVLINK_CMD_MISSION_STATUS` from any sysid/compid, unlike every
    `NAMED_VALUE_FLOAT` path which is strictly gated to `1/1` or `1/191`. A GCS
    or any other MAVLink participant can reboot the AGT.
21. **The Iridium test is not state-gated.** `main.cpp:225` runs the test
    whenever `iridiumTestRequested` is set, including while `DIVING`. The
    session blocks the main loop for tens of seconds to minutes and switches
    the shared antenna away from GNSS.
22. **The critical-voltage sustain window is a bare literal.**
    `state_machine.cpp:70` uses `10000UL` rather than a named constant in
    `config.h`, unlike every other threshold in the failsafe path.
23. **`enterState` re-arms Pi power on every state entry.** All three cases at
    `state_machine.cpp:239-257` call `RelayController_setPowerManagement(true)`.
    If the AGT has already cut power and something later drives a state entry —
    for instance the serial `reset` command, or a Lua restart publishing
    `STATE=-1` while the Pi is coming back up — the power relay closes again.
    This is presumably intentional as a recovery mechanism, but it means the
    cut is not one-way within a single boot.
25. **`status.previousState` is written but never read** outside
    `StateMachine_getStatus()`, which no production caller inspects for that
    field.
