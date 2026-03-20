# Doris-AGT-Firmware

**Oceanographic Drop Camera Communication System**

Advanced firmware for the SparkFun Artemis Global Tracker designed for autonomous deep-sea deployment with drop weight release and multi-channel communication.

## Mission Profile

This system is designed for **oceanographic drop camera** deployments:

1. **Pre-Mission** - Surface configuration, system test, GPS fix
2. **Deployment** - Self-test verified, submerges (depth > 2m triggers mission)
3. **Seafloor Recording** - Navigator/Pi records video; AGT monitors failsafe conditions
4. **Surface Recovery** - Depth < 3m or GPS fix triggers recovery mode
5. **Power Conservation** - Shuts down nonessential systems (Navigator/Pi, camera, lights) in recovery

## State-Based Architecture

The firmware uses a **state machine** with **ArduPilot/Navigator providing real-time sensor data** via MAVLink:

### System States

1. **PRE_MISSION** - Initial setup, waiting for operator
2. **SELF_TEST** - System verification; Iridium can transmit for position check
3. **MISSION** - Active underwater deployment; failsafe monitoring active
4. **RECOVERY** - Low power surface mode with strobe LEDs and Iridium position reports

### Control Philosophy

- **Depth-based state transitions** — SELF_TEST → MISSION on depth > 2m, MISSION → RECOVERY on depth < 3m or GPS fix
- **Failsafe system** monitors battery voltage, leak detection, max depth, and autopilot heartbeat
- **Release relay** fired automatically on failsafe (1500s / 25 min for electrolytic release)
- **Power management relay** shuts down Navigator/Pi/camera/lights in RECOVERY

## Overview

The AGT serves as the **communication and control hub**, handling:

- **Iridium Satellite Communication** - Global position reporting with mission stats
- **Meshtastic Mesh Networking** - NMEA GPS output to RAK4603 via J10
- **GPS Tracking** - Continuous position monitoring (u-blox ZOE-M8Q)
- **MAVLink Interface** - GPS + battery + system time to ArduPilot over USB
- **Failsafe Release** - Relay control for electrolytic drop weight release
- **NeoPixel Status Display** - 30 LED visual indicators with recovery strobe
- **Power Management Relay** - State-based shutdown of nonessentials
- **BlueOS Configuration** - Configured via BlueOS extension on Navigator/Pi

## Quick Links

- **[Pinout Guide](docs/PINOUT_GUIDE.md)** - **START HERE** - Visual connection guide with board images
- **[State Machine Architecture](docs/STATE_MACHINE.md)** - Control system overview
- **[Mission Profile](docs/MISSION_PROFILE.md)** - Detailed deployment phases
- **[BlueOS Integration](docs/BLUEOS_INTEGRATION.md)** - ArduPilot command interface
- **[Wiring Diagram](docs/WIRING_DIAGRAM.md)** - Hardware connections
- **[Full Documentation](README_FIRMWARE.md)** - Complete feature reference
- **[Quick Start Guide](docs/QUICK_START.md)** - Get up and running fast

## Features

### Communication Interfaces
- GPS position tracking with u-blox ZOE-M8Q
- Iridium 9603N satellite modem for global coverage
- Meshtastic RAK4603 for LoRa mesh networking (NMEA GPS feed via SoftwareSerial)
- MAVLink over USB for ArduPilot integration (GPS, battery, system time)

### Hardware Control
- **Relay 1 (Power Management)**: Controls power to Navigator/Pi, camera, and lights
  - ON in PRE_MISSION, SELF_TEST, MISSION
  - OFF in RECOVERY (conserves power for extended surface wait)
- **Relay 2 (Release)**: Electrolytic drop weight release mechanism
  - Triggered by failsafe conditions or manual `release_now` command
  - Default duration: 1500 seconds (25 minutes) for electrolytic dissolution
- **30 NeoPixel LED Indicators**: Visual status with recovery strobe mode
- **Battery Monitoring**: Real-time voltage/current via Blue Robotics PSM analog (GPIO11/12)

### System Architecture

