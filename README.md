# Doris-AGT-Firmware

**Oceanographic Drop Camera Communication System**

Advanced firmware for the SparkFun Artemis Global Tracker designed for autonomous deep-sea deployment with drop weight release and multi-channel communication.

## Mission Profile

This system is designed for **oceanographic drop camera** deployments:

1. **Deployment** - Released from boat, descends vertically through water column
2. **Seafloor Recording** - Records oceanographic data and video for 24 hours
3. **Ballast Release** - Automatically releases drop weight at programmed time
4. **Surface Recovery** - Ascends to surface and transmits position via Iridium/mesh
5. **Power Conservation** - Shuts down nonessential systems (Navigator/Pi, camera, lights) during extended surface wait

## State-Based Architecture

The firmware uses a **state machine** with **ArduPilot/Navigator as the primary decision maker**:

### System States

1. **PREDEPLOYMENT** - Surface configuration and system test
2. **MISSION** - Active deployment (ArduPilot leads control)
3. **RECOVERY** - Low power surface mode awaiting pickup
4. **EMERGENCY** - Failsafe mode (immediate drop weight release + shutdown)

### Control Philosophy

- **ArduPilot commands state transitions** via serial/MAVLink
- **AGT monitors sensors** and can autonomously trigger emergency
- **Drop weight release** automated by timer or commanded
- **Power management relay** controlled by state machine

## Overview

The AGT serves as the **communication and control hub**, handling:

- 🛰️ **Iridium Satellite Communication** - Global position reporting for recovery
- 📡 **Meshtastic Mesh Networking** - Local range communication via RAK4603
- 🎯 **GPS Tracking** - Continuous position monitoring
- ⚓ **Drop Weight Release** - Timed relay control for ballast (state-based)
- 🔋 **Battery Monitoring** - Optional (ArduPilot can handle battery decisions)
- 💡 **NeoPixel Status Display** - 30 LED visual health indicators
- ⚡ **Power Management Relay** - State-based shutdown of Navigator/Pi, camera, lights
- ⚙️ **BlueOS Configuration** - Configured via BlueOS extension on Navigator/Pi

## Quick Links

- **[State Machine Architecture](docs/STATE_MACHINE.md)** - **START HERE** - Control system overview
- **[Mission Profile](docs/MISSION_PROFILE.md)** - Detailed deployment phases
- **[BlueOS Integration](docs/BLUEOS_INTEGRATION.md)** - ArduPilot command interface
- **[Wiring Diagram](docs/WIRING_DIAGRAM.md)** - Hardware connections
- **[Full Documentation](README_FIRMWARE.md)** - Complete feature reference
- **[Quick Start Guide](docs/QUICK_START.md)** - Get up and running fast

## Features

### Communication Interfaces
- GPS position tracking with u-blox ZOE-M8Q
- Iridium 9603N satellite modem for global coverage
- Meshtastic RAK4603 for LoRa mesh networking
- MAVLink over USB for ArduPilot integration

### Hardware Control
- **Relay 1 (Power Management)**: Controls power to Navigator/Pi, camera, and lights
  - Triggered automatically when battery voltage drops below threshold
  - Conserves power during extended surface recovery wait
- **Relay 2 (Drop Weight Release)**: Triggers ballast release mechanism
  - Programmed via GMT time or delay from deployment
  - Configured through BlueOS extension
- **30 NeoPixel LED Indicators**: Visual status for recovery team
- **Battery Monitoring**: Real-time voltage/current via Blue Robotics PSM analog

### System Architecture

```
┌─────────────────────────────────────────────┐
│         Drop Camera System                   │
│                                              │
│  ┌──────────────┐         ┌──────────────┐  │
│  │ Navigator/Pi │◄──USB───┤     AGT      │  │
│  │  (BlueOS)    │         │ (Comms Hub)  │  │
│  └──────┬───────┘         └───┬──────┬───┘  │
│         │                     │      │       │
│    ┌────▼─────┐          ┌───▼──┐ ┌─▼─────┐ │
│    │  Camera  │          │ Mesh │ │Iridium│ │
│    │  Lights  │          │RAK603│ │ 9603N │ │
│    └──────────┘          └──────┘ └───────┘ │
│         ▲                                    │
│    Relay 1 (Power Mgmt)                      │
│                                              │
│    Relay 2 ──► Drop Weight Release          │
└─────────────────────────────────────────────┘
```

### Configuration via BlueOS

All mission parameters are configured through a **BlueOS extension** on the Navigator/Pi:
- Drop weight release time (GMT or delay)
- Iridium reporting intervals
- Meshtastic update rates
- Power save voltage threshold
- LED brightness and patterns

**The AGT firmware does NOT require direct serial configuration** - all settings are pushed from BlueOS.

## Hardware Requirements

### Core System
- SparkFun Artemis Global Tracker
- Blue Robotics Navigator + Raspberry Pi (BlueOS)
- Blue Robotics PSM (Power Sense Module - analog connection)
- Meshtastic RAK4603 (connected to AGT SPI header: GPIO6/GPIO7)
- WS2812B LED strip (30 LEDs, external 5V power required)

### Relays
- Relay 1: High-current relay for Navigator/Pi, camera, lights power
- Relay 2: Ballast release mechanism trigger

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

# Monitor serial output
pio device monitor -b 115200
```

## Quick Configuration

Connect via serial (115200 baud) and use these commands:

```
config                          # View current settings
set_iridium_interval 600        # Set Iridium to 10 minutes
set_meshtastic_interval 30      # Set Meshtastic to 30 seconds
set_timed_event gmt 1735689600 5000  # Set timed event
save                            # Save configuration
```

Type `help` for all commands.

## System Status

The NeoPixel LEDs indicate system state:

| Color | Pattern | Meaning |
|-------|---------|---------|
| Blue | Rainbow | Booting |
| Yellow | Pulse | GPS searching |
| Green | Pulse | GPS locked |
| Magenta | Chase | Iridium transmitting |
| Orange | Pulse | Low battery |
| Red | Blink | Error |

## Documentation

- [README_FIRMWARE.md](README_FIRMWARE.md) - Complete documentation
- [QUICK_START.md](docs/QUICK_START.md) - Quick start guide
- [WIRING_DIAGRAM.md](docs/WIRING_DIAGRAM.md) - Wiring instructions
- [CHANGELOG.md](CHANGELOG.md) - Project history and version changes

## License

See [LICENSE](LICENSE) file for details.

## Contributing

Contributions welcome! Please open an issue or submit a pull request.

## Acknowledgments

- SparkFun for the Artemis Global Tracker
- Blue Robotics for the Power Sense Module
- Meshtastic project
- ArduPilot project
