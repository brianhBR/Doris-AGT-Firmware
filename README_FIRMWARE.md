# Doris AGT Firmware

Comprehensive firmware for the SparkFun Artemis Global Tracker with multi-interface communication and control capabilities.

## Features

### Core Functionality
- **GPS Position Tracking** - u-blox ZOE-M8Q GNSS receiver
- **Iridium Satellite Communication** - Global position reporting via Iridium 9603N
- **Meshtastic Integration** - Serial interface to RAK4603 for mesh networking
- **MAVLink Interface** - GPS data forwarding to ArduPilot Navigator via USB
- **Battery Monitoring** - Blue Robotics PSM for voltage/current sensing
- **NeoPixel Status Display** - 30 LED status indicators with animations
- **Relay Control** - Dual relay system for power management and timed events

### Advanced Features
- Configurable reporting intervals for all communication channels
- Programmable timed event relay (GMT or delay-based triggering)
- Automatic power management based on battery voltage
- Serial configuration interface
- EEPROM-based persistent configuration
- RTC synchronization from GPS

## Hardware Requirements

### Main Board
- SparkFun Artemis Global Tracker (AGT)

### Connected Peripherals
1. **Meshtastic RAK4603** - Connected via Serial2
2. **ArduPilot Navigator** - Connected via USB (Serial)
3. **NeoPixel LED Strip** - 30 LEDs connected to GPIO32
4. **Blue Robotics PSM** - Connected via I2C (address 0x40)
5. **Relays** (2x):
   - Relay 1 (GPIO4): Power management for nonessential systems
   - Relay 2 (GPIO35): Programmable timed event trigger

### Pin Assignments

| Function | Pin | Description |
|----------|-----|-------------|
| NeoPixel Data | GPIO32 | WS2812 LED strip control |
| Relay 1 | GPIO4 | Power management relay |
| Relay 2 | GPIO35 | Timed event relay |
| Meshtastic TX | Serial2 TX | To RAK4603 RX |
| Meshtastic RX | Serial2 RX | From RAK4603 TX |
| MAVLink | USB Serial | To/from Navigator |
| PSM | I2C (SDA/SCL) | Battery monitoring |

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
- Adafruit NeoPixel
- ArduinoJson
- MAVLink C Library v2

### Building and Uploading

1. Connect your Artemis Global Tracker via USB
2. In PlatformIO:
   - Click "Build" to compile
   - Click "Upload" to flash firmware
   - Click "Monitor" to view serial output

Or use command line:
```bash
pio run -t upload
pio device monitor
```

## Configuration

### Serial Commands

Connect to the AGT via USB serial (115200 baud) and use these commands:

| Command | Description | Example |
|---------|-------------|---------|
| `help` | Show all commands | `help` |
| `config` | Display current configuration | `config` |
| `save` | Save configuration to EEPROM | `save` |
| `reset` | Reset to default configuration | `reset` |
| `set_iridium_interval <seconds>` | Set Iridium reporting interval | `set_iridium_interval 600` |
| `set_meshtastic_interval <seconds>` | Set Meshtastic update interval | `set_meshtastic_interval 30` |
| `enable_<feature>` | Enable a feature | `enable_mavlink` |
| `disable_<feature>` | Disable a feature | `disable_iridium` |
| `set_timed_event <gmt\|delay> <time> <duration>` | Configure timed relay event | See below |
| `set_power_save_voltage <volts>` | Set low voltage threshold | `set_power_save_voltage 11.5` |

### Configurable Features
- `iridium` - Iridium satellite communication
- `meshtastic` - Meshtastic mesh networking
- `mavlink` - MAVLink interface
- `psm` - Battery monitoring
- `neopixels` - LED status display

### Timed Event Configuration

The timed event relay can be triggered in two modes:

**GMT Mode** (absolute time):
```
set_timed_event gmt 1735689600 5000
```
- `gmt` - Use absolute GMT time
- `1735689600` - Unix timestamp (seconds since 1970)
- `5000` - Duration in milliseconds (5 seconds)

**Delay Mode** (relative to boot):
```
set_timed_event delay 3600 5000
```
- `delay` - Use time since boot
- `3600` - Seconds after boot (1 hour)
- `5000` - Duration in milliseconds (5 seconds)

After setting, don't forget to save:
```
save
```

### Default Configuration

```cpp
Iridium Interval: 600 seconds (10 minutes)
Meshtastic Interval: 30 seconds
MAVLink Interval: 1000 ms (1 Hz)
Power Save Voltage: 11.5V
All features: Enabled
Timed Event: Disabled
```

## Operation

### System States

The NeoPixel LEDs indicate the current system state:

| State | Color | Pattern | Description |
|-------|-------|---------|-------------|
| Boot | Blue | Rainbow | System initializing |
| GPS Search | Yellow | Pulse | Searching for GPS fix |
| GPS Fix | Green | Slow pulse | Valid GPS fix acquired |
| Iridium TX | Magenta | Chase | Transmitting via Iridium |
| Error | Red | Fast blink | System error |
| Low Battery | Orange | Pulse | Battery below threshold |
| Standby | Dim Cyan | Solid | Normal operation |

