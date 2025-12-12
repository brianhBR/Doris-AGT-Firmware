# Changelog

All notable changes to the Doris AGT Firmware project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).

## [Unreleased]

### Added
- GPS enable pin configured as open-drain for proper MOSFET gate control
- GPS backup battery charging enabled for faster fixes
- Proper SparkFun AGT example procedures for GPS initialization

### Changed
- GPS sleep/wake functions now use proper open-drain pin configuration
- Updated to match SparkFun hardware design specifications

## [0.2.0] - 2024-12-11 - Iridium and Emergency State Improvements

### Added
- SparkFun's official Iridium 9603N initialization procedure
  - Supercapacitor charging with PGOOD monitoring
  - Battery voltage checks during charging
  - Top-up charge period for reliable operation
  - USB power profile for proper timing
- Emergency state now waits for drop weight release completion
- GPS fix OR timer completion for emergency→recovery transition

### Changed
- Drop weight relay timing updated for electrolytic release mechanism (20+ minute duration)
- Emergency state improvements for proper sequencing
- Iridium manager follows SparkFun AGT example patterns exactly

### Fixed
- Antenna switch safety maintained (GPS and Iridium never enabled simultaneously)
- Proper supercapacitor charging sequence prevents failed transmissions

## [0.1.0] - 2024-12-10 - Meshtastic Protobuf Protocol

### Added
- Nanopb dependency for protobuf encoding/decoding
- Meshtastic protobuf protocol implementation (Client API)
- Separate UART instances for Iridium and Meshtastic
  - Serial1 (UART1) for Iridium on D24/D25
  - MeshtasticSerial (UART0) for RAK4603 on D39/D40 (J10 connector)
- GPS positions now appear on Meshtastic maps
- Efficient binary encoding for mesh messages

### Changed
- Meshtastic interface updated from TEXT mode to PROTOBUF mode (PROTO)
- Serial port configuration uses dedicated hardware UART instances
- No more AT commands (never worked with Meshtastic)

### Fixed
- Proper message types (POSITION_APP for GPS, TEXT_MESSAGE_APP for alerts)
- Native Meshtastic integration with all apps
- 4-byte header packet framing for reliable communication

## [0.0.9] - 2024-12-10 - State Machine Architecture

### Added
- Complete state machine implementation (4 states)
  - PREDEPLOYMENT: Configuration and system test
  - MISSION: Active deployment (ArduPilot leads)
  - RECOVERY: Low power surface mode
  - EMERGENCY: Failsafe mode with immediate drop weight release
- ArduPilot/Navigator as primary decision maker
- State-based power management (not battery-based)
- Emergency sensor trigger framework
- Comprehensive state machine documentation
- Command interface for state transitions
  - `start_mission`, `enter_recovery`, `emergency`, `exit_emergency`, `reset`, `status`
- Drop weight control commands
  - `arm_drop gmt <time> <duration>`
  - `arm_drop delay <seconds> <duration>`
  - `release_now`

### Changed
- Battery monitoring (PSM) made optional
  - Can disable with `disable_psm`
  - ArduPilot handles battery decisions
  - AGT power decisions based on state, not battery voltage
- Relay 1 (Power Management) controlled by state machine
  - ON in PREDEPLOYMENT and MISSION
  - OFF in RECOVERY and EMERGENCY
- Relay 2 (Drop Weight) armed during MISSION, triggered at programmed time or emergency
- Main loop refactored around state machine
  - `StateMachine_update()` is highest priority
  - State guards enforce valid transitions
  - Automatic transitions (drop weight → RECOVERY)

### Removed
- Battery-based power management decisions
- `set_power_save_voltage` from relay control logic (still configurable for reference)

## [0.0.8] - 2024-12-09 - PSM and Serial Port Corrections

### Added
- Blue Robotics PSM analog interface
  - GPIO11 (AD11) for voltage sensing
  - GPIO12 (AD12) for current sensing
  - Proper calibration constants (11.0 V/V divider, 37.8788 A/V)
  - 14-bit ADC with 2.0V reference
- Meshtastic Serial2 on accessible pins
  - GPIO6 (MISO, SPI header) for TX
  - GPIO7 (MOSI, SPI header) for RX
  - Easy physical access for wiring

### Changed
- PSM interface updated from incorrect I2C to correct analog inputs
- Serial2 pins moved from inaccessible GPIO0/GPIO1 to GPIO6/GPIO7
- Updated wiring documentation for new pin assignments

### Fixed
- PSM now reads battery voltage and current correctly
- Meshtastic communication accessible via SPI header

### Removed
- Incorrect I2C-based PSM implementation
- References to inaccessible GPIO0/GPIO1 for Serial2

## [0.0.7] - 2024-12-09 - Initial Oceanographic Drop Camera Configuration

