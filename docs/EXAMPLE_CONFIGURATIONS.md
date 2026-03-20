# Example Configurations

This document provides ready-to-use configuration examples for common deployment scenarios.

## Configuration Workflow

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

## Default Configuration

When reset to defaults (`reset` in config manager), the firmware uses:

```
Iridium Interval:     600 seconds (10 minutes)
Meshtastic Interval:  3 seconds
MAVLink Interval:     1000 ms (1 Hz)
Power Save Voltage:   11.5V
Enabled:              Iridium, Meshtastic, MAVLink, NeoPixels
Disabled:             PSM (analog battery monitor)
Timed Event:          Disabled (default duration 1500s when set)
```

---

## Scenario 1: Standard Drop Camera Deployment

**Use Case:** Full oceanographic drop camera mission with all communication channels.

**Configuration:**
```
enable_iridium
enable_meshtastic
enable_mavlink
enable_neopixels
disable_psm

set_iridium_interval 600
set_meshtastic_interval 3
set_mavlink_interval 1000

save
```

**Deployment:**
```
start_self_test
# Deploy when ready — MISSION auto-enters on depth > 2m
# RECOVERY auto-enters on depth < 3m or GPS fix
```

**Expected Behavior:**
- GPS → MAVLink to Navigator at 1 Hz
- GPS → Meshtastic NMEA every 3 seconds
- Iridium position + mission stats every 10 minutes (SELF_TEST and RECOVERY only)
- Failsafe monitors voltage, leak, depth, heartbeat during MISSION
- Recovery strobe LEDs when surfaced

---

## Scenario 2: Extended Surface Wait (Conserve Iridium)

**Use Case:** Deployment where recovery may take days. Reduce Iridium transmissions to save credits.

**Configuration:**
```
enable_iridium
enable_meshtastic
enable_mavlink
enable_neopixels

set_iridium_interval 1800
set_meshtastic_interval 3

save
```

**Expected Behavior:**
- Iridium reports every 30 minutes (saves credits)
- Meshtastic NMEA every 3 seconds for local-range recovery
- Standard failsafe monitoring

---

## Scenario 3: Timed Release Mission

**Use Case:** Pre-programmed release at a specific time or delay.

**GMT Mode** (specific time):
```
# Release at Jan 15, 2026, 14:30:00 GMT
# Unix timestamp: 1736953800
set_timed_event gmt 1736953800 1500

enable_iridium
enable_meshtastic
enable_mavlink
enable_neopixels

set_iridium_interval 600

save
```

**Delay Mode** (relative to boot):
```
# Release 24 hours after boot, 25-minute electrolytic release
set_timed_event delay 86400 1500

enable_iridium
enable_meshtastic
enable_mavlink
enable_neopixels

set_iridium_interval 600

save
```

**Calculate Unix Timestamp:**
```python
from datetime import datetime
import calendar

dt = datetime(2026, 1, 15, 14, 30, 0)  # Year, Month, Day, Hour, Min, Sec (UTC)
timestamp = calendar.timegm(dt.timetuple())
print(f"Unix timestamp: {timestamp}")
```

Or use: https://www.unixtimestamp.com/

---

## Scenario 4: MAVLink GPS Provider Only

**Use Case:** Provide GPS data to ArduPilot Navigator, no satellite or mesh comms.

**Configuration:**
```
disable_iridium
disable_meshtastic

enable_mavlink
enable_neopixels

set_mavlink_interval 1000

save
```

**Expected Behavior:**
- GPS fix sent to Navigator via USB at 1 Hz
- Battery status sent with GPS updates (if PSM enabled)
- System time forwarded from RTC
- No Iridium or Meshtastic transmissions

**MAVLink Messages Sent:**
- `HEARTBEAT` — every 1 second
- `GPS_RAW_INT` — every 1 second (when GPS has fix)
- `BATTERY_STATUS` — every 1 second (if PSM enabled)
- `SYSTEM_TIME` — every 1 second (RTC synced from GPS)

---

## Scenario 5: Mesh Network GPS Node

**Use Case:** Part of a Meshtastic mesh network with frequent NMEA GPS output.