```
┌─────────────────────────────────────────────┐
│         Drop Camera System                   │
│                                              │
│  ┌──────────────┐         ┌──────────────┐  │
│  │ Navigator/Pi │◄──USB───┤     AGT      │  │
│  │  (BlueOS)    │ MAVLink │ (Comms Hub)  │  │
│  └──────┬───────┘  57600  └───┬──────┬───┘  │
│         │                     │      │       │
│    ┌────▼─────┐     ┌───────▼┐  ┌──▼─────┐ │
│    │  Camera  │     │  Mesh  │  │Iridium │ │
│    │  Lights  │     │RAK4603 │  │ 9603N  │ │
│    └──────────┘     │(J10    │  └────────┘ │
│         ▲           │ NMEA)  │              │
│    Relay 1          └────────┘              │
│    (Power Mgmt)                             │
│                                              │
│    Relay 2 ──► Electrolytic Release         │
└─────────────────────────────────────────────┘
```

### Configuration via BlueOS

All mission parameters are configured through a **BlueOS extension** on the Navigator/Pi:
- Iridium reporting intervals
- Meshtastic update rates
- Failsafe thresholds (configured in config.h)
- LED brightness and patterns

**The AGT firmware does NOT require direct serial configuration** - all settings can be pushed from BlueOS.

## Hardware Requirements

### Core System
- SparkFun Artemis Global Tracker
- Blue Robotics Navigator + Raspberry Pi (BlueOS)
- Blue Robotics PSM (Power Sense Module - analog on GPIO11/GPIO12)
- Meshtastic RAK4603 (connected to AGT J10 Qwiic: D39/D40, NMEA at 9600 baud)
- WS2812B LED strip (30 LEDs, external 5V power required)

### Relays
- Relay 1: High-current relay for Navigator/Pi, camera, lights power
- Relay 2: Electrolytic release mechanism (rated for extended activation, battery-powered coil)

### Battery
- 4S LiPo or suitable deep-cycle marine battery
- Sized for: 24hr seafloor operation + multi-day surface wait

See [Wiring Diagram](docs/WIRING_DIAGRAM.md) for complete connection details.

## Installation

### Prerequisites
- [PlatformIO](https://platformio.org/) or [Arduino IDE](https://www.arduino.cc/en/software)
- SparkFun Artemis board definitions

### Build and Upload

```bash
# Clone the repository
git clone https://github.com/yourusername/Doris-AGT-Firmware.git
cd Doris-AGT-Firmware

# Initialize submodules
git submodule update --init --recursive

# Build and upload
pio run -t upload

# Monitor serial output (57600 baud)
pio device monitor -b 57600
```

## Quick Configuration

Connect via serial (57600 baud) and use these commands:

```
help                                # Show all commands
config                              # View current settings
set_iridium_interval 600            # Set Iridium to 10 minutes
set_meshtastic_interval 3           # Set Meshtastic to 3 seconds
set_timed_event gmt 1735689600 1500 # Set timed event (seconds duration)
save                                # Save configuration
```

## System Status

The NeoPixel LEDs indicate system state:

| State | Pattern | Meaning |
|-------|---------|---------|
| PRE_MISSION | Pre-mission pattern | Waiting for operator |
| SELF_TEST | Self-test pattern | System verification |
| MISSION (fix) | Green pulse | GPS locked, operational |
| MISSION (no fix) | Yellow pulse | No GPS (expected underwater) |
| RECOVERY | Strobe | Flashing for visual recovery aid |

## Documentation

- **[PINOUT_GUIDE.md](docs/PINOUT_GUIDE.md)** - Visual pinout guide with board images
- [README_FIRMWARE.md](README_FIRMWARE.md) - Complete documentation
- [STATE_MACHINE.md](docs/STATE_MACHINE.md) - State machine architecture
- [MESHTASTIC_PROTOCOL.md](docs/MESHTASTIC_PROTOCOL.md) - Meshtastic NMEA interface
- [QUICK_START.md](docs/QUICK_START.md) - Quick start guide
- [WIRING_DIAGRAM.md](docs/WIRING_DIAGRAM.md) - Wiring instructions
- [CHANGELOG.md](CHANGELOG.md) - Project history and version changes

## License

See [LICENSE](LICENSE) file for details.

## Contributing

Contributions welcome! Please open an issue or submit a pull request.

## Acknowledgments

- SparkFun for the Artemis Global Tracker
- Blue Robotics for the Power Sense Module and Navigator
- Meshtastic project
- ArduPilot project
