# Changelog

All notable changes to the Doris AGT Firmware project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).

## [Unreleased]

### Fixed
- GNSS time can no longer move the AGT RTC to an implausible future year or
  apply a correction larger than five minutes after synchronization. RTC
  discipline now processes each new UBX-NAV-PVT message once, validates real
  calendar dates, verifies hardware readback, and reports rejected updates over
  MAVLink instead of propagating them into BlueOS and ArduPilot log timestamps.

## [0.3.5] - 2026-08-12

### Added
- DORIS ASCII Iridium protocol B with fixed signed coordinates, speed, course,
  maximum mission depth, battery voltage, minimum pressure-sensor temperature,
  and a reserved `00` status byte. No-fix reports use zero navigation fields.
- Ingestion of Lua `MIN_TEMP` telemetry, including validity checks and
  per-mission minimum tracking.

### Changed
- Automatic recovery Iridium sessions now start only after the acknowledged
  BlueOS shutdown handshake, 30-second grace, and physical payload cutoff.
  This prevents a long synchronous satellite attempt from delaying clean
  shutdown; explicit operator tests remain available before cutoff.

## [0.3.4] - 2026-08-12

### Added
- AssistNow Autonomous (AOP) on the ZOE-M8Q. The receiver now predicts
  satellite orbits on-chip (up to ~3 days ahead on M8) from ephemeris it has
  already downloaded — no network, almanac upload, or server needed. Predictions
  persist in BBR kept alive by the V_BCKP coin cell, so a surfacing after a long
  dive (broadcast ephemeris long expired) can get an AOP-assisted start instead
  of a full cold acquisition. Enabled via `UBX-CFG-NAVX5`, which — unlike
  `enableGNSS` — does not reset the receiver or wipe ephemeris. The enable path
  is idempotent and runs outside the `alreadyConfigured` guard, so units whose
  BBR predates this feature also converge on next boot. Gated by
  `GPS_ENABLE_AOP` with `GPS_AOP_ORBIT_MAX_ERR` for the orbit-error bound.
- AOP status in the `AGT_DEBUG` GPS diagnostics: reports whether AOP is enabled
  (`use=ON/OFF`) and the live subsystem state (`idle(ready)` vs `generating`),
  so an operator can confirm predictions are computed and stored in BBR during
  the pre-deployment warm-up before splashdown.

### Changed
- GPS navigation rate lowered from 6 Hz to 1 Hz (`GPS_UPDATE_RATE_HZ`) so the
  receiver has spare CPU to generate AOP orbit predictions. 1 Hz is ample for a
  surface position tracker; MAVLink, Meshtastic, and Iridium all consume
  position far more slowly.

## [0.3.3] - 2026-08-12

### Changed
- Lua's repeated fresh terminal `STATE=4` is now the sole payload-shutdown
  authority. After an observed dive, three recovery reports start a
  `SURFACE_LOGGING_DWELL_MS` three-minute powered logging window; depth/GPS and
  the independent recovery backstop cannot vote for cutoff.
- A valid BlueOS `PWR_ACK` now latches the 30-second final electrical grace, so
  expected MAVLink loss during Linux shutdown cannot cancel cutoff. Stale or
  reverted Lua state still resets the dwell before ACK, and every AGT reboot
  restores payload power.

## [0.3.2] - 2026-08-12

### Fixed
- GPS calendar time is now passed to `Apollo3RTC::setTime` in the API's actual
  order (`hundredths, seconds, minutes, hours, day, month, year-since-2000`).
  The old call supplied `hours, minutes, seconds, 0, day, month, full-year`,
  which turned a real time such as `2026-08-12 18:06:45` into
  `2074-08-12 00:45:06`. The resulting `SYSTEM_TIME` moved the vehicle clock,
  fragmented recorder and autopilot logs, and caused post-dive processing to
  select an unrelated MCAP. The RTC is now read back and must match before its
  time is published, so a future mapping failure suppresses `SYSTEM_TIME`
  instead of poisoning the vehicle clock.

## [0.3.1] - 2026-07-31

