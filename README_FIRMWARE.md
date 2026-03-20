# Doris AGT Firmware

Comprehensive firmware for the SparkFun Artemis Global Tracker with multi-interface communication and control capabilities for an oceanographic drop camera system.

## Features

### Core Functionality
- **GPS Position Tracking** - u-blox ZOE-M8Q GNSS receiver
- **Iridium Satellite Communication** - Global position reporting with mission stats via Iridium 9603N
- **Meshtastic Integration** - NMEA GPS output to RAK4603 J10 (external GPS) via SoftwareSerial
- **MAVLink Interface** - GPS, battery, and system time forwarding to ArduPilot Navigator via USB
- **Battery Monitoring** - Blue Robotics PSM via analog inputs (GPIO11/12)
- **NeoPixel Status Display** - 30 LED status indicators with recovery strobe mode
- **Relay Control** - Power management relay + electrolytic release relay
- **Failsafe System** - Automatic release on low voltage, leak, max depth, or lost heartbeat

### Advanced Features
- Configurable reporting intervals for all communication channels
- Programmable timed event relay (GMT or delay-based triggering)
- State-based power management (nonessentials OFF in recovery)
- Depth-based automatic state transitions from MAVLink sensor data
- Serial configuration interface with EEPROM persistence
- RTC synchronization from GPS, forwarded to ArduPilot as SYSTEM_TIME
- Mission data tracking (depth, voltage, leak status) from autopilot

## Hardware Requirements

### Main Board
- SparkFun Artemis Global Tracker (AGT)

### Connected Peripherals
1. **Meshtastic RAK4603** - Connected via SoftwareSerial on J10 Qwiic (D39 TX / D40 RX)
2. **ArduPilot Navigator** - Connected via USB (Serial, 57600 baud)
3. **NeoPixel LED Strip** - 30 LEDs connected to GPIO32
4. **Blue Robotics PSM** - Analog inputs: GPIO11 (voltage), GPIO12 (current)
5. **Relays** (2x):
   - Relay 1 (GPIO4): Power management for nonessential systems
   - Relay 2 (GPIO35): Electrolytic release mechanism

### Pin Assignments

| Function | Pin | Description |
|----------|-----|-------------|
| NeoPixel Data | GPIO32 | WS2812 LED strip control |
| Relay 1 | GPIO4 | Power management relay |
| Relay 2 | GPIO35 | Release relay |
| Meshtastic TX | D39 (J10 pin 1) | NMEA to RAK4603 J10 RX |
| Meshtastic RX | D40 (J10 pin 2) | From RAK4603 J10 TX (optional) |
| MAVLink | USB Serial | To/from Navigator (57600 baud) |
| PSM Voltage | GPIO11 (AD11) | Analog battery voltage |
| PSM Current | GPIO12 (AD12) | Analog battery current |

## Software Setup

### PlatformIO Installation

