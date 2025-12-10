# BlueOS Integration Guide

## Overview

The AGT firmware is designed to be configured and controlled via a **BlueOS extension** running on the Navigator/Raspberry Pi combo. This document describes the integration architecture and communication protocol.

## System Architecture

```
┌─────────────────────────────────────────────────────────┐
│                    Drop Camera System                    │
└─────────────────────────────────────────────────────────┘

┌──────────────────────┐              ┌──────────────────┐
│   Navigator + Pi     │              │       AGT        │
│      (BlueOS)        │◄────USB──────┤  Communications  │
│                      │   MAVLink    │      Hub         │
└──────────────────────┘   + Config   └──────────────────┘
         │                                     │
         │                              ┌──────┴──────┐
    ┌────▼────┐                        │             │
    │ BlueOS  │                    ┌───▼───┐    ┌────▼────┐
    │Extension│                    │ Mesh  │    │ Iridium │
    │(Web UI) │                    │RAK603 │    │  9603N  │
    └─────────┘                    └───────┘    └─────────┘
         │
    User Config
```

## Communication Protocol

### USB Serial Connection

The Navigator communicates with the AGT via **USB serial** at **115200 baud**.

Two communication paths are used:
1. **MAVLink Protocol** - GPS data from AGT to Navigator
2. **Configuration Commands** - Settings from BlueOS extension to AGT

### Configuration Message Format

The AGT accepts the same text-based serial commands as documented in the firmware, but these should be sent programmatically from the BlueOS extension.

**Command Structure:**
```
<command> <parameters>\n
```

All commands must be terminated with newline (`\n`).

### Command Reference

#### View Configuration
```
config
```
Returns current configuration in human-readable format.

#### Set Iridium Interval
```
set_iridium_interval <seconds>
```
Example: `set_iridium_interval 600` (10 minutes)

#### Set Meshtastic Interval
```
set_meshtastic_interval <seconds>
```
Example: `set_meshtastic_interval 30` (30 seconds)

#### Enable/Disable Features
```
enable_<feature>
disable_<feature>
```
Features: `iridium`, `meshtastic`, `mavlink`, `psm`, `neopixels`

Examples:
- `enable_iridium`
- `disable_mavlink`

#### Set Drop Weight Release Time
```
set_timed_event <gmt|delay> <time> <duration_ms>
```

**GMT Mode** (absolute time):
```
set_timed_event gmt 1735689600 5000
```
- `gmt` - Use absolute GMT time
- `1735689600` - Unix timestamp (seconds since epoch)
- `5000` - Duration to hold relay active (milliseconds)

**Delay Mode** (relative to power-on):
```
set_timed_event delay 86400 5000
```
- `delay` - Use time since boot
- `86400` - Seconds after deployment (24 hours)
- `5000` - Duration to hold relay active (milliseconds)

#### Set Power Save Voltage
```
set_power_save_voltage <volts>
```
Example: `set_power_save_voltage 11.5`

Relay 1 will turn OFF (disabling Navigator/Pi, camera, lights) when battery drops below this voltage.

#### Save Configuration
```
save
```
**IMPORTANT**: Must be called after configuration changes to persist to EEPROM.

#### Reset to Defaults
```
reset
```
Resets all settings to factory defaults.

## BlueOS Extension Requirements

### Extension Features

The BlueOS extension should provide a web-based UI with:

1. **Mission Configuration Tab**
   - Drop weight release time picker (date/time or delay)
   - Iridium reporting interval slider
   - Meshtastic update rate selector
   - Power save voltage input

2. **System Status Tab**
   - GPS fix status and position
   - Battery voltage, current, power
   - Iridium signal quality
   - Time until drop weight release
   - Current system state (LED color/pattern)

3. **Advanced Settings Tab**
   - Feature enable/disable toggles
   - LED brightness control
   - Manual relay testing
   - Configuration backup/restore

### Communication Implementation

**Python Example** (for BlueOS extension backend):