### Fixed
- Named-float names no longer trail bytes borrowed from the string literal next
  to them. MAVLink's `NAMED_VALUE_FLOAT` name is a fixed `char[10]` and the
  generated packer copies all ten bytes out of whatever pointer it is handed, so
  passing a shorter literal read past the end of that literal and put whatever
  the linker had placed after it on the wire. Captured off the vehicle, the AGT
  was transmitting `AGT_CAP\0RE`, `REL_STAT\0P` and `PWR_SHDN\0v` — the trailing
  characters are fragments of the neighbouring names. A receiver that stops at
  the terminator reads these correctly, but BlueOS deletes NULs instead and so
  saw `AGT_CAPRE`, `REL_STATP` and `PWR_SHDNv`, which match nothing and were
  discarded. The extension consequently reported no AGT capability
  advertisement, no release status, and the AGT release path unavailable, while
  the AGT had in fact been announcing all three once a second since 0.3.0; the
  power-cutoff handshake could never have completed. Names now pass through
  `mavlinkNameField()`, which copies into a zero-filled field. `STATUSTEXT` was
  already staged in a padded buffer and was never affected, which is why log
  messages always looked right.

## [0.3.0] - 2026-07-28 - Safe Surface Power Control

### Added
- v0.3 safe surface power control: repeated/fresh recovery state, fresh shallow
  depth that is also moving, sustained qualification, and BlueOS ACK + final
  grace
- Unlocated Iridium reporting so surfacing is visible without a fix. The first
  `SURFACED,NOFIX,...` report goes out 2 minutes after entering `RECOVERY` and
  repeats every 30 minutes, carrying elapsed time, voltage, leak, and max depth
  but no position. A fix upgrades it to the full located report immediately.
  The repeat interval is long on purpose: Iridium and GPS share one antenna, so
  each session interrupts the acquisition being waited on
- MAVLink named-float safety protocol (`RELAY`, `REL_STAT`, `PWR_SHDN`,
  `PWR_ACK`) with strict source/value validation
- Repeated `AGT_CAP` capability bitmask for BlueOS compatibility gating
- Latched GPIO35 release controller with EEPROM active-state persistence,
  guarded explicit OFF, manual/Iridium paths, and DIVING-only sensor failsafes.
  The output mirrors the Navigator relay that Lua still drives from the same
  request, so the actuator may be wired to either controller — but only one.
  Power cutoff via Relay 1 is only valid when the actuator is wired to the AGT,
  because it takes the Navigator output offline with the Pi
- Native tests for mission-data freshness, surface-power qualification/ACK, and
  release latch/hold/persistence behavior
- `AGT_DEBUG` MAVLink command (`MAV_CMD_USER_3`, 31012) that dumps firmware
  version, RockBLOCK IMEI, and GPS diagnostics as STATUSTEXT
- RockBLOCK IMEI reported over MAVLink; cached on first Iridium modem power-up
- GPS enable pin configured as open-drain for proper MOSFET gate control
- GPS backup battery charging enabled for faster fixes
- Proper SparkFun AGT example procedures for GPS initialization
- Simplified state machine: PRE_MISSION → SELF_TEST → MISSION → RECOVERY
  - Depth-based automatic transitions from MAVLink sensor data
  - SELF_TEST → MISSION when depth > 2m
  - MISSION → RECOVERY when depth < 3m or GPS fix
- Failsafe system (replaces EMERGENCY state)
  - Low voltage, leak, max depth, no heartbeat, manual triggers
  - Fires release relay and enters RECOVERY on any trigger
- MissionData module for real-time data from autopilot via MAVLink
  - Depth from SCALED_PRESSURE and VFR_HUD
  - Battery voltage from SYS_STATUS and BATTERY_STATUS
  - Heartbeat watchdog and leak detection
- MAVLink SYSTEM_TIME forwarding (RTC synced from GPS, for ArduSub BRD_RTC_TYPES=2)
- Meshtastic NMEA 0183 GPS output via SoftwareSerial on J10 (D39/D40)
  - Sends GPGGA and GPRMC sentences at configurable interval
  - RAK4603 uses AGT as external GPS source
