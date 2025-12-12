# State Machine Architecture

## Overview

The AGT firmware uses a **state-based architecture** where the system operates in one of four distinct states. **ArduPilot/Navigator is the primary decision maker** for state transitions, while the AGT can autonomously trigger emergency mode based on sensor thresholds.

## State Diagram

```
                    ┌──────────────────┐
                    │  PREDEPLOYMENT   │◄───┐
                    │  (Surface Test)  │    │
                    └────────┬─────────┘    │
                             │              │
                     start_mission          reset
                             │              │
                    ┌────────▼─────────┐    │
             ┌─────►│     MISSION      │────┘
             │      │ (ArduPilot Leads)│
             │      └────────┬─────────┘
             │               │
             │       drop weight released
             │         or commanded
             │               │
             │      ┌────────▼─────────┐
             └──────┤     RECOVERY     │
      exit_emergency│ (Low Power Wait) │
                    └────────┬─────────┘
                             │
                             │
                    ┌────────▼─────────┐
                    │    EMERGENCY     │
                    │  (Failsafe Mode) │
                    └──────────────────┘
                             ▲
                             │
                    Emergency can be entered
                      from any state
```

## State Definitions

### 1. PREDEPLOYMENT

**Purpose:** System configuration and testing before deployment

**Characteristics:**
- All systems powered ON
- GPS acquiring fix
- Iridium/Meshtastic can be tested
- Configuration via BlueOS/serial
- System health checks
- Drop weight can be armed

**Relay States:**
- Relay 1 (Power Management): **ON** (all systems powered)
- Relay 2 (Drop Weight): **OFF** (disarmed)

**Allowed Transitions:**
- → MISSION (via `start_mission` command)
- → EMERGENCY (via `emergency` command or sensor trigger)

**NeoPixel Display:**
- Blue rainbow during boot
- Yellow pulse while searching for GPS
- Green pulse when GPS fix acquired

---

### 2. MISSION

**Purpose:** Active deployment - ArduPilot controls the mission

**Characteristics:**
- ArduPilot makes mission decisions
- AGT monitors:
  - Drop weight trigger time
  - Emergency sensor thresholds
  - GPS tracking
- All systems operational
- Navigator/Pi recording video and data
- Camera and lights active

**Relay States:**
- Relay 1 (Power Management): **ON** (all systems powered)
- Relay 2 (Drop Weight): **Armed**, triggers at programmed time

**Allowed Transitions:**
- → RECOVERY (after drop weight release or commanded)
- → EMERGENCY (via command or autonomous sensor trigger)

**NeoPixel Display:**
- Green pulse (GPS fix, operational)
- Magenta chase (during Iridium transmission)
- Yellow pulse (if GPS fix lost)

**Drop Weight Trigger:**
The state machine monitors the drop weight trigger time and automatically:
1. Activates Relay 2 for programmed duration (typically 5 seconds)
2. Transitions to RECOVERY state after successful release

---

### 3. RECOVERY

**Purpose:** Low power surface mode awaiting recovery

**Characteristics:**
- **Navigator/Pi powered OFF** (via Relay 1)
- Camera powered OFF
- Lights powered OFF
- AGT continues:
  - GPS tracking
  - Iridium position reporting
  - Meshtastic broadcasts
- Conserving battery for extended surface wait
- NeoPixels active for visual location aid

**Relay States:**
- Relay 1 (Power Management): **OFF** (nonessentials shut down)
- Relay 2 (Drop Weight): **N/A** (already triggered)

**Allowed Transitions:**
- → EMERGENCY (if additional problems detected)
- → PREDEPLOYMENT (via `reset` command after recovery)

**NeoPixel Display:**
- Green pulse (operational, awaiting recovery)
- Magenta chase (during Iridium transmission)

**Power Budget (Recovery Mode):**
- AGT: 1-2W
- GPS: 0.03W (in power save)
- Iridium: 2-5W average (spikes during TX)
- Meshtastic: 0.5-1W
- NeoPixels: 0.5-2W
- **Total: ~4-10W** (vs 16-62W in mission mode)

---

### 4. EMERGENCY

**Purpose:** Failsafe mode for abort scenarios

**Characteristics:**
- **Immediate drop weight release**
- **Immediate nonessentials shutdown**
- AGT continues minimal operations:
  - GPS tracking
  - Iridium emergency beacon
  - Meshtastic distress broadcasts
- Can be triggered by:
  - ArduPilot command
  - AGT autonomous sensor detection
  - Manual command

**Relay States:**
- Relay 1 (Power Management): **OFF** (immediate shutdown)
- Relay 2 (Drop Weight): **Triggered** (immediate release)

**Allowed Transitions:**
- → RECOVERY (automatic after 30 seconds)