```python
import serial
import time

class AGTController:
    def __init__(self, port='/dev/ttyUSB0', baud=115200):
        self.ser = serial.Serial(port, baud, timeout=1)
        time.sleep(2)  # Wait for connection

    def send_command(self, command):
        """Send command and read response"""
        self.ser.write(f"{command}\n".encode())
        time.sleep(0.1)

        # Read response
        response = []
        while self.ser.in_waiting:
            line = self.ser.readline().decode('utf-8').strip()
            response.append(line)

        return response

    def get_config(self):
        """Get current configuration"""
        return self.send_command("config")

    def set_drop_weight_release(self, timestamp, duration_ms=5000):
        """Set drop weight release time (GMT)"""
        cmd = f"set_timed_event gmt {timestamp} {duration_ms}"
        response = self.send_command(cmd)
        self.send_command("save")
        return response

    def set_iridium_interval(self, seconds):
        """Set Iridium reporting interval"""
        cmd = f"set_iridium_interval {seconds}"
        response = self.send_command(cmd)
        self.send_command("save")
        return response

    def enable_feature(self, feature):
        """Enable a feature (iridium, meshtastic, mavlink, psm, neopixels)"""
        response = self.send_command(f"enable_{feature}")
        self.send_command("save")
        return response

    def disable_feature(self, feature):
        """Disable a feature"""
        response = self.send_command(f"disable_{feature}")
        self.send_command("save")
        return response

# Usage
agt = AGTController()

# Configure mission
agt.set_drop_weight_release(1735689600, 5000)  # Jan 1, 2025, 5sec
agt.set_iridium_interval(600)  # 10 minutes
agt.enable_feature("iridium")
agt.enable_feature("meshtastic")

# Get status
config = agt.get_config()
print("\n".join(config))
```

### Receiving Status Updates

The AGT continuously outputs status information on the serial port. The extension should monitor these messages:

**GPS Status:**
```
GPS: Fix - Lat: 37.422408 Lon: -122.084108 Sats: 12
GPS: Searching... Sats: 4 Fix: 0
```

**Battery Status:**
```
PSM: V=12.45V I=1.23A P=15.32W (ADC: V=1234 I=5678)
```

**Iridium Status:**
```
Iridium: Signal quality: 4
Iridium: Message sent successfully
Iridium: Send failed: error 32
```

**Relay Status:**
```
Relay: Power management ON
Relay: Power management OFF
Relay: Triggering timed event for 5000ms
Relay: Timed event completed
```

### Web UI Design Suggestions

#### Mission Setup Page

```
┌─────────────────────────────────────────────────┐
│  Drop Camera Mission Configuration              │
├─────────────────────────────────────────────────┤
│                                                  │
│  Drop Weight Release:                            │
│  ○ Delay from deployment: [24] hours [0] minutes│
│  ○ Specific GMT time: [Date/Time Picker]        │
│                                                  │
│  Release Duration: [5000] milliseconds           │
│                                                  │
│  Communication Settings:                         │
│  Iridium Interval: [10] minutes                  │
│  Meshtastic Interval: [30] seconds               │
│                                                  │
│  Power Management:                               │
│  Low Battery Threshold: [11.5] volts             │
│                                                  │
│  [Deploy Mission] [Save as Template]             │
└─────────────────────────────────────────────────┘
```

#### Status Dashboard

```
┌─────────────────────────────────────────────────┐
│  System Status                                   │
├─────────────────────────────────────────────────┤
│                                                  │
│  GPS: ● FIX  Lat: 37.422408  Lon: -122.084108   │
│       12 satellites, HDOP: 1.2                   │
│                                                  │
│  Battery: 12.45V, 1.23A (15.3W)                  │
│  ████████████████░░░░ 82%                        │
│                                                  │
│  Drop Weight: Armed                              │
│  Release in: 23h 45m 12s                         │
│                                                  │
│  Iridium: Signal Quality: ●●●●○ (4/5)           │
│  Last TX: 3 minutes ago                          │
│                                                  │
│  Status LEDs: 🟢 Green Pulse (GPS Fix)           │
│                                                  │
│  [Abort Mission] [Manual Relay Test]             │
└─────────────────────────────────────────────────┘
```

## MAVLink Integration

The AGT sends MAVLink messages to the Navigator containing GPS data. The BlueOS extension can subscribe to these MAVLink messages via the BlueOS MAVLink router.

**Relevant MAVLink Messages:**

