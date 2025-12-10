# Quick Start Guide

## 1. Hardware Setup

### Connections Required

1. **Meshtastic RAK4603**
   - RAK4603 TX → AGT Serial2 RX
   - RAK4603 RX → AGT Serial2 TX
   - Common ground

2. **ArduPilot Navigator**
   - USB cable: AGT ↔ Navigator
   - Uses native USB serial

3. **NeoPixel LED Strip**
   - Data pin → GPIO32
   - +5V → Power supply
   - GND → Common ground
   - Note: Ensure adequate 5V power supply (30 LEDs × 60mA max = 1.8A)

4. **Blue Robotics PSM**
   - SDA → AGT I2C SDA (GPIO40)
   - SCL → AGT I2C SCL (GPIO39)
   - Power and sensor connections per PSM manual

5. **Relays (2x)**
   - Relay 1 Control → GPIO4
   - Relay 2 Control → GPIO35
   - Connect relay loads as needed

### Power Supply

- Main battery: 4S LiPo (11.0V - 16.8V)
- Monitor via PSM
- Ensure supercapacitor for Iridium

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

# Monitor
pio device monitor -b 115200
```

## 3. Initial Configuration

After uploading, connect to serial monitor (115200 baud):

```
# View current config
config

# Set Iridium interval (seconds)
set_iridium_interval 600

# Set Meshtastic interval (seconds)
set_meshtastic_interval 30

# Set low battery threshold
set_power_save_voltage 11.5

# Save configuration
save
```

## 4. Testing Each Subsystem

### GPS Test
1. Place unit outdoors with clear sky view
2. Watch serial monitor for: `GPS: Fix - Lat: XX.XXXXXX Lon: XX.XXXXXX Sats: X`
3. Should acquire fix within 2-3 minutes

### Iridium Test
```
# Enable Iridium
enable_iridium
save

# Wait for GPS fix
# Iridium will transmit at configured interval
# Watch for: "Iridium: Message sent successfully"
```

### Meshtastic Test
```
# Enable Meshtastic
enable_meshtastic
save

# Check serial output for transmission confirmations
# Verify reception on other Meshtastic nodes
```

### MAVLink Test
```
# Enable MAVLink
enable_mavlink
save

# Connect Navigator to AGT via USB
# Use MAVProxy or Mission Planner to verify GPS data
```

### NeoPixel Test
```
# Should show status automatically
# Boot: Blue rainbow
# GPS Search: Yellow pulse
# GPS Fix: Green pulse
```

### Battery Monitor Test
```
# Enable PSM
enable_psm
save

# Watch for periodic battery readings:
# "PSM: V=12.34V I=1.23A P=15.18W"
```

### Relay Test

**Power Management Relay:**
- Automatically controlled by battery voltage
- Below 11.5V: Relay 1 turns OFF

**Timed Event Relay:**
```
# Set timed event (10 seconds from boot, 5 second duration)
set_timed_event delay 10 5000
save

# Reset to trigger again
# Device will activate relay 2 after 10 seconds for 5 seconds
```

## 5. Typical Usage Scenarios

### Scenario 1: Remote Tracking
```
enable_iridium
enable_neopixels
enable_psm
disable_meshtastic
disable_mavlink
set_iridium_interval 600
save
```

### Scenario 2: Marine Deployment with Timed Event
```
# All features enabled
# Timed event at specific GMT time
# Example: Release at Jan 1, 2025, 12:00:00 GMT
set_timed_event gmt 1735732800 5000
enable_iridium
enable_psm
enable_neopixels
save
```

### Scenario 3: UAV Integration
```
enable_mavlink
enable_gps
enable_psm
disable_iridium
disable_meshtastic
set_mavlink_interval 1000
save
```

### Scenario 4: Mesh Network Node
```
enable_meshtastic
enable_gps
enable_neopixels
disable_iridium
disable_mavlink
set_meshtastic_interval 30
save
```

## 6. Troubleshooting Quick Checks

| Issue | Quick Check |
|-------|-------------|
| No serial output | Check USB cable, driver, baud rate (115200) |
| GPS not locking | Check antenna, sky view, wait 3 minutes |
| Iridium fails | Check supercap charged, signal quality >= 2 |
| No NeoPixels | Check GPIO32 connection, power supply |
| PSM not found | Run I2C scanner, check address 0x40 |
| Relay not working | Check GPIO4/35, verify active high/low |

## 7. Monitoring and Debugging

### Serial Monitor Output

Normal operation shows:
```
GPS: Fix - Lat: 37.422408 Lon: -122.084108 Sats: 12
PSM: V=12.45V I=1.23A P=15.32W
Meshtastic: Sent position - POS:37.422408,-122.084108,15.2m,12sat
MAVLink: Autopilot heartbeat received
Iridium: Message sent successfully
```

### Status LED Reference

- **Blue rainbow**: Booting
- **Yellow pulse**: GPS searching
- **Green pulse**: GPS locked, normal operation
- **Magenta chase**: Iridium transmitting
- **Orange pulse**: Low battery warning
- **Red blink**: System error

## 8. Advanced Configuration

### Calculate Unix Timestamp for GMT Events

```python
from datetime import datetime
import time

# Example: January 1, 2026, 14:30:00 UTC
dt = datetime(2026, 1, 1, 14, 30, 0)
timestamp = int(time.mktime(dt.timetuple()))
print(f"Unix timestamp: {timestamp}")
```

Or use online converter: https://www.unixtimestamp.com/

Then configure:
```
set_timed_event gmt 1767268200 5000
save
```

## 9. Next Steps

- Read full [README_FIRMWARE.md](../README_FIRMWARE.md) for details
- Customize intervals in [config.h](../include/config.h)
- Modify LED patterns in [neopixel_controller.cpp](../src/modules/neopixel_controller.cpp)
- Adjust battery thresholds for your battery type

## Support

Check serial output for error messages and refer to the main documentation for detailed troubleshooting.
