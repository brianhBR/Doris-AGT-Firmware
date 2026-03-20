# State Machine Architecture

## Overview

The AGT firmware uses a **state-based architecture** with four sequential states. **ArduPilot/Navigator provides real-time sensor data** (depth, battery, leak) via MAVLink, and the AGT uses this data for automatic state transitions and failsafe decisions.

## State Diagram

```
                    ┌──────────────────┐
                    │   PRE_MISSION    │◄───┐
                    │  (Initial Setup) │    │
                    └────────┬─────────┘    │
                             │              │
                     start_self_test        reset
                             │              │
                    ┌────────▼─────────┐    │
                    │    SELF_TEST     │────┘
                    │  (Verify Ready)  │
                    └────────┬─────────┘
                             │
                     depth > 2m (MAVLink)
                             │
                    ┌────────▼─────────┐
                    │     MISSION      │
                    │  (Underwater)    │
                    └────────┬─────────┘
                             │
                     depth < 3m OR GPS fix
                     OR failsafe triggered
                             │
                    ┌────────▼─────────┐
                    │     RECOVERY     │
                    │ (Surface / Low   │
                    │   Power / Strobe)│
                    └──────────────────┘
```

## State Definitions

### 1. PRE_MISSION

**Purpose:** Initial power-on state, waiting for operator to begin

**Characteristics:**
- All systems powered ON (Relay 1 ON)
- GPS acquiring fix
- Configuration via serial/BlueOS
- Waiting for `start_self_test` command

**Relay States:**
- Relay 1 (Power Management): **ON** (Navigator/Pi powered)
- Relay 2 (Release): **OFF** (inactive)

**Allowed Transitions:**
- → SELF_TEST (via `start_self_test` command)

**NeoPixel Display:**
- Pre-mission indicator pattern

---

### 2. SELF_TEST

**Purpose:** Verify systems are ready before deployment

**Characteristics:**
- GPS, Iridium, battery, and mission parameters verified
- Iridium can transmit (position check)
- System health confirmed
- Waiting for deployment (depth > 2m from MAVLink)

**Relay States:**
- Relay 1 (Power Management): **ON** (all systems powered)
- Relay 2 (Release): **OFF** (inactive)

**Allowed Transitions:**
- → MISSION (automatic when MAVLink depth > 2m)
- → PRE_MISSION (via `reset` command)

**NeoPixel Display:**
- Self-test indicator pattern

**Automatic Transition:**
The AGT monitors depth data from ArduPilot via MAVLink (SCALED_PRESSURE or VFR_HUD messages). When confirmed depth exceeds `MISSION_DEPTH_THRESHOLD_M` (2.0m), the system automatically transitions to MISSION.

---

### 3. MISSION

**Purpose:** Active underwater deployment

**Characteristics:**
- ArduPilot recording video and sensor data
- AGT monitors failsafe conditions:
  - Battery voltage (from autopilot via MAVLink)
  - Leak detection
  - Maximum depth exceeded
  - Loss of autopilot heartbeat
- GPS tracking (no fix expected underwater)
- Meshtastic NMEA output continues

**Relay States:**
- Relay 1 (Power Management): **ON** (all systems powered)
- Relay 2 (Release): **OFF** (until failsafe triggers it)

**Allowed Transitions:**
- → RECOVERY (automatic when depth < 3m or GPS fix acquired)
- → RECOVERY (via failsafe trigger — release relay fires first)

**NeoPixel Display:**
- Green pulse (GPS fix, operational)
- Yellow pulse (no GPS fix)

**Failsafe Monitoring:**
The state machine continuously checks for failsafe conditions during MISSION (see Failsafe System below).

---

### 4. RECOVERY

**Purpose:** Surface recovery mode with visual and satellite tracking

**Characteristics:**
- **Navigator/Pi powered OFF** (Relay 1 OFF) to conserve power
- Camera and lights powered OFF
- AGT continues:
  - GPS tracking
  - Iridium position reporting (with mission stats)
  - Meshtastic NMEA broadcasts
- **Strobe LEDs** active for visual location aid
- Conserving battery for extended surface wait

**Relay States:**
- Relay 1 (Power Management): **OFF** (nonessentials shut down)
- Relay 2 (Release): **N/A** (may have been triggered by failsafe)

**Allowed Transitions:**
- → PRE_MISSION (via `reset` command after physical recovery)

**NeoPixel Display:**
- **Recovery strobe** (high-visibility flashing for locating)

---

## Failsafe System

The failsafe system replaces the previous EMERGENCY state. When a failsafe condition is detected during MISSION, the AGT:

1. Fires the release relay (Relay 2) for `RELEASE_RELAY_DURATION_SEC` (default 1500 seconds / 25 minutes for electrolytic release)
2. Transitions immediately to RECOVERY state

### Failsafe Triggers

| Source | Constant | Condition | Description |
|--------|----------|-----------|-------------|
| Low Voltage | `FAILSAFE_LOW_VOLTAGE` | Autopilot-confirmed voltage < 11.0V | Battery critically low |
| Leak | `FAILSAFE_LEAK` | Leak detected via MAVLink/sensor | Water ingress |
| Max Depth | `FAILSAFE_MAX_DEPTH` | Depth > 200m | Exceeded safe operating depth |
| No Heartbeat | `FAILSAFE_NO_HEARTBEAT` | No MAVLink heartbeat for 30s | Autopilot communication lost |
| Manual | `FAILSAFE_MANUAL` | `release_now` command | Operator-initiated abort |

**Voltage failsafe** only acts on voltage data confirmed from the autopilot via MAVLink (not PSM fallback data), preventing false triggers from noisy analog readings.