**NeoPixel Display:**
- **Red fast blink** (emergency indicator)

**Emergency Triggers:**
| Source | Description | Action |
|--------|-------------|--------|
| EMERGENCY_ARDUPILOT | ArduPilot/Navigator command | Immediate emergency mode |
| EMERGENCY_DEPTH_SENSOR | Depth threshold exceeded | Release and surface |
| EMERGENCY_TEMPERATURE | Temperature out of range | Protect electronics |
| EMERGENCY_PRESSURE | Pressure sensor failure | Failsafe surface |
| EMERGENCY_TIMEOUT | Mission timeout exceeded | Automatic abort |
| EMERGENCY_MANUAL | Manual abort command | User-initiated emergency |

**Auto-Recovery:**
After 30 seconds in emergency mode, system automatically transitions to RECOVERY state (unless sensors still indicate emergency).

---

## State Transitions

### Command-Based Transitions

Sent from ArduPilot/BlueOS via serial:

```
start_mission              # PREDEPLOYMENT → MISSION
enter_recovery             # MISSION → RECOVERY
emergency                  # Any → EMERGENCY
exit_emergency             # EMERGENCY → RECOVERY
reset                      # Any → PREDEPLOYMENT
```

### Automatic Transitions

| From | To | Trigger | Description |
|------|----|---------| ------------|
| MISSION | RECOVERY | Drop weight released | Ballast release successful |
| EMERGENCY | RECOVERY | 30 seconds elapsed | Emergency handled, stabilized |

### Transition Guards

Some transitions are only allowed from specific states:

- `start_mission`: Only from PREDEPLOYMENT
- `enter_recovery`: Only from MISSION or EMERGENCY
- `emergency`: From any state except EMERGENCY
- `reset`: From any state

---

## Drop Weight Control

### Arming the Drop Weight

```cpp
// Via serial command:
arm_drop <gmt|delay> <time> <duration>

// Examples:
arm_drop gmt 1735689600 5000      // GMT time, 5 second release
arm_drop delay 86400 5000         // 24 hours from boot, 5 second release
```

### Drop Weight States

1. **Disarmed**: Drop weight relay inactive, not monitoring time
2. **Armed**: Monitoring trigger time, ready to release
3. **Released**: Relay has been activated, drop weight ejected

### Trigger Modes

**GMT Mode** (Absolute Time):
- Requires GPS-synchronized RTC
- Precise time-of-day release
- Best for coordinated operations
- Example: Release at exactly 14:00:00 GMT tomorrow

**Delay Mode** (Relative Time):
- Counts seconds from boot/arming
- Independent of GPS
- Best for predictable mission durations
- Example: Release 24 hours after deployment

### Manual Release

```
release_now                # Immediate drop weight release
```

Used for:
- Abort scenarios
- Testing
- Emergency surface command

---

## LED State Indicators

| LED Pattern | State | Meaning |
|-------------|-------|---------|
| Blue rainbow | PREDEPLOYMENT | System booting |
| Yellow pulse | Any | Searching for GPS |
| Green pulse | MISSION/RECOVERY | GPS fix, operational |
| Magenta chase | Any | Iridium transmitting |
| Red fast blink | EMERGENCY | Emergency mode active |

---

## ArduPilot Integration

### Command Protocol

ArduPilot sends state transition commands via serial (115200 baud):

```python
# Python example (BlueOS extension)
import serial

agt = serial.Serial('/dev/ttyUSB0', 115200)

# Start mission
agt.write(b'start_mission\n')

# Arm drop weight (24 hours from now, 5 second release)
agt.write(b'arm_drop delay 86400 5000\n')

# Check status
agt.write(b'status\n')
response = agt.read(1024)
print(response.decode())

# Enter recovery after successful mission
agt.write(b'enter_recovery\n')

# Trigger emergency
agt.write(b'emergency\n')
```

### State Queries

```
status                     # Print current state machine status
```

Response includes:
- Current state
- Time in state
- Drop weight status (armed/released)
- Nonessentials power status
- Emergency source (if applicable)

---

## Autonomous Emergency Detection

The AGT can **autonomously** trigger emergency mode based on sensor thresholds (AGT as failsafe if ArduPilot fails):

### Sensor Monitoring (in checkEmergencySensors())

```cpp
// Example thresholds (to be implemented)
#define MAX_DEPTH_METERS     3000
#define MIN_TEMP_CELSIUS     0
#define MAX_TEMP_CELSIUS     40
#define MAX_MISSION_HOURS    48

if (currentDepth > MAX_DEPTH_METERS) {
    StateMachine_triggerEmergency(EMERGENCY_DEPTH_SENSOR);
}

if (temperature < MIN_TEMP || temperature > MAX_TEMP) {
    StateMachine_triggerEmergency(EMERGENCY_TEMPERATURE);
}

if (missionTime > MAX_MISSION_HOURS) {
    StateMachine_triggerEmergency(EMERGENCY_TIMEOUT);
}
```

