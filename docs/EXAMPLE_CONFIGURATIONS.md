# Example Configurations

This document provides ready-to-use configuration examples for common deployment scenarios.

## Configuration Template

```
# View current config
config

# Set your configuration
<commands here>

# Save to EEPROM
save

# Verify
config
```

## Scenario 1: Remote Asset Tracker

**Use Case:** Track remote equipment with periodic Iridium updates, no mesh network.

**Requirements:**
- Iridium reporting every 10 minutes
- GPS tracking
- Battery monitoring
- Visual status
- No Meshtastic or MAVLink

**Configuration:**
```bash
# Disable unnecessary features
disable_meshtastic
disable_mavlink

# Enable required features
enable_iridium
enable_psm
enable_neopixels

# Set intervals
set_iridium_interval 600

# Set power save threshold
set_power_save_voltage 11.5

# Save
save
```

**Expected Behavior:**
- GPS acquires fix (yellow pulse → green pulse)
- Sends position via Iridium every 10 minutes (magenta chase)
- Monitors battery continuously
- Disables nonessentials below 11.5V (orange pulse)

---

## Scenario 2: Marine Deployment with Timed Release

**Use Case:** Ocean deployment with timed ballast release at specific GMT time.

**Requirements:**
- Iridium reporting every 30 minutes
- Timed event at specific date/time (ballast release)
- Battery monitoring critical
- Full status indication

**Configuration:**
```bash
# Enable all core features
enable_iridium
enable_psm
enable_neopixels
disable_meshtastic
disable_mavlink

# Set Iridium to 30 minute intervals
set_iridium_interval 1800

# Set timed event (example: Jan 15, 2025, 14:30:00 GMT)
# Calculate Unix timestamp: 1736953800
set_timed_event gmt 1736953800 5000

# Conservative power save
set_power_save_voltage 12.0

# Save
save
```

