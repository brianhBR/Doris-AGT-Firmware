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
└──────────────────────┘   57600     └──────────────────┘
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

The Navigator communicates with the AGT via **USB serial** at **57600 baud**.

Two communication paths share the same USB serial:
1. **MAVLink Protocol** - Binary messages (GPS, battery, system time, heartbeat)
2. **Text Commands** - Configuration and state control commands

### Command Message Format

The AGT accepts text-based serial commands. All commands must be terminated with newline (`\n`).

```
<command> <parameters>\n
```

### Command Reference

#### State Control

```
start_self_test                    # PRE_MISSION → SELF_TEST
reset                              # Any → PRE_MISSION
release_now                        # Trigger failsafe (fire release relay → RECOVERY)
status                             # Print state machine status
```

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
Example: `set_meshtastic_interval 3` (3 seconds)

#### Set MAVLink Interval
```
set_mavlink_interval <ms>
```
Example: `set_mavlink_interval 1000` (1 Hz)

#### Enable/Disable Features
```
enable_<feature>
disable_<feature>
```
Features: `iridium`, `meshtastic`, `mavlink`, `psm`, `neopixels`

Examples:
- `enable_iridium`
- `disable_psm`

#### Set Timed Event (Release Relay)
```
set_timed_event <gmt|delay> <time> <duration_seconds>
```

**GMT Mode** (absolute time):
```
set_timed_event gmt 1735689600 1500
```
- `gmt` - Use absolute GMT time
- `1735689600` - Unix timestamp (seconds since epoch)
- `1500` - Duration to hold relay active (seconds, 25 min for electrolytic release)

**Delay Mode** (relative to power-on):
```
set_timed_event delay 86400 1500
```
- `delay` - Use time since boot
- `86400` - Seconds after deployment (24 hours)
- `1500` - Duration to hold relay active (seconds)

#### Set Power Save Voltage
```
set_power_save_voltage <volts>
```
Example: `set_power_save_voltage 11.5`

#### Save Configuration
```
save
```
**IMPORTANT**: Must be called after configuration changes to persist to EEPROM.

#### GPS Information
```
gps                                # Show GPS position or satellite count
gps_diag                           # GPS BBR/backup battery diagnostics
```

#### Meshtastic Testing
```
mesh_test                          # Send test text
mesh_test_gps                      # Send test NMEA coordinates
mesh_send <text>                   # Send custom text
```

#### Leak Testing
```
set_leak <0|1>                     # Set/clear leak flag
```

## BlueOS Extension Requirements

### Extension Features

The BlueOS extension should provide a web-based UI with:

1. **Mission Configuration Tab**
   - Timed event configuration (date/time or delay, duration in seconds)
   - Iridium reporting interval slider
   - Meshtastic update rate selector

2. **System Status Tab**
   - Current state (PRE_MISSION / SELF_TEST / MISSION / RECOVERY)
   - GPS fix status and position
   - Battery voltage, current, power (from PSM or MAVLink)
   - Time in current state
   - Release relay status (triggered / not triggered)
   - Last failsafe source (if any)

3. **Advanced Settings Tab**
   - Feature enable/disable toggles
   - LED brightness control
   - Manual relay testing (`release_now`)
   - State control (`start_self_test`, `reset`)

### Communication Implementation

**Python Example** (for BlueOS extension backend):

