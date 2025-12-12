# Changes Summary - Oceanographic Drop Camera Configuration

> **⚠️ ARCHIVED**: This document has been consolidated into [CHANGELOG.md](CHANGELOG.md).
> Please refer to the CHANGELOG for current project history and changes.

## Overview
Updated firmware and documentation to reflect the oceanographic drop camera deployment scenario and corrected hardware interface specifications.

## Key Changes

### 1. PSM Interface Correction ✅
**Issue:** Original implementation incorrectly used I2C for Blue Robotics PSM
**Resolution:** Updated to use analog inputs as per PSM specifications

**Changes Made:**
- **File**: `src/modules/psm_interface.cpp`
  - Removed I2C communication code
  - Implemented analog ADC reading on GPIO11 (voltage) and GPIO12 (current)
  - Added Blue Robotics calibration constants:
    - Voltage: 11.0 V/V divider
    - Current: 37.8788 A/V with 0.330V offset
  - Configured Artemis 14-bit ADC with 2.0V reference

- **File**: `include/config.h`
  - Added `PSM_VOLTAGE_PIN` (GPIO11/AD11)
  - Added `PSM_CURRENT_PIN` (GPIO12/AD12)

### 2. Serial2 Pin Configuration ✅
**Issue:** GPIO0/GPIO1 initially recommended were not broken out to accessible headers
**Resolution:** Changed to use SPI header breakout pins

**Changes Made:**
- **Meshtastic Serial2 Pins:**
  - TX2: GPIO6 (MISO header pin)
  - RX2: GPIO7 (MOSI header pin)

- **File**: `include/config.h`
  - Updated `MESHTASTIC_TX_PIN` to GPIO6
  - Updated `MESHTASTIC_RX_PIN` to GPIO7
  - Added comments noting these are on the SPI header

- **File**: `src/modules/meshtastic_interface.cpp`
  - Updated initialization comments to reflect GPIO6/7 usage

**Benefits:**
- Easy physical access via SPI header
- Adjacent pins for simpler wiring
- No conflicts with other systems

### 3. Meshtastic Protocol Correction ✅
**Issue:** Original implementation incorrectly used AT commands for Meshtastic
**Resolution:** Updated to use TEXT mode (TEXTMSG) protocol

**Changes Made:**
- **File**: `src/modules/meshtastic_interface.cpp`
  - Removed all AT command code (`AT+SEND=`, `AT+RECV`, etc.)
  - Implemented simple text-based protocol using `println()`
  - Added new functions: `sendState()`, `sendAlert()`
  - Updated message handling for incoming mesh messages
  - Added detailed configuration instructions in comments

- **File**: `include/modules/meshtastic_interface.h`
  - Added protocol documentation in header
  - Added function declarations for new functions
  - Documented message format standards

- **New File**: `MESHTASTIC_UPDATE.md`
  - Complete protocol explanation
  - RAK4603 configuration instructions
  - Message format standards
  - Testing procedures

**RAK4603 Configuration Required:**
RAK4603 must be pre-configured via Meshtastic CLI:
```bash
meshtastic --set serial.mode TEXTMSG
meshtastic --set serial.enabled true
meshtastic --set serial.baud BAUD_115200
meshtastic --commit
```

**Message Formats:**
- Position: `POS:<lat>,<lon>,<alt>m,<sats>sat`
- State: `STATE:<state>,<time>s`
- Telemetry: `TELEM:V=<volts>V,I=<amps>A`
- Alert: `ALERT:<message>`

**Benefits:**
- Correct protocol (no more AT commands)
- Simple implementation (no protobuf needed)
- Human-readable messages
- Easy debugging
- Compatible with all Meshtastic devices

### 4. Documentation Updates ✅

#### README.md
- Updated to reflect **oceanographic drop camera** application
- Added mission profile overview (deployment → seafloor → release → recovery)
- Emphasized BlueOS configuration approach
- Updated system architecture diagram
- Clarified relay functions:
  - Relay 1: Navigator/Pi, camera, lights (power management)
  - Relay 2: Drop weight release (ballast)

#### New Documents Created:

**`docs/BLUEOS_INTEGRATION.md`** - Comprehensive BlueOS extension guide
- Communication protocol with AGT
- Command reference
- Python example code for BlueOS extension
- Web UI design suggestions
- MAVLink integration details
- Pre-deployment testing framework

**`docs/MISSION_PROFILE.md`** - Detailed mission phases
- Phase 1: Pre-Deployment (surface configuration)
- Phase 2: Deployment (descent through water column)
- Phase 3: Seafloor Recording (24-hour mission)
- Phase 4: Ballast Release (autonomous surfacing)
- Phase 5: Ascent (return to surface)
- Phase 6: Surface Recovery Wait (power conservation)
- Phase 7: Recovery (mission complete)
- Configuration recommendations for different scenarios
- Power budget calculations
- Deployment checklist
- Emergency procedures

**`docs/WIRING_DIAGRAM.md`** - Updated wiring instructions
- Corrected PSM analog connections (GPIO11/12)
- Updated Meshtastic connections (GPIO6/7 on SPI header)
- Removed I2C references for PSM
- Updated relay descriptions for drop camera application

**`docs/MESHTASTIC_PROTOCOL.md`** - Meshtastic communication protocol
- Explains correct Protocol Buffer-based protocol
- Documents why AT commands don't work
- Recommends TEXT mode (TEXTMSG) as simplest solution
- Provides RAK4603 configuration instructions
- Message format recommendations and examples