**Expected Behavior:**
- Tracks position and reports every 30 minutes
- At 2025-01-15 14:30:00 GMT, activates Relay 2 for 5 seconds
- Timed event is one-shot (doesn't repeat)
- Power management kicks in at 12.0V

**Calculate Unix Timestamp:**
```python
from datetime import datetime
import calendar

dt = datetime(2025, 1, 15, 14, 30, 0)  # Year, Month, Day, Hour, Min, Sec
timestamp = calendar.timegm(dt.timetuple())
print(f"Unix timestamp: {timestamp}")
```

---

## Scenario 3: UAV GPS Provider

**Use Case:** Provide GPS data to ArduPilot Navigator, no satellite comms.

**Requirements:**
- MAVLink GPS data at 1 Hz
- Battery monitoring
- No Iridium (save power/credits)
- No mesh networking
- Status LEDs

**Configuration:**
```bash
# Disable satellite comms
disable_iridium
disable_meshtastic

# Enable MAVLink and monitoring
enable_mavlink
enable_psm
enable_neopixels

# Set MAVLink rate to 1 Hz (1000ms)
set_mavlink_interval 1000

# Power save at 11.5V
set_power_save_voltage 11.5

# Save
save
```

**Expected Behavior:**
- GPS fix sent to Navigator via USB at 1 Hz
- Battery status sent with GPS updates
- No Iridium transmissions (saves credits and power)
- Standard power management

**MAVLink Messages Sent:**
- `HEARTBEAT` - Every 1 second
- `GPS_RAW_INT` - Every 1 second (when GPS has fix)
- `BATTERY_STATUS` - Every 1 second

---

## Scenario 4: Mesh Network Node

**Use Case:** Part of a Meshtastic mesh network with frequent updates.

**Requirements:**
- Meshtastic updates every 30 seconds
- GPS tracking for position sharing
- No satellite comms
- No autopilot integration
- Status indication

**Configuration:**
```bash
# Disable unnecessary features
disable_iridium
disable_mavlink

# Enable mesh networking
enable_meshtastic
enable_psm
enable_neopixels

# Frequent mesh updates
set_meshtastic_interval 30

# Power management
set_power_save_voltage 11.5

# Save
save
```

**Expected Behavior:**
- Sends position to mesh network every 30 seconds
- Shares telemetry data
- No Iridium transmissions
- Normal power management

---

## Scenario 5: Hybrid Tracker (Full Features)

**Use Case:** Maximum capability deployment with all features enabled.

**Requirements:**
- Iridium backup (hourly)
- Meshtastic primary (every minute)
- MAVLink to autopilot
- Full battery monitoring
- Visual status
- Power management

**Configuration:**
```bash
# Enable everything
enable_iridium
enable_meshtastic
enable_mavlink
enable_psm
enable_neopixels

# Set intervals
set_iridium_interval 3600        # 1 hour
set_meshtastic_interval 60        # 1 minute
set_mavlink_interval 1000         # 1 Hz

# Power management
set_power_save_voltage 11.5

# Save
save
```

**Expected Behavior:**
- Continuous GPS tracking
- MAVLink updates to Navigator at 1 Hz
- Meshtastic position every minute
- Iridium backup every hour
- Full battery monitoring
- Standard power management

**Power Considerations:**
- This configuration uses maximum power
- Ensure battery capacity is adequate
- Monitor current draw via PSM

---

## Scenario 6: Delayed Event Trigger

**Use Case:** Activate relay after fixed delay from power-on (e.g., 1 hour deployment timer).

**Requirements:**
- Activate relay 1 hour after boot
- GPS tracking
- Iridium reporting
- Battery monitoring

**Configuration:**
```bash
# Enable features
enable_iridium
enable_psm
enable_neopixels
disable_meshtastic
disable_mavlink

# Standard intervals
set_iridium_interval 600

# Set timed event: 3600 seconds (1 hour) after boot, 5 second duration
set_timed_event delay 3600 5000

# Power management
set_power_save_voltage 11.5

# Save
save
```

**Expected Behavior:**
- System boots normally
- After 3600 seconds (1 hour), Relay 2 activates for 5 seconds
- Normal tracking continues
- Timed event is one-shot

---

## Scenario 7: Power-Critical Deployment

**Use Case:** Extended deployment with aggressive power saving.

**Requirements:**
- Minimal Iridium updates (every 4 hours)
- No mesh or autopilot
- Aggressive power management
- Dim status LEDs

**Configuration:**
```bash
# Minimal features
enable_iridium
enable_psm
enable_neopixels
disable_meshtastic
disable_mavlink

# Long intervals
set_iridium_interval 14400        # 4 hours

# Conservative power save
set_power_save_voltage 12.0

# Save
save
```

**Additional Power Saving (Code Changes):**
```cpp
// In config.h, reduce LED brightness:
#define NEOPIXEL_BRIGHTNESS  10  // Very dim (was 50)

// Consider sleep modes between transmissions
// (requires code modifications)
```

**Expected Behavior:**
- Iridium transmissions every 4 hours
- Power save triggers early (12.0V)
- Minimal LED brightness
- Extended battery life

---

## Scenario 8: Development/Testing

**Use Case:** Testing and development configuration.

**Requirements:**
- Frequent updates for testing
- All features enabled
- Verbose output
- No actual Iridium transmissions (save credits)

**Configuration:**
```bash
# Enable all features
enable_meshtastic
enable_mavlink
enable_psm
enable_neopixels

# Disable Iridium to save credits
disable_iridium

# Frequent updates for testing
set_meshtastic_interval 10        # Every 10 seconds
set_mavlink_interval 1000         # 1 Hz

# Save
save
```

**Testing Checklist:**
- [ ] GPS acquires fix
- [ ] Meshtastic sends data every 10 seconds
- [ ] MAVLink updates at 1 Hz
- [ ] PSM reads battery correctly
- [ ] NeoPixels show proper colors
- [ ] Relays can be triggered manually
- [ ] Configuration saves/loads correctly

---

## Special Commands Reference

### Check System Status
```bash
config                  # Show full configuration
```

### Reset to Defaults
```bash
reset                   # Reset all settings to defaults
save                    # Save defaults
```

### Test Specific Features
```bash
# Test GPS only
disable_iridium
disable_meshtastic
disable_mavlink
enable_neopixels
save

# Test Iridium only
enable_iridium
disable_meshtastic
disable_mavlink
set_iridium_interval 60  # 1 minute for testing
save
```

### Manual Relay Testing
Modify code temporarily for manual testing:
```cpp
// In main.cpp loop():
if (Serial.available()) {
    char c = Serial.read();
    if (c == '1') {
        RelayController_setPowerManagement(true);
        Serial.println("Relay 1 ON");
    }
    if (c == '2') {
        RelayController_setPowerManagement(false);
        Serial.println("Relay 1 OFF");
    }
    if (c == '3') {
        RelayController_triggerTimedEvent(5000);
        Serial.println("Relay 2 triggered for 5s");
    }
}
```

---

## Unix Timestamp Quick Reference

Common deployment times (all times GMT):

| Description | Date/Time | Unix Timestamp |
|-------------|-----------|----------------|
| New Year 2025 | 2025-01-01 00:00:00 | 1735689600 |
| New Year 2026 | 2026-01-01 00:00:00 | 1767225600 |
| Mid 2025 | 2025-07-01 12:00:00 | 1719835200 |
| End 2025 | 2025-12-31 23:59:59 | 1735689599 |

**Calculate Custom Timestamp:**
- Online: https://www.unixtimestamp.com/
- Python: `calendar.timegm(datetime(Y,M,D,H,M,S).timetuple())`
- Command line: `date -u -d "2025-01-15 14:30:00" +%s`

---

## Troubleshooting Configurations

### Configuration Not Saving
```bash
# Check if save succeeded
save
# Should print: "Config: Saved successfully"

# Verify by reloading
config
```

### Configuration Reset After Power Cycle
- Check EEPROM library is working
- Verify checksum is calculated
- Ensure `save` command was executed

### Features Not Working After Enable
- Some features require hardware connections
- Check wiring for disabled features
- Restart device after configuration change

### Timed Event Not Triggering

**For GMT mode:**
- Verify RTC is set (needs GPS fix first)
- Check Unix timestamp is correct
- Ensure timestamp is in the future

**For delay mode:**
- Check uptime: `millis() / 1000` seconds
- Verify trigger time hasn't passed
- Check event is enabled in config

---

## Best Practices

1. **Always save after configuration changes**
   ```bash
   save
   ```

2. **Verify configuration before deployment**
   ```bash
   config
   ```

3. **Test critical features before deployment**
   - GPS fix acquisition
   - Iridium transmission (if enabled)
   - Timed event (if configured)
   - Battery monitoring

4. **Consider power budget**
   - More frequent updates = more power
   - Balance update rate with battery capacity
   - Test full system current draw

5. **Document your configuration**
   - Save configuration output to file
   - Note any custom code changes
   - Record deployment date/time for delay events

---

## Need Help?

- Check [README_FIRMWARE.md](../README_FIRMWARE.md) for detailed feature info
- Review [QUICK_START.md](QUICK_START.md) for setup instructions
- Verify [WIRING_DIAGRAM.md](WIRING_DIAGRAM.md) for connections