### Failsafe Behavior

- The release relay fires **once** — if already triggered, subsequent failsafe events do not re-trigger it
- All failsafe sources result in the same action: release relay + RECOVERY
- The failsafe source is logged and reported in `status` output

---

## State Transitions

### Command-Based Transitions

Sent via USB serial (57600 baud):

```
start_self_test           # PRE_MISSION → SELF_TEST
reset                     # Any → PRE_MISSION
release_now               # MISSION → fires release relay → RECOVERY
```

### Automatic Transitions

| From | To | Trigger | Description |
|------|----|---------| ------------|
| SELF_TEST | MISSION | MAVLink depth > 2m | Deployment confirmed |
| MISSION | RECOVERY | Depth < 3m OR GPS fix | Surfaced |
| MISSION | RECOVERY | Any failsafe trigger | Release relay fired, enter low-power mode |

### Transition Guards

- `start_self_test`: Only from PRE_MISSION
- `enterMission`: Only from SELF_TEST (automatic, not a serial command)
- Failsafe checks: Only run during MISSION state

---

## Iridium Transmission Windows

Iridium can only transmit in certain states (GPS and Iridium share the antenna via RF switch):

- **SELF_TEST**: Allowed (pre-deployment position check)
- **RECOVERY**: Allowed (position reporting for recovery)
- **PRE_MISSION**: Not allowed
- **MISSION**: Not allowed (underwater, no signal)

---

## LED State Indicators

| LED Pattern | State | Meaning |
|-------------|-------|---------|
| Pre-mission pattern | PRE_MISSION | Waiting for operator |
| Self-test pattern | SELF_TEST | System verification |
| Green pulse | MISSION (GPS fix) | Operational with GPS |
| Yellow pulse | MISSION (no fix) | Operational, no GPS |
| Recovery strobe | RECOVERY | Flashing for visual location |

---

## Serial Commands

```
help                      # Show all commands
start_self_test           # PRE_MISSION → SELF_TEST
status                    # Print current state, time, failsafe info
gps                       # Show GPS position or satellite count
gps_diag                  # GPS BBR/backup battery diagnostics
release_now               # Trigger failsafe (fires release relay → RECOVERY)
reset                     # Return to PRE_MISSION
set_leak <0|1>            # Set/clear leak flag for testing
mesh_test                 # Send test text to Meshtastic
mesh_test_gps             # Send test NMEA to Meshtastic
mesh_send <text>          # Send custom text to Meshtastic
config                    # Show configuration
save                      # Save config to EEPROM
set_iridium_interval <s>  # Set Iridium interval (seconds)
set_meshtastic_interval <s>  # Set Meshtastic interval (seconds)
set_mavlink_interval <ms> # Set MAVLink interval (milliseconds)
set_timed_event <gmt|delay> <time> <duration_s>
set_power_save_voltage <V>
enable_<feature>          # Enable feature
disable_<feature>         # Disable feature
```

---

## MissionData

The `MissionData` module collects real-time data from the autopilot via MAVLink for state transitions and failsafe decisions:

| Field | Source | Purpose |
|-------|--------|---------|
| `depth_m` | MAVLink SCALED_PRESSURE / VFR_HUD | State transitions (SELF_TEST→MISSION, MISSION→RECOVERY) |
| `max_depth_m` | Tracked from depth updates | Failsafe: max depth check, Iridium reports |
| `battery_voltage` | MAVLink SYS_STATUS / BATTERY_STATUS, fallback PSM | Failsafe: low voltage |
| `leak_detected` | MAVLink or `set_leak` command | Failsafe: water ingress |
| `last_heartbeat_ms` | MAVLink HEARTBEAT | Failsafe: heartbeat timeout |

---

## Configuration Thresholds

Defined in `config.h`:

```cpp
#define FAILSAFE_HEARTBEAT_TIMEOUT_MS  30000   // 30s no heartbeat → failsafe
#define FAILSAFE_MAX_DEPTH_M           200.0   // Max depth before failsafe
#define MISSION_DEPTH_THRESHOLD_M      2.0     // Depth to enter MISSION
#define RECOVERY_DEPTH_THRESHOLD_M     3.0     // Depth to enter RECOVERY
#define BATTERY_CRITICAL_VOLTAGE       11.0    // Voltage failsafe trigger
#define RELEASE_RELAY_DURATION_SEC     1500    // Release relay on-time (25 min)
```

---

## State Persistence

State machine status is **not** persisted to EEPROM. On power cycle:
- System starts in PRE_MISSION
- Release relay status resets
- This is intentional for safety

For persistent settings (intervals, features), use ConfigManager with `save` command.

---

## API Reference

### State Machine Functions

```cpp
void StateMachine_init();
void StateMachine_update();

SystemState StateMachine_getState();
StateMachineStatus StateMachine_getStatus();
uint32_t StateMachine_getTimeInState();

void StateMachine_startSelfTest();       // PRE_MISSION → SELF_TEST
void StateMachine_enterMission();        // SELF_TEST → MISSION (called by main on depth)
void StateMachine_enterRecovery();       // → RECOVERY
void StateMachine_reset();               // → PRE_MISSION

void StateMachine_triggerFailsafe(FailsafeSource source);

bool StateMachine_canTransmitIridium();
bool StateMachine_canTransmitMeshtastic();
bool StateMachine_shouldShutdownNonessentials();
bool StateMachine_isRecoveryStrobe();

void StateMachine_printState();
```

---

## References

- [Main Firmware Documentation](../README_FIRMWARE.md)
- [Mission Profile](MISSION_PROFILE.md)
- [BlueOS Integration](BLUEOS_INTEGRATION.md)