**`MESHTASTIC_UPDATE.md`** - Summary of Meshtastic changes
- Problem description (AT commands incorrect)
- Solution implementation (TEXT mode)
- Code changes summary
- Testing procedures
- Message format standards

## Pin Allocation Summary

| Function | GPIO | Header/Connection | Type |
|----------|------|-------------------|------|
| PSM Voltage | GPIO11 (AD11) | Analog pin | Analog Input |
| PSM Current | GPIO12 (AD12) | Analog pin | Analog Input |
| Meshtastic TX | GPIO6 | MISO (SPI header) | Serial TX |
| Meshtastic RX | GPIO7 | MOSI (SPI header) | Serial RX |
| NeoPixel Data | GPIO32 (AD32) | GPIO header | Digital Out |
| Relay 1 (Power Mgmt) | GPIO4 | SPI CS2 header | Digital Out |
| Relay 2 (Drop Weight) | GPIO35 | SPI CS1 header | Digital Out |

## Configuration Context

### Original Generic Tracker
- Configured via serial commands on AGT directly
- Generic "power management" and "timed event" terminology
- I2C-based PSM (incorrect)

### Updated Oceanographic Drop Camera
- **Configured via BlueOS extension** on Navigator/Pi
- Specific mission terminology:
  - "Navigator/Pi, camera, lights" (not "nonessentials")
  - "Drop weight release" (not "timed event")
- **Analog-based PSM** (correct)
- Mission-specific power management for extended surface wait

## Testing Recommendations

### Before Deployment:
1. **PSM Analog Test**: Verify voltage and current readings with known load
   - Expected voltage range: 11-16.8V (4S LiPo)
   - Current should read correctly with ADC calibration

2. **Serial2 Test**: Verify Meshtastic communication on GPIO6/7
   - RAK4603 must be in TEXTMSG mode first
   - Send simple text messages to RAK4603
   - Verify messages appear in Meshtastic app
   - Check on SPI header with multimeter/scope if needed

3. **Relay Test**: Both relays should operate correctly
   - Relay 1: High-current load (Navigator/Pi, camera, lights)
   - Relay 2: Drop weight release mechanism

4. **Full Mission Simulation**: Run on bench with BlueOS
   - Set short delay (e.g., 60 seconds) for drop weight test
   - Monitor all systems
   - Verify power save mode triggers at threshold voltage

## Migration Notes

If you have existing code or configurations:

### PSM Interface
**Old** (incorrect):
```cpp
Wire.beginTransmission(PSM_I2C_ADDRESS);
Wire.requestFrom(PSM_I2C_ADDRESS, 2);
```

**New** (correct):
```cpp
uint16_t voltageADC = analogRead(PSM_VOLTAGE_PIN);  // GPIO11
uint16_t currentADC = analogRead(PSM_CURRENT_PIN);  // GPIO12
float voltage = (voltageADC / 16383.0) * 2.0 * 11.0;  // Apply calibration
```

### Meshtastic Pins
**Old**: GPIO0/GPIO1 (not accessible)
**New**: GPIO6/GPIO7 (SPI header, easily accessible)

### Meshtastic Protocol
**Old**: AT command-based (incorrect)
```cpp
MESHTASTIC_SERIAL.print("AT+SEND=");
MESHTASTIC_SERIAL.println(message);
```

**New**: TEXT mode (correct)
```cpp
// Simply send text - RAK4603 handles mesh protocol
MESHTASTIC_SERIAL.println("POS:37.422408,-122.084108,15.2m,12sat");
```

## Known Limitations

1. **Apollo3 Serial2 Pin Configuration**:
   - The Artemis/Apollo3 core may require variant file updates to map Serial2 to GPIO6/7
   - Default Serial2 pins may differ between Artemis module variants
   - May need to verify with SparkFun Apollo3 core documentation

2. **MAVLink Note**:
   - Current implementation assumes USB Serial for MAVLink
   - Navigator should be able to parse both MAVLink messages and configuration commands
   - BlueOS extension needs to multiplex/demultiplex traffic

3. **RTC Synchronization**:
   - AGT RTC depends on GPS fix for accurate time
   - Must acquire GPS fix before deployment for GMT-based drop weight release
   - Backup: Use delay-based release if GPS sync uncertain

## Future Enhancements

Potential improvements for future versions:

1. **BlueOS Extension Development**:
   - Complete BlueOS extension implementation
   - Web UI for mission configuration
   - Real-time status monitoring
   - Mission log export

2. **Enhanced Telemetry**:
   - Add mission phase detection
   - Depth estimation from pressure sensor
   - Battery state-of-charge algorithms
   - Predictive surface wait time

3. **Safety Features**:
   - Backup release timer (hardware watchdog)
   - Emergency abort commands
   - Redundant position reporting
   - Battery critical warnings earlier in mission

4. **Data Logging**:
   - SD card logging of all events
   - GPS track recording
   - Battery history
   - System state changes

## References

- Blue Robotics PSM: https://bluerobotics.com/store/comm-control-power/control/psm-asm-r2-rp/
- AGT Hardware: `SparkFun_Artemis_Global_Tracker/Documentation/Hardware_Overview/`
- AGT Pin Definitions: `SparkFun_Artemis_Global_Tracker/Documentation/Hardware_Overview/ARTEMIS_PINS.md`

## Questions or Issues?

- PSM readings incorrect? Check ADC calibration constants
- Meshtastic not communicating? Verify GPIO6/7 connections on SPI header
- Drop weight not releasing? Test RTC synchronization and trigger time
- Power save not activating? Check battery voltage threshold setting

See full documentation in `README_FIRMWARE.md` and `docs/` directory.