1. Install [VSCode](https://code.visualstudio.com/)
2. Install PlatformIO extension
3. Clone this repository:
   ```bash
   git clone https://github.com/yourusername/Doris-AGT-Firmware.git
   cd Doris-AGT-Firmware
   ```

4. Open the project in VSCode/PlatformIO

### Library Dependencies

The following libraries are automatically installed via `platformio.ini`:
- SparkFun u-blox GNSS Arduino Library
- SparkFun IridiumSBD I2C Arduino Library
- ArduinoJson
- MAVLink C Library v2
- Nanopb (for meshtastic protobuf stubs)

### Building and Uploading

1. Connect your Artemis Global Tracker via USB
2. In PlatformIO:
   - Click "Build" to compile
   - Click "Upload" to flash firmware
   - Click "Monitor" to view serial output (57600 baud)

Or use command line:
```bash
pio run -t upload
pio device monitor -b 57600
```

There is also a `selftest` build environment for isolated hardware testing:
```bash
pio run -e selftest -t upload
```

## Configuration

### Serial Commands

Connect to the AGT via USB serial (57600 baud) and use these commands:

| Command | Description | Example |
|---------|-------------|---------|
| `help` | Show all commands | `help` |
| `config` | Display current configuration | `config` |
| `save` | Save configuration to EEPROM | `save` |
| `reset` | Return to PRE_MISSION state | `reset` |
| `start_self_test` | Begin self-test sequence | `start_self_test` |
| `status` | Show state machine status | `status` |
| `gps` | Show GPS position or satellite count | `gps` |
| `gps_diag` | GPS BBR/backup battery diagnostics | `gps_diag` |
| `release_now` | Trigger failsafe (fire release relay) | `release_now` |
| `set_leak <0\|1>` | Set/clear leak flag for testing | `set_leak 1` |
| `set_iridium_interval <seconds>` | Set Iridium reporting interval | `set_iridium_interval 600` |
| `set_meshtastic_interval <seconds>` | Set Meshtastic update interval | `set_meshtastic_interval 3` |
| `set_mavlink_interval <ms>` | Set MAVLink update interval | `set_mavlink_interval 1000` |
| `enable_<feature>` | Enable a feature | `enable_mavlink` |
| `disable_<feature>` | Disable a feature | `disable_iridium` |
| `set_timed_event <gmt\|delay> <time> <duration_s>` | Configure timed relay event | See below |
| `set_power_save_voltage <volts>` | Set low voltage threshold | `set_power_save_voltage 11.5` |
| `mesh_test` | Send test text to Meshtastic | `mesh_test` |
| `mesh_test_gps` | Send test NMEA coordinates | `mesh_test_gps` |
| `mesh_send <text>` | Send custom text to Meshtastic | `mesh_send hello` |

### Configurable Features
- `iridium` - Iridium satellite communication
- `meshtastic` - Meshtastic mesh networking (NMEA GPS output)
- `mavlink` - MAVLink interface
- `psm` - Battery monitoring (analog, disabled by default)
- `neopixels` - LED status display

### Timed Event Configuration

The timed event relay can be triggered in two modes:

**GMT Mode** (absolute time):
```
set_timed_event gmt 1735689600 1500
```
- `gmt` - Use absolute GMT time
- `1735689600` - Unix timestamp (seconds since 1970)
- `1500` - Duration in seconds (25 minutes for electrolytic release)

**Delay Mode** (relative to boot):
```
set_timed_event delay 86400 1500
```
- `delay` - Use time since boot
- `86400` - Seconds after boot (24 hours)
- `1500` - Duration in seconds

After setting, save:
```
save
```

### Default Configuration

```
Iridium Interval:     600 seconds (10 minutes)
Meshtastic Interval:  3 seconds
MAVLink Interval:     1000 ms (1 Hz)
Power Save Voltage:   11.5V
Enabled:              Iridium, Meshtastic, MAVLink, NeoPixels
Disabled:             PSM (causes MbedOS mutex issues)
Timed Event:          Disabled (default duration 1500s when set)
```

## Operation

### System States

| State | Relay 1 | Relay 2 | Description |
|-------|---------|---------|-------------|
| PRE_MISSION | ON | OFF | Initial setup, waiting for operator |
| SELF_TEST | ON | OFF | System verification, Iridium can TX |
| MISSION | ON | OFF* | Underwater, failsafe monitoring active |
| RECOVERY | OFF | N/A | Surface, strobe LEDs, Iridium reports |

*Relay 2 fires on failsafe trigger during MISSION.

### Failsafe System

During MISSION state, the AGT monitors these conditions:

| Trigger | Threshold | Source |
|---------|-----------|--------|
| Low Voltage | < 11.0V | Autopilot via MAVLink (SYS_STATUS/BATTERY_STATUS) |
| Leak | Detected | MAVLink or `set_leak` command |
| Max Depth | > 200m | MAVLink (SCALED_PRESSURE/VFR_HUD) |
| No Heartbeat | > 30s without | MAVLink HEARTBEAT |
| Manual | `release_now` | Serial command |

When triggered: release relay fires (1500s) and system enters RECOVERY.

### NeoPixel Status

| State | Pattern | Description |
|-------|---------|-------------|
| PRE_MISSION | Pre-mission | Waiting for operator |
| SELF_TEST | Self-test | System verification |
| MISSION (GPS fix) | Green pulse | GPS locked |
| MISSION (no fix) | Yellow pulse | Searching (expected underwater) |
| RECOVERY | Strobe | High-visibility flashing for recovery |

### Data Flow

```
GPS (I2C) ──┬──► MAVLink (Navigator USB, 57600 baud)
            ├──► Meshtastic NMEA (SoftwareSerial, 9600 baud, J10)
            └──► Iridium (Satellite, with mission stats)

MAVLink IN ──┬──► Depth (SCALED_PRESSURE / VFR_HUD)
             ├──► Battery voltage (SYS_STATUS / BATTERY_STATUS)
             ├──► Heartbeat monitoring
             └──► Failsafe decisions

PSM (Analog GPIO11/12) ──► Fallback battery data + telemetry

Configuration ◄──► EEPROM
Status ────────────► NeoPixels
```

## Iridium Messages

Position reports include mission statistics:
```
LAT:37.422408,LON:-122.084108,ALT:15.2,SPD:2.5,SAT:12,BATT:12.45V
```

When in RECOVERY after failsafe, reports also include depth and failsafe source information.

## MAVLink Integration

The AGT sends the following MAVLink messages to the Navigator:

1. **HEARTBEAT** - Every 1 second
   - System ID: 1
   - Component ID: 191 (MAV_COMP_ID_ONBOARD_COMPUTER)

2. **GPS_RAW_INT** - At configured interval (default 1 Hz)
   - Position, altitude, speed, course
   - Fix type and satellite count

3. **BATTERY_STATUS** - With GPS updates
   - Voltage, current, state of charge estimate

4. **SYSTEM_TIME** - With GPS updates
   - RTC time synced from GPS (Unix microseconds)
   - ArduPilot uses this when BRD_RTC_TYPES=2

The AGT receives and processes:

- **HEARTBEAT** - Updates heartbeat watchdog
- **SYS_STATUS** - Battery voltage from autopilot
- **SCALED_PRESSURE** - Depth calculation (pressure-based)
- **VFR_HUD** - Depth from altitude (negative = underwater)
- **BATTERY_STATUS** - Battery voltage from autopilot

## Troubleshooting

### GPS Not Acquiring Fix
- Check antenna connection
- Ensure clear view of sky
- Wait 2-3 minutes for cold start
- Use `gps` command to check satellite count
- Use `gps_diag` to check BBR/backup battery status

### Iridium Transmission Failures
- Verify supercapacitor is charged (PGOOD LED)
- Check signal quality (need >= 2)
- Ensure antenna has clear view of sky
- Iridium only transmits in SELF_TEST and RECOVERY states

### Meshtastic Not Showing Position
- Verify NMEA output with `mesh_test_gps` command
- Check wiring: AGT D39 (TX) → RAK J10 RX
- Ensure common ground connection
- Verify RAK4603 configured for external GPS on J10
- Check baud rate (9600)

### MAVLink Not Working
- Verify USB connection to Navigator
- Baud rate: 57600
- Use MAVLink inspector to verify messages
- Check system/component IDs (sysid=1, compid=191)

### NeoPixels Not Working
- Verify data pin (GPIO32)
- Check external 5V power supply (do NOT use AGT 3.3V)
- Verify LED count (30)

### PSM Not Reading
- Check analog connections on GPIO11 (voltage) and GPIO12 (current)
- PSM is disabled by default; enable with `enable_psm` + `save`
- Verify PSM is powered from battery sense side

### Release Relay Not Firing
- Relay only fires during MISSION state on failsafe trigger
- Test with `release_now` command (triggers failsafe → RECOVERY)
- Check GPIO35 wiring
- Verify relay coil powered from battery (not AGT 3.3V)

## File Structure

```
Doris-AGT-Firmware/
├── include/
│   ├── config.h                    # Pin definitions, timing, thresholds
│   ├── mavlink_platform.h          # MAVLink serial glue
│   ├── APOLLOrtc.h                 # RTC support
│   └── modules/
│       ├── gps_manager.h
│       ├── iridium_manager.h
│       ├── meshtastic_interface.h
│       ├── mavlink_interface.h
│       ├── neopixel_controller.h
│       ├── psm_interface.h
│       ├── relay_controller.h
│       ├── config_manager.h
│       ├── state_machine.h
│       └── mission_data.h
├── src/
│   ├── main.cpp                    # Main program
│   ├── main_selftest.cpp           # Self-test build (separate PIO env)
│   ├── APOLLOrtc.cpp               # RTC implementation
│   └── modules/
│       ├── gps_manager.cpp
│       ├── iridium_manager.cpp
│       ├── meshtastic_interface.cpp
│       ├── mavlink_interface.cpp
│       ├── neopixel_controller.cpp
│       ├── psm_interface.cpp
│       ├── relay_controller.cpp
│       ├── config_manager.cpp
│       ├── state_machine.cpp
│       └── mission_data.cpp
├── lib/
│   └── meshtastic/                 # Meshtastic protobuf stubs
├── utils/
│   └── SoftwareSerial.*            # SoftwareSerial for Meshtastic NMEA
├── platformio.ini                  # PlatformIO configuration (2 environments)
└── README_FIRMWARE.md              # This file
```

## Customization

### Changing Update Intervals

Edit [config.h](include/config.h):
```cpp
#define IRIDIUM_SEND_INTERVAL_MS   600000  // 10 minutes
#define MESHTASTIC_UPDATE_MS       1000    // 1 second
#define MAVLINK_UPDATE_MS          1000    // 1 Hz
```

Or use serial commands at runtime:
```
set_iridium_interval 300    # 5 minutes
set_meshtastic_interval 5   # 5 seconds
save
```

### Adjusting Failsafe Thresholds

Edit [config.h](include/config.h):
```cpp
#define BATTERY_CRITICAL_VOLTAGE       11.0   // Volts
#define FAILSAFE_HEARTBEAT_TIMEOUT_MS  30000  // 30 seconds
#define FAILSAFE_MAX_DEPTH_M           200.0  // Meters
#define MISSION_DEPTH_THRESHOLD_M      2.0    // SELF_TEST → MISSION
#define RECOVERY_DEPTH_THRESHOLD_M     3.0    // MISSION → RECOVERY
#define RELEASE_RELAY_DURATION_SEC     1500   // 25 minutes
```

### Adjusting LED Behavior

Modify [neopixel_controller.cpp](src/modules/neopixel_controller.cpp):
- Change colors in `config.h`
- Adjust animation speeds
- Create custom patterns

### Battery Thresholds

Edit [config.h](include/config.h):
```cpp
#define BATTERY_LOW_VOLTAGE      11.5  // Volts (warning)
#define BATTERY_CRITICAL_VOLTAGE 11.0  // Volts (failsafe trigger)
#define BATTERY_FULL_VOLTAGE     14.8  // 4S LiPo
```

## Contributing

Pull requests welcome! Please ensure:
1. Code follows existing style
2. Add comments for complex logic
3. Test on hardware before submitting
4. Update documentation

## License

See [LICENSE](LICENSE) file.

## Support

For issues, questions, or contributions:
- GitHub Issues: [Create an issue](https://github.com/yourusername/Doris-AGT-Firmware/issues)
- Documentation: [SparkFun AGT Docs](https://learn.sparkfun.com/tutorials/artemis-global-tracker-hookup-guide)

## Version History

See [CHANGELOG.md](CHANGELOG.md) for full version history.

## Acknowledgments

- SparkFun for the Artemis Global Tracker hardware
- Blue Robotics for the PSM and Navigator
- Meshtastic project for mesh networking
- ArduPilot project for MAVLink