- SoftwareSerial library for Apollo3 (third serial channel)
- Self-test PlatformIO environment (`pio run -e selftest`)
- Serial commands: `start_self_test`, `debug`, `set_leak`, `mesh_test`, `mesh_test_gps`, `mesh_send`
- Recovery strobe LED pattern for visual location aid

### Changed
- Surface detection no longer requires a GPS fix, in either the power cutoff or
  the AGT's independent backstop. Measured over four dives, acquisition after
  surfacing took 17 s, 6.8 min, 30.4 min, and 38.7 min, and the mission ended
  within a second of the fix every time, so a fix was the sole thing holding up
  the end of the dive. Depth is now the primary evidence
- Both surface tests require the depth reading to have moved by at least
  `SURFACE_DEPTH_LIVENESS_M` (0.02 m) across their window. A frozen channel
  reads shallow and perfectly steady, which is indistinguishable from floating,
  and with the GPS term gone this is what keeps a dead sensor from being enough
  on its own. Measured surface windows spanned at least 0.070 m
- The independent backstop now needs sustained shallow depth for
  `SURFACE_QUALIFY_MS` rather than a single shallow reading plus a fix
- Entering `RECOVERY` no longer cuts Pi power; missing/stale inputs or missing
  BlueOS ACK always keep power on, and boot/reset restores power
- Power cutoff requires a live recovery signal from the autopilot and is never
  persisted. A power cycle restores payload power, and because the proof of a
  dive is a RAM-only depth high-water mark, the vehicle must dive and reach
  recovery again before a second cutoff is possible. On deck this means the
  operator can power cycle, download data, and configure a new mission without
  the AGT shutting the Pi down again
- `RELAY` command acknowledgements are only sent when the outcome changes; Lua
  republishes the request at 2 Hz for the whole mission, which previously
  produced a continuous STATUSTEXT stream on the link
- BlueOS post-ACK shutdown grace increased to 30 seconds so
  `systemctl poweroff` can complete before physical cutoff
- Release control is independent from power shutdown and no longer
  automatically turns off at the old 1500-second timeout; 1500 s is now the
  minimum OFF hold while the Lua-compatible requested hold is 7200 s
- Autopilot mission/depth/voltage/release MAVLink inputs now require source 1/1
- Replaced the unsupported `VERSION` command (`MAV_CMD_USER_6`, 31015 — MAVLink only defines USER_1..5) by folding version/IMEI reporting into `AGT_DEBUG`
- Serial `gps_diag` command renamed to `debug` (now also prints version + IMEI)
- State machine redesigned from 4 states (PREDEPLOYMENT/MISSION/RECOVERY/EMERGENCY) to 4 new states (PRE_MISSION/SELF_TEST/MISSION/RECOVERY)
- GPS sleep/wake functions now use proper open-drain pin configuration
- Updated to match SparkFun hardware design specifications
- Meshtastic interface changed from protobuf (PROTO mode) to NMEA GPS output
  - Baud changed from 115200 to 9600
  - Uses SoftwareSerial instead of hardware UART0
  - RAK4603 connected via J10 Qwiic connector (not SPI header)
- USB serial baud changed from 115200 to 57600 (shared debug + MAVLink)
- MAVLink component ID changed from MAV_COMP_ID_GPS to MAV_COMP_ID_ONBOARD_COMPUTER2 (192, avoids BlueOS/MAVLink server conflict on 191)
- MAVLink type changed from GPS to MAV_TYPE_ONBOARD_CONTROLLER
- PSM disabled by default (MbedOS mutex issues with analog reads)
- Default Meshtastic interval changed from 30s to 3s
- Relay 2 duration in seconds (not milliseconds), default 1500s for electrolytic release
- Relay wiring clarified: both use NO (Normally Open), active HIGH
- Power management is state-based (RECOVERY → Relay 1 OFF), not voltage-based
- All documentation updated to reflect current firmware architecture

### Removed
- EMERGENCY state (replaced by failsafe system within MISSION)
- `start_mission`, `enter_recovery`, `emergency`, `exit_emergency` commands
- `arm_drop` commands (replaced by `set_timed_event` + failsafe)
- Protobuf-based Meshtastic communication
- Battery-voltage-based power management decisions (now state-based)

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