1. **HEARTBEAT** (ID 0)
   - System ID: 1
   - Component ID: MAV_COMP_ID_GPS
   - Sent every 1 second

2. **GPS_RAW_INT** (ID 24)
   - Position, altitude, velocity
   - Fix type and satellite count
   - Sent at configured rate (default 1 Hz)

3. **BATTERY_STATUS** (ID 147)
   - Voltage, current, remaining capacity
   - Sent with GPS updates

## Time Synchronization

For accurate GMT-based drop weight release, the AGT's RTC must be synchronized:

1. **GPS Time Sync**: The AGT automatically updates its RTC when GPS fix is acquired
2. **Manual Sync**: The BlueOS extension could send time sync commands (future feature)

**Current Implementation:**
The AGT updates its RTC from GPS automatically. The BlueOS extension should display:
- RTC status (synchronized/not synchronized)
- Time since last GPS sync
- Warning if RTC drift is detected

## Testing and Validation

### Pre-Deployment Checklist

The BlueOS extension should provide a testing interface:

```python
def run_predeploy_checks(agt):
    """Run pre-deployment system checks"""

    checks = {
        "GPS Fix": False,
        "Iridium Signal": False,
        "Battery Voltage": False,
        "Drop Weight Armed": False,
        "PSM Reading": False,
        "Meshtastic Link": False
    }

    # GPS check
    status = agt.send_command("status")  # Future command
    if "GPS: Fix" in str(status):
        checks["GPS Fix"] = True

    # Battery check
    if "PSM: V=" in str(status):
        checks["PSM Reading"] = True
        # Parse voltage and check > 12V

    # More checks...

    return checks
```

### Simulation Mode

For testing without deployment, the extension should support:
- Simulated GPS positions
- Fast-forward time for drop weight release testing
- Manual relay triggering
- Iridium message simulation

## Error Handling

The BlueOS extension should handle these error conditions:

### Communication Errors
```python
try:
    response = agt.send_command("config")
except serial.SerialException:
    # AGT not responding
    log_error("AGT communication failed")
    alert_user("Check USB connection to AGT")
```

### Configuration Errors
- Invalid timestamps (in the past)
- Out-of-range values
- Failed EEPROM save

### Mission Errors
- GPS not acquiring fix before deployment
- Low battery at deployment
- Iridium signal too weak
- Drop weight relay failure

## BlueOS Extension File Structure

```
blueos-extension-drop-camera/
├── manifest.json          # Extension metadata
├── docker-compose.yml     # Container configuration
├── Dockerfile             # Build instructions
├── app/
│   ├── main.py           # FastAPI backend
│   ├── agt_controller.py # AGT serial interface
│   ├── mavlink_mon.py    # MAVLink message handler
│   └── static/
│       ├── index.html    # Web UI
│       ├── app.js        # Frontend logic
│       └── styles.css    # UI styling
└── README.md             # Extension documentation
```

## API Endpoints (Suggested)

For the BlueOS extension web service:

```
GET  /api/status          - Get current AGT status
POST /api/config          - Update AGT configuration
GET  /api/config          - Get current AGT configuration
POST /api/mission/arm     - Arm drop weight release
POST /api/mission/abort   - Abort mission, release immediately
POST /api/relay/test      - Test relay operation
GET  /api/battery         - Get battery status
GET  /api/gps             - Get current GPS position
POST /api/iridium/send    - Send custom Iridium message
```

## Development Resources

- **BlueOS Extension Template**: https://github.com/bluerobotics/BlueOS-examples
- **BlueOS Documentation**: https://docs.bluerobotics.com/ardusub-zola/software/onboard/BlueOS-latest/
- **MAVLink Python**: https://github.com/ArduPilot/pymavlink
- **Serial Communication**: Python `pyserial` library

## Next Steps

1. Develop BlueOS extension skeleton using template
2. Implement AGT serial communication layer
3. Create web UI for mission configuration
4. Add MAVLink monitoring for GPS data
5. Implement pre-deployment testing suite
6. Add mission logging and data export
7. Test end-to-end deployment workflow

## Support

For AGT firmware questions: See [README_FIRMWARE.md](../README_FIRMWARE.md)
For BlueOS extension development: See BlueOS documentation
