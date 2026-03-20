# Quick Start Guide

## 1. Hardware Setup

### Connections Required

1. **Meshtastic RAK4603**
   - AGT D39 (J10 pin 1, TX) → RAK J10 RX (external GPS UART)
   - AGT D40 (J10 pin 2, RX) → RAK J10 TX (optional)
   - 3.3V from J10 pin 3 → RAK VCC
   - GND from J10 pin 4 → RAK GND

2. **ArduPilot Navigator**
   - USB cable: AGT ↔ Navigator
   - Uses native USB serial at 57600 baud

3. **NeoPixel LED Strip**
   - Data pin → GPIO32
   - +5V → External power supply (not AGT 3.3V)
   - GND → Common ground
   - 30 LEDs at 20mA each = 600mA typical at brightness 50

4. **Blue Robotics PSM**
   - PSM V_OUT → GPIO11 (AD11) — Analog voltage
   - PSM I_OUT → GPIO12 (AD12) — Analog current
   - Common GND
   - PSM powered from battery sense side

5. **Relays (2x)**
   - Relay 1 Control → GPIO4 (Navigator/Pi/camera/lights power)
   - Relay 2 Signal → GPIO35 (electrolytic release, relay coil from battery)

### Power Supply

- Main battery: 4S LiPo (11.0V - 16.8V)
- Monitor via PSM analog inputs
- Ensure supercapacitor for Iridium
- External 5V supply for NeoPixel strip

## 2. Software Setup

### Install PlatformIO

```bash
# Install VSCode
# Then install PlatformIO IDE extension

# Or use command line
pip install platformio
```

### Clone and Build

```bash
git clone https://github.com/yourusername/Doris-AGT-Firmware.git
cd Doris-AGT-Firmware
git submodule update --init --recursive

# Build
pio run

# Upload
pio run -t upload

# Monitor (57600 baud)
pio device monitor -b 57600
```

## 3. Initial Configuration

After uploading, connect to serial monitor (57600 baud):

```
# View current config
config

# Set Iridium interval (seconds)
set_iridium_interval 600

# Set Meshtastic interval (seconds)
set_meshtastic_interval 3

# Save configuration
save
```

## 4. Testing Each Subsystem

### GPS Test
1. Place unit outdoors with clear sky view
2. Watch serial monitor for: `GPS: Fix - Lat: XX.XXXXXX Lon: XX.XXXXXX Sats: X`
3. Should acquire fix within 2-3 minutes
4. Use `gps` command to check status
5. Use `gps_diag` for backup battery and BBR diagnostics

### Iridium Test
```
# Enable Iridium
enable_iridium
save

# Enter self-test (Iridium can only TX in SELF_TEST or RECOVERY)
start_self_test

# Wait for GPS fix
# Iridium will transmit at configured interval
# Watch for: "Iridium: Message sent successfully"
```

### Meshtastic Test
```
# Enable Meshtastic
enable_meshtastic
save

# Send test NMEA with known coordinates
mesh_test_gps

# Check Meshtastic app — RAK node should show position on map
```

### MAVLink Test
```
# Enable MAVLink
enable_mavlink
save

# Connect Navigator to AGT via USB
# Use MAVProxy or Mission Planner to verify GPS data
# System ID: 1, Component ID: 191
```

### NeoPixel Test
```
# Should show status automatically
# PRE_MISSION: pre-mission pattern
# After start_self_test: self-test pattern
# RECOVERY: strobe pattern
```

### Battery Monitor Test
```
# Enable PSM (disabled by default)
enable_psm
save

# Watch for periodic battery readings:
# "PSM: V=12.34V I=1.23A P=15.18W"
```

### Relay Test

**Power Management Relay (Relay 1):**
- ON in PRE_MISSION, SELF_TEST, MISSION
- OFF in RECOVERY

**Release Relay (Relay 2):**
```
# Trigger failsafe to fire release relay and enter recovery
release_now

# Reset to start over
reset
```

## 5. Typical Usage Workflow

### Normal Deployment

```
# 1. Power on AGT — starts in PRE_MISSION
# 2. Wait for GPS fix
# 3. Configure mission parameters
set_iridium_interval 600
save

# 4. Start self-test
start_self_test

# 5. Deploy — system enters MISSION when depth > 2m (from autopilot)
# 6. Automatic RECOVERY when depth < 3m or GPS fix
# 7. Iridium reports position for recovery
# 8. After physical recovery:
reset
```

### Testing Failsafe

```
# Start in PRE_MISSION
start_self_test

# Simulate going underwater (need autopilot sending depth > 2m)
# Or test failsafe directly:
release_now

# System fires release relay and enters RECOVERY
# Strobe LEDs active, Iridium position reports

# Reset when done
reset
```

## 6. Troubleshooting Quick Checks

| Issue | Quick Check |
|-------|-------------|
| No serial output | Check USB cable, driver, baud rate (57600) |
| GPS not locking | Check antenna, sky view, wait 3 minutes, `gps_diag` |
| Iridium fails | Check supercap charged, signal quality >= 2, must be in SELF_TEST or RECOVERY |
| Meshtastic no position | Check D39→RAK J10 RX wiring, `mesh_test_gps`, baud 9600 |
| No NeoPixels | Check GPIO32 connection, external 5V power supply |
| PSM reads zero | Check GPIO11/12 analog connections, `enable_psm`, `save` |
| Relay not working | Check GPIO4/35, verify active high, check relay coil power |

## 7. Monitoring and Debugging

### Serial Monitor Output

Normal operation shows:
```
GPS: Fix - Lat: 37.422408 Lon: -122.084108 Sats: 12
PSM: V=12.45V I=1.23A P=15.32W
Meshtastic: NMEA GPS output -> J10 (external GPS UART)
MAVLink: Interface initialized
Iridium: Message sent successfully
```

### Status Command

```
status
```

Shows:
- Current state (PRE_MISSION / SELF_TEST / MISSION / RECOVERY)
- Time in current state
- Nonessentials powered (ON/OFF)
- Release triggered (YES/NO)
- Last failsafe source (if any)

### LED Reference

- **PRE_MISSION**: Pre-mission indicator
- **SELF_TEST**: Self-test indicator
- **MISSION (GPS fix)**: Green pulse
- **MISSION (no fix)**: Yellow pulse
- **RECOVERY**: Strobe (high-visibility flashing)

## 8. Advanced Configuration

### Calculate Unix Timestamp for GMT Events

```python
from datetime import datetime
import calendar

# Example: January 1, 2026, 14:30:00 UTC
dt = datetime(2026, 1, 1, 14, 30, 0)
timestamp = calendar.timegm(dt.timetuple())
print(f"Unix timestamp: {timestamp}")
```

Or use online converter: https://www.unixtimestamp.com/

Then configure:
```
set_timed_event gmt 1767271800 1500
save
```

## 9. Next Steps

- Read full [README_FIRMWARE.md](../README_FIRMWARE.md) for details
- Review [State Machine](STATE_MACHINE.md) architecture
- Review [Meshtastic Protocol](MESHTASTIC_PROTOCOL.md) for NMEA details
- Customize thresholds in [config.h](../include/config.h)
- Modify LED patterns in [neopixel_controller.cpp](../src/modules/neopixel_controller.cpp)
- Adjust failsafe thresholds for your deployment

## Support

Check serial output for error messages and refer to the main documentation for detailed troubleshooting.