**Configuration:**
```
disable_iridium
disable_mavlink

enable_meshtastic
enable_neopixels

set_meshtastic_interval 3

save
```

**Expected Behavior:**
- NMEA GPS sentences to RAK4603 every 3 seconds
- RAK node shows AGT position on Meshtastic mesh
- No Iridium or MAVLink transmissions

---

## Scenario 6: Remote Asset Tracker (Iridium Only)

**Use Case:** Track remote equipment with periodic Iridium updates, no mesh or autopilot.

**Configuration:**
```
disable_meshtastic
disable_mavlink

enable_iridium
enable_neopixels

set_iridium_interval 600

save
```

**Note:** Iridium only transmits in SELF_TEST and RECOVERY states. Use `start_self_test` to enable transmission. The system will stay in SELF_TEST (no depth data to trigger MISSION) and periodically send position reports.

---

## Scenario 7: Power-Critical Deployment

**Use Case:** Extended deployment with minimal power consumption.

**Configuration:**
```
enable_iridium
enable_neopixels
disable_meshtastic
disable_mavlink
disable_psm

set_iridium_interval 14400        # 4 hours

save
```

**Additional Power Saving (Code Changes):**
```cpp
// In config.h, reduce LED brightness:
#define NEOPIXEL_BRIGHTNESS  10  // Very dim (was 50)
```

---

## Scenario 8: Development / Testing

**Use Case:** Bench testing with frequent updates and no Iridium charges.

**Configuration:**
```
disable_iridium

enable_meshtastic
enable_mavlink
enable_neopixels

set_meshtastic_interval 3
set_mavlink_interval 1000

save
```

**Testing Checklist:**
```
gps                     # Check GPS fix
gps_diag                # Check GPS backup battery
mesh_test_gps           # Send test NMEA to Meshtastic
status                  # Check state machine
start_self_test         # Enter self-test
release_now             # Test failsafe (fires relay, enters RECOVERY)
reset                   # Return to PRE_MISSION
```

---

## Serial Command Quick Reference

### State Control
```
start_self_test           # PRE_MISSION → SELF_TEST
release_now               # Trigger failsafe → RECOVERY
reset                     # Any → PRE_MISSION
status                    # Print state info
```

### GPS
```
gps                       # Show position or satellite count
gps_diag                  # BBR/backup battery diagnostics
```

### Meshtastic
```
mesh_test                 # Send test text
mesh_test_gps             # Send test NMEA coordinates
mesh_send <text>          # Send custom text
```

### Configuration
```
config                    # Show all settings
save                      # Save to EEPROM
set_iridium_interval <s>
set_meshtastic_interval <s>
set_mavlink_interval <ms>
set_timed_event <gmt|delay> <time> <duration_s>
set_power_save_voltage <V>
enable_<feature>
disable_<feature>
set_leak <0|1>            # Test leak flag
```

### Features
`iridium`, `meshtastic`, `mavlink`, `psm`, `neopixels`

---

## Troubleshooting Configurations

### Configuration Not Saving
```
save
# Should print: "Config: Saved successfully"

# Verify by viewing
config
```

### Features Not Working After Enable
- Some features require hardware connections
- PSM requires analog wiring on GPIO11/12
- Meshtastic requires SoftwareSerial wiring (D39 → RAK J10 RX)
- Restart device after configuration change if needed

### Iridium Not Transmitting
- Iridium only transmits in SELF_TEST and RECOVERY states
- Use `start_self_test` to enter a transmit-capable state
- Check supercapacitor charge (PGOOD)

### Timed Event Not Triggering
**For GMT mode:**
- Verify RTC is set (needs GPS fix first)
- Check Unix timestamp is correct and in the future

**For delay mode:**
- Check uptime via `status` (time in state)
- Verify trigger time hasn't already passed

---

## References

- [README_FIRMWARE.md](../README_FIRMWARE.md) — full feature documentation
- [QUICK_START.md](QUICK_START.md) — setup instructions
- [WIRING_DIAGRAM.md](WIRING_DIAGRAM.md) — hardware connections
- [STATE_MACHINE.md](STATE_MACHINE.md) — state machine architecture