### Power Management

The firmware automatically manages power based on battery voltage:

1. **Normal Operation** (>11.5V)
   - All systems enabled
   - Relay 1 ON (nonessential systems powered)

2. **Low Battery** (11.0V - 11.5V)
   - Relay 1 OFF (nonessential systems disabled)
   - LED warning (orange)
   - Continue essential tracking

3. **Critical** (<11.0V)
   - Emergency mode
   - Consider emergency beacon transmission

### Data Flow

```
GPS (I2C) ─────┬────> Iridium (Satellite)
               ├────> Meshtastic (RAK4603 Serial)
               └────> MAVLink (Navigator USB)

PSM (I2C) ─────┬────> Power Management
               └────> Telemetry Reports

Configuration <────> EEPROM
Status ───────────> NeoPixels
```

## Iridium Messages

Position reports are sent in the following format:
```
LAT:37.422408,LON:-122.084108,ALT:15.2,SPD:2.5,SAT:12,BATT:12.45V,1.23A
```

Fields:
- LAT: Latitude (decimal degrees)
- LON: Longitude (decimal degrees)
- ALT: Altitude (meters MSL)
- SPD: Ground speed (m/s)
- SAT: Number of satellites
- BATT: Battery voltage and current

## MAVLink Integration

The AGT sends the following MAVLink messages to the Navigator:

1. **HEARTBEAT** - Every 1 second
   - System ID: 1
   - Component ID: MAV_COMP_ID_GPS

2. **GPS_RAW_INT** - At configured interval (default 1 Hz)
   - Position, altitude, speed, course
   - Fix type and satellite count

3. **BATTERY_STATUS** - With GPS updates
   - Voltage, current, state of charge

## Troubleshooting

### GPS Not Acquiring Fix
- Check antenna connection
- Ensure clear view of sky
- Wait 2-3 minutes for cold start
- Check serial output for satellite count

### Iridium Transmission Failures
- Verify supercapacitor is charged (PGOOD LED)
- Check signal quality (need >= 2)
- Ensure antenna has clear view of sky
- Verify Iridium account has credits

### Meshtastic Not Responding
- Check serial connections (TX/RX crossed)
- Verify baud rate (115200)
- Test RAK4603 with AT commands
- Check Serial2 pin configuration

### MAVLink Not Working
- Verify USB connection to Navigator
- Check baud rate (57600)
- Use MAVLink inspector to verify messages
- Check system/component IDs

### NeoPixels Not Working
- Verify data pin (GPIO32)
- Check power supply (5V, adequate current)
- Test with simple color command
- Verify LED count (30)

### PSM Not Detected
- Check I2C address (0x40)
- Verify I2C connections (SDA/SCL)
- Use I2C scanner to verify device
- Check PSM power supply

## File Structure

```
Doris-AGT-Firmware/
├── include/
│   ├── config.h                    # Main configuration
│   └── modules/
│       ├── gps_manager.h
│       ├── iridium_manager.h
│       ├── meshtastic_interface.h
│       ├── mavlink_interface.h
│       ├── neopixel_controller.h
│       ├── psm_interface.h
│       ├── relay_controller.h
│       └── config_manager.h
├── src/
│   ├── main.cpp                    # Main program
│   └── modules/
│       ├── gps_manager.cpp
│       ├── iridium_manager.cpp
│       ├── meshtastic_interface.cpp
│       ├── mavlink_interface.cpp
│       ├── neopixel_controller.cpp
│       ├── psm_interface.cpp
│       ├── relay_controller.cpp
│       └── config_manager.cpp
├── platformio.ini                  # PlatformIO configuration
└── README_FIRMWARE.md             # This file
```

## Customization

### Changing Update Intervals

Edit [config.h](include/config.h):
```cpp
#define IRIDIUM_SEND_INTERVAL_MS   600000  // 10 minutes
#define MESHTASTIC_UPDATE_MS       30000   // 30 seconds
#define MAVLINK_UPDATE_MS          1000    // 1 Hz
```

### Adjusting LED Behavior

Modify [neopixel_controller.cpp](src/modules/neopixel_controller.cpp):
- Change colors in `config.h`
- Adjust animation speeds
- Create custom patterns

### Battery Thresholds

Edit [config.h](include/config.h):
```cpp
#define BATTERY_LOW_VOLTAGE      11.5  // Volts
#define BATTERY_CRITICAL_VOLTAGE 11.0  // Volts
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

### v1.0.0 (Initial Release)
- GPS position tracking
- Iridium satellite communication
- Meshtastic mesh networking
- MAVLink interface
- NeoPixel status display
- Battery monitoring (PSM)
- Dual relay control
- Configuration system
- Timed event triggering

## Acknowledgments

- SparkFun for the Artemis Global Tracker hardware
- Blue Robotics for the PSM
- Meshtastic project for mesh networking
- ArduPilot project for MAVLink