### Added
- Oceanographic drop camera mission profile
  - Deployment → Seafloor Recording → Ballast Release → Surface Recovery
- BlueOS integration documentation
- Mission profile documentation with deployment phases
- Wiring diagram documentation
- Example configurations for different scenarios
- Quick start guide
- Power budget calculations
- Deployment checklist
- Emergency procedures
- RTC synchronization from GPS
- NeoPixel visual status display (30 LEDs)
- MAVLink interface for ArduPilot integration

### Changed
- System architecture focused on drop camera deployment
- Relay functions clarified:
  - Relay 1: Navigator/Pi, Camera, Lights (power management)
  - Relay 2: Drop weight release (ballast)
- Configuration approach via BlueOS extension
- Documentation reflects state-based control philosophy

## [0.0.5] - 2024-12-08 - Initial Feature Set

### Added
- GPS position tracking with u-blox ZOE-M8Q
  - I2C communication at 400kHz
  - UBX protocol (binary, not NMEA)
  - 1Hz navigation frequency
  - Automatic NAV PVT messages
  - Minimum 4 satellites for fix
- Iridium 9603N satellite communication
  - Global position reporting
  - Text message support
  - Binary message support
  - Signal quality monitoring
  - Power management (sleep/wake)
- Meshtastic mesh networking (initial implementation)
- Dual relay controller
  - Relay 1 (GPIO4): Power management
  - Relay 2 (GPIO35): Timed events
- NeoPixel status display
  - 30 LED indicators
  - Multiple color patterns
  - State-based animations
- Configuration system
  - EEPROM persistence
  - Serial command interface
  - Feature enable/disable
  - Interval configuration
- Timed event system
  - GMT mode (absolute time)
  - Delay mode (relative to boot)
  - Configurable duration

### Dependencies
- SparkFun u-blox GNSS v3 ^3.0.0
- SparkFun IridiumSBD I2C Arduino Library v3.0.6
- Adafruit NeoPixel ^1.12.0
- MAVLink C Library v2
- ArduinoJson ^6.21.3
- Nanopb ^0.4.8 (for Meshtastic protobufs)

---

## Version History Summary

| Version | Date | Focus |
|---------|------|-------|
| Unreleased | 2024-12-12 | GPS hardware improvements |
| 0.2.0 | 2024-12-11 | Iridium and emergency improvements |
| 0.1.0 | 2024-12-10 | Meshtastic protobuf protocol |
| 0.0.9 | 2024-12-10 | State machine architecture |
| 0.0.8 | 2024-12-09 | PSM and serial corrections |
| 0.0.7 | 2024-12-09 | Oceanographic drop camera |
| 0.0.5 | 2024-12-08 | Initial feature set |

---

## Migration Notes

### From 0.1.0 to 0.2.0
- No breaking changes
- Iridium initialization improved (no code changes needed)
- Emergency state behavior enhanced

### From 0.0.9 to 0.1.0
- Meshtastic now uses PROTOBUF mode
- Update RAK4603: `meshtastic --set serial.mode PROTO`
- GPS positions now display on maps

### From 0.0.8 to 0.0.9
- **BREAKING**: State machine commands required
- Battery monitoring optional (`disable_psm` if not using)
- Use `start_mission` instead of direct operation
- Power management now state-based, not voltage-based
- Update command scripts to use new state commands

### From 0.0.7 to 0.0.8
- **BREAKING**: PSM wiring changed (I2C → Analog)
  - Rewire to GPIO11 (voltage) and GPIO12 (current)
- **BREAKING**: Meshtastic wiring changed
  - Move from GPIO0/1 to GPIO6/7 (SPI header)

---

## Archived Change Documents

The following documents have been consolidated into this CHANGELOG and moved to the `archive/` folder:
- [`archive/CHANGES_SUMMARY.md`](archive/CHANGES_SUMMARY.md) - Initial PSM, Serial, and Meshtastic changes
- [`archive/MESHTASTIC_UPDATE.md`](archive/MESHTASTIC_UPDATE.md) - TEXT mode implementation
- [`archive/MESHTASTIC_PROTOBUF_UPDATE.md`](archive/MESHTASTIC_PROTOBUF_UPDATE.md) - PROTOBUF mode implementation
- [`archive/SERIAL_PORT_CONFIGURATION.md`](archive/SERIAL_PORT_CONFIGURATION.md) - Separate UART instances
- [`archive/STATE_MACHINE_REFACTOR.md`](archive/STATE_MACHINE_REFACTOR.md) - State machine architecture

These files remain in the repository for historical reference but are no longer actively updated.

---

## Contributing

When making changes:
1. Update this CHANGELOG under "Unreleased"
2. Use categories: Added, Changed, Deprecated, Removed, Fixed, Security
3. Include migration notes for breaking changes
4. Update version number and date when releasing