```python
import serial
import time

class AGTController:
    def __init__(self, port='/dev/ttyUSB0', baud=57600):
        self.ser = serial.Serial(port, baud, timeout=1)
        time.sleep(2)

    def send_command(self, command):
        """Send command and read response"""
        self.ser.write(f"{command}\n".encode())
        time.sleep(0.1)

        response = []
        while self.ser.in_waiting:
            line = self.ser.readline().decode('utf-8').strip()
            response.append(line)

        return response

    def get_config(self):
        """Get current configuration"""
        return self.send_command("config")

    def get_status(self):
        """Get state machine status"""
        return self.send_command("status")

    def start_self_test(self):
        """Start self-test sequence"""
        return self.send_command("start_self_test")

    def release_now(self):
        """Trigger failsafe release"""
        return self.send_command("release_now")

    def reset(self):
        """Return to PRE_MISSION"""
        return self.send_command("reset")

    def set_timed_event(self, mode, time_val, duration_s=1500):
        """Set timed event (mode: 'gmt' or 'delay')"""
        cmd = f"set_timed_event {mode} {time_val} {duration_s}"
        response = self.send_command(cmd)
        self.send_command("save")
        return response

    def set_iridium_interval(self, seconds):
        """Set Iridium reporting interval"""
        response = self.send_command(f"set_iridium_interval {seconds}")
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
agt.set_timed_event("delay", 86400, 1500)  # 24hr delay, 25min release
agt.set_iridium_interval(600)  # 10 minutes
agt.enable_feature("iridium")
agt.enable_feature("meshtastic")

# Start self-test
agt.start_self_test()

# Get status
status = agt.get_status()
print("\n".join(status))
```

### Receiving Status Updates

The AGT continuously outputs status information on the serial port. The extension should parse these messages:

**GPS Status:**
```
GPS: Fix - Lat: 37.422408 Lon: -122.084108 Sats: 12
```

**Battery Status (if PSM enabled):**
```
PSM: V=12.45V I=1.23A P=15.32W (ADC: V=1234 I=5678)
```

**State Machine Status (every 60s and on `status` command):**
```
===== STATE =====
State: MISSION
Time in state: 3600 s
Nonessentials: ON
Release triggered: NO
=================
```

**Relay Status:**
```
Relay: Power management ON
Relay: Power management OFF
Relay: Triggering timed event for 1500s
Relay: Timed event completed
```

**Failsafe:**
```
FAILSAFE: LOW_VOLTAGE
FAILSAFE: LEAK
FAILSAFE: MANUAL
```

## MAVLink Integration

The AGT sends MAVLink messages to the Navigator containing GPS data, battery status, and system time. The BlueOS extension can subscribe to these MAVLink messages via the BlueOS MAVLink router.

**Messages sent by AGT:**

1. **HEARTBEAT** (ID 0)
   - System ID: 1
   - Component ID: 191 (MAV_COMP_ID_ONBOARD_COMPUTER)
   - Sent every 1 second

2. **GPS_RAW_INT** (ID 24)
   - Position, altitude, velocity
   - Fix type and satellite count
   - Sent at configured rate (default 1 Hz)

3. **BATTERY_STATUS** (ID 147)
   - Voltage, current, remaining capacity estimate
   - Sent with GPS updates

4. **SYSTEM_TIME** (ID 2)
   - RTC time synced from GPS (Unix microseconds + boot time)
   - ArduPilot uses when BRD_RTC_TYPES=2

**Messages received by AGT:**

- **HEARTBEAT** - Updates heartbeat watchdog for failsafe
- **SYS_STATUS** - Battery voltage from autopilot
- **SCALED_PRESSURE** - Depth calculation (pressure-based)
- **VFR_HUD** - Depth from altitude (negative = underwater)
- **BATTERY_STATUS** - Battery voltage from autopilot

## Time Synchronization

For accurate GMT-based timed events, the AGT's RTC must be synchronized:

1. **GPS Time Sync**: The AGT automatically updates its RTC when GPS fix is acquired
2. **SYSTEM_TIME**: The AGT forwards RTC time to ArduPilot via MAVLink SYSTEM_TIME messages

## Development Resources

- **BlueOS Extension Template**: https://github.com/bluerobotics/BlueOS-examples
- **BlueOS Documentation**: https://docs.bluerobotics.com/ardusub-zola/software/onboard/BlueOS-latest/
- **MAVLink Python**: https://github.com/ArduPilot/pymavlink
- **Serial Communication**: Python `pyserial` library

## Support

For AGT firmware questions: See [README_FIRMWARE.md](../README_FIRMWARE.md)
For BlueOS extension development: See BlueOS documentation