These provide a **hardware-level failsafe** independent of ArduPilot.

---

## Battery Monitoring

**Battery monitoring is OPTIONAL** in this architecture:

- ArduPilot/Navigator can handle battery management
- PSM interface can be **disabled** in configuration
- AGT does NOT make power decisions based on battery
- Battery data forwarded to ArduPilot via MAVLink (if PSM enabled)

### Disabling Battery Monitoring

```
disable_psm
save
```

System continues to operate without PSM, battery monitoring handled by ArduPilot.

---

## State Persistence

State machine status is **not** persisted to EEPROM. On power cycle:
- System starts in PREDEPLOYMENT
- Drop weight configuration must be re-sent
- This is intentional for safety (prevents accidental releases)

For persistent configuration, use ConfigManager for intervals and settings.

---

## Testing Commands

### Pre-Deployment Testing

```bash
# Check status
status

# Test GPS
# (wait for green pulse LEDs)

# Test Iridium
# (will transmit if GPS fix and enabled)

# Test drop weight (short delay for testing)
arm_drop delay 10 5000     # 10 seconds, 5 second release
# Wait 10 seconds, observe relay activation

# Reset after test
reset
```

### Mission Simulation

```bash
# Start in predeployment
status

# Arm drop weight for 60 second test mission
arm_drop delay 60 5000

# Start mission
start_mission

# Wait 60 seconds
# System should automatically release and enter recovery

# Check status
status

# Should show: STATE_RECOVERY, dropWeightReleased: YES

# Reset for next test
reset
```

### Emergency Testing

```bash
# From any state
emergency

# Should see:
# - Immediate relay activations
# - Red blinking LEDs
# - State: EMERGENCY

# After 30 seconds, auto-transition to RECOVERY
status
```

---

## Troubleshooting

### Drop Weight Not Releasing

1. Check if armed: `status` → `dropWeightArmed: YES`
2. Check trigger time hasn't passed
3. Verify RTC synchronized (for GMT mode)
4. Check relay wiring (GPIO35)
5. Test with `release_now` for immediate release

### Stuck in One State

- Use `reset` command to return to PREDEPLOYMENT
- Check for serial communication issues
- Verify state transition guards are met

### Emergency Mode Triggering Unexpectedly

- Check sensor thresholds in `checkEmergencySensors()`
- Review serial logs for trigger source
- May indicate actual hardware problem

### Relays Not Switching

- Verify relay power supply
- Check GPIO4 (power mgmt) and GPIO35 (drop weight)
- Test with multimeter
- Check RELAY_ACTIVE_HIGH setting in config.h

---

## API Reference

### State Machine Functions

```cpp
// Initialize state machine
void StateMachine_init();

// Update state machine (call in loop)
void StateMachine_update();

// Get current state
SystemState StateMachine_getState();

// Get detailed status
StateMachineStatus StateMachine_getStatus();

// Request state transition
bool StateMachine_requestTransition(StateTransition transition);

// Trigger emergency
bool StateMachine_triggerEmergency(EmergencySource source);

// Check if in emergency
bool StateMachine_isEmergency();

// Arm drop weight
void StateMachine_armDropWeight(bool useGMT, uint32_t triggerTime, uint32_t durationSeconds);

// Manual release
void StateMachine_releaseDropWeight();

// Get time in current state (seconds)
uint32_t StateMachine_getTimeInState();

// State-specific queries
bool StateMachine_canTransmitIridium();
bool StateMachine_canTransmitMeshtastic();
bool StateMachine_shouldShutdownNonessentials();

// Print state to serial
void StateMachine_printState();
```

---

## Best Practices

1. **Always arm drop weight before starting mission**
   ```
   arm_drop gmt 1735689600 5000
   start_mission
   ```

2. **Test state transitions on bench before deployment**

3. **Monitor state status during mission** via serial logs

4. **Use delay mode if GPS sync uncertain** (more reliable for deep missions)

5. **Set emergency sensor thresholds conservatively** (failsafe should trigger early)

6. **Verify drop weight mechanism** works before relying on timed release

7. **Plan for extended recovery wait** (days on surface possible)

---

## References

- [Main Firmware Documentation](../README_FIRMWARE.md)
- [Mission Profile](MISSION_PROFILE.md)
- [BlueOS Integration](BLUEOS_INTEGRATION.md)
- [Configuration Commands](../README_FIRMWARE.md#configuration)
