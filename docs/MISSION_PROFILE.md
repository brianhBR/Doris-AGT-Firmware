# Oceanographic Drop Camera Mission Profile

## Mission Overview

The drop camera system performs autonomous deep-sea deployments for oceanographic research, recording video and environmental data at depth for 24 hours before automatically surfacing for recovery.

## Mission Phases

### Phase 1: Pre-Deployment (Surface)
**Duration**: Variable (minutes to hours)
**Location**: On research vessel

**System State:**
- AGT powered ON
- GPS acquiring fix
- Configuration verified via BlueOS
- All systems operational for testing
- Battery fully charged

**AGT Operations:**
- GPS fix acquisition and RTC synchronization
- Iridium signal quality check
- Meshtastic mesh connectivity test
- Battery voltage/current monitoring
- NeoPixel status: **Blue Rainbow** (boot) → **Yellow Pulse** (GPS search) → **Green Pulse** (GPS fix)

**Critical Checks:**
- [ ] GPS fix acquired (minimum 6 satellites)
- [ ] RTC synchronized to GPS time
- [ ] Iridium signal quality ≥ 2
- [ ] Battery voltage > 13.5V (for 4S LiPo)
- [ ] Drop weight release time configured correctly
- [ ] Power save voltage set appropriately
- [ ] Test both relays (power management + drop weight)
- [ ] Verify NeoPixel visibility
- [ ] Confirm Meshtastic connectivity

---

### Phase 2: Deployment (Descent)
**Duration**: ~10-60 minutes (depth dependent)
**Location**: Water column

**System State:**
- Free-falling through water column
- All systems recording
- AGT tracking position (may lose GPS below surface)
- Navigator/Pi recording video and sensor data

**AGT Operations:**
- GPS tracking continues until submerged
- Last known surface position logged
- Iridium reports position before losing signal (if configured for frequent updates during deployment)
- Battery monitoring active
- NeoPixel status: **Green Pulse** (operational)

**Typical Descent Rates:**
- 0.5 - 2.0 m/s depending on ballast configuration

**Power Budget:**
- Navigator/Pi: 5-10W
- Camera/Lights: 10-50W
- AGT: 1-2W
- **Total**: 16-62W

---

### Phase 3: Seafloor Recording (On Bottom)
**Duration**: ~24 hours
**Location**: Seafloor

**System State:**
- Stationary on seafloor
- Camera and sensors recording continuously
- AGT maintaining timekeeping for ballast release
- No GPS signal (underwater)
- No Iridium communication (underwater)

**AGT Operations:**
- RTC running, counting down to ballast release
- Battery monitoring (critical for mission success)
- System health monitoring
- NeoPixel status: **Green Pulse** (operational)
- Relay 1 (power management): ON (all systems powered)
- Relay 2 (drop weight): ARMED, waiting for trigger time

**Critical Function:**
- Drop weight relay timing maintained via RTC
- Battery must last for full recording period + ascent + surface wait

**Power Budget:**
- Navigator/Pi: 5-10W
- Camera/Lights: 10-50W (may cycle)
- AGT: 1-2W
- **24-hour energy requirement**: 384-1488 Wh

**Recommended Battery:**
- Minimum: 2000 Wh (4S 20Ah LiPo at ~14V)
- Recommended: 3000+ Wh for safety margin and surface wait

---

### Phase 4: Ballast Release (Ascent Start)
**Duration**: 5 seconds
**Location**: Seafloor

**System State:**
- AGT RTC reaches programmed release time
- Ballast release relay activated

**AGT Operations:**
1. RTC reaches trigger time (GMT or delay)
2. Relay 2 (drop weight) activates for programmed duration (typically 5000ms)
3. Drop weight mechanism releases ballast
4. System begins positive buoyancy ascent
5. Relay 2 deactivates after duration expires

**Configuration Examples:**

**GMT Mode** (specific time):
```
Deployment: Dec 15, 2025, 08:00:00 GMT
Recording: 24 hours
Release: Dec 16, 2025, 08:00:00 GMT
Unix timestamp: 1734336000
Command: set_timed_event gmt 1734336000 5000
```

**Delay Mode** (relative):
```
Deployment: Power-on at unknown time
Recording: 24 hours
Release: 86400 seconds after boot
Command: set_timed_event delay 86400 5000
```

**NeoPixel Status:** May still show **Green Pulse** but irrelevant underwater

---

### Phase 5: Ascent (Return to Surface)
**Duration**: ~10-60 minutes (depth dependent)
**Location**: Water column

**System State:**
- Ascending due to positive buoyancy
- All systems still powered (Relay 1 ON)
- AGT waiting to reacquire GPS at surface
- Camera may continue recording ascent

**AGT Operations:**
- Waiting for GPS signal (attempts to acquire)
- Battery monitoring active
- Relay 1 still ON (systems powered)
- NeoPixel status: **Green Pulse** → **Yellow Pulse** (searching for GPS)

**Typical Ascent Rates:**
- 0.5 - 1.5 m/s depending on buoyancy

**Power Budget:**
- Same as Phase 3 (16-62W)
- Duration: 10-60 minutes

---

### Phase 6: Surface Recovery Wait (Critical Power Management)
**Duration**: Hours to days (unpredictable)
**Location**: Surface, drifting

**System State:**
- Floating on surface
- GPS fix reacquired
- Iridium reporting position for recovery
- Meshtastic active if recovery vessel nearby
- **Power conservation mode may activate**

**AGT Operations:**

**Initial Surface Arrival (Battery > Threshold):**
- GPS fix reacquired (RTC re-synchronized)
- Iridium reports position every 10 minutes (configurable)
- Meshtastic broadcasts position every 30 seconds (configurable)
- MAVLink sends GPS to Navigator (if still powered)
- NeoPixel status: **Green Pulse** (GPS fix) or **Magenta Chase** (during Iridium TX)

**Power Save Mode (Battery < Threshold):**
When battery voltage drops below configured threshold (default 11.5V):
1. **Relay 1 turns OFF** → Shuts down Navigator/Pi, camera, lights
2. AGT continues operating on minimal power
3. Iridium reports position at configured interval
4. Meshtastic continues mesh broadcasts
5. NeoPixel status: **Orange Pulse** (low battery warning)

**Power Budget (Normal Mode):**
- Navigator/Pi: 5-10W
- Camera: OFF (not recording)
- Lights: OFF
- AGT + Iridium: 2-5W (average, spikes during TX)
- Meshtastic: 0.5-1W
- **Total**: 7.5-16W

**Power Budget (Power Save Mode - Relay 1 OFF):**
- Navigator/Pi: OFF (0W)
- Camera: OFF (0W)
- Lights: OFF (0W)
- AGT + Iridium: 2-5W (average)
- Meshtastic: 0.5-1W
- NeoPixels: 0.5-2W
- **Total**: 3-8W

**Extended Surface Wait Calculations:**

Assuming 2000Wh battery, with 1500Wh remaining at surface:

| Mode | Power Draw | Runtime |
|------|------------|---------|
| Normal (all systems) | 12W avg | 125 hours (5.2 days) |
| Power Save (AGT only) | 5W avg | 300 hours (12.5 days) |

**Recovery Visual Aids:**
- NeoPixel LEDs visible from recovery vessel
- Orange pulse indicates low battery/power save mode
- Green pulse indicates healthy battery
- Magenta chase indicates active Iridium transmission

---

### Phase 7: Recovery (Mission Complete)
**Duration**: Minutes
**Location**: Surface, at vessel

**System State:**
- Retrieved by recovery team
- Visual confirmation via NeoPixels
- GPS position verified

**AGT Operations:**
- Continues GPS tracking until powered off
- Final position logged
- Mission data available on Navigator/Pi (if powered)
- AGT configuration and logs retrievable

**Post-Recovery:**
1. Power OFF system
2. Download data from Navigator/Pi
3. Download AGT logs via USB serial
4. Recharge batteries
5. Inspect drop weight mechanism
6. Prepare for next deployment

---

## Configuration Recommendations

### For Short Surface Wait (< 2 days)
```bash
# More frequent Iridium reports
set_iridium_interval 300          # 5 minutes

# Frequent mesh updates
set_meshtastic_interval 15        # 15 seconds

# Higher power save threshold (more conservative)
set_power_save_voltage 12.0       # 12.0V

# Enable all features
enable_iridium
enable_meshtastic
enable_mavlink
enable_psm
enable_neopixels

save
```

### For Extended Surface Wait (> 2 days)
```bash
# Less frequent Iridium reports (conserve credits)
set_iridium_interval 1800         # 30 minutes

# Standard mesh updates
set_meshtastic_interval 30        # 30 seconds

# Lower power save threshold (maximize operational time)
set_power_save_voltage 11.5       # 11.5V

# Enable all features
enable_iridium
enable_meshtastic
enable_mavlink
enable_psm
enable_neopixels

save
```

### For Deep Ocean (> 3000m)
```bash
# Longer descent/ascent times
# Consider delay-based release (more predictable than GPS time)
set_timed_event delay 90000 5000  # 25 hours (24hr + 1hr margin)

# Standard intervals
set_iridium_interval 600          # 10 minutes
set_meshtastic_interval 30        # 30 seconds

save
```

## Emergency Procedures

### Lost Communication
If recovery team loses contact:
- AGT continues to broadcast position via Iridium at configured interval
- Meshtastic provides local range backup (5-10km typically)
- NeoPixels provide visual signal if within sight

### Battery Critical
If battery voltage drops to critical level (< 11.0V):
- Relay 1 already OFF (power save active)
- AGT continues minimal operations
- Iridium reports may become unreliable below ~10.5V
- GPS tracking continues

### Drop Weight Failure
If drop weight does not release:
- System remains on seafloor
- Battery will eventually deplete
- No automatic recovery possible
- Consider manual release mechanisms as backup
- Monitor via absence of Iridium reports

### Early Surfacing
If system surfaces before planned time:
- Check GPS position reports via Iridium
- May indicate drop weight malfunction or unexpected buoyancy
- Recovery team should respond promptly

## Data Analysis

Post-mission data includes:
- **AGT Logs**: GPS track, battery history, Iridium transmissions
- **Navigator/Pi**: Video, oceanographic sensors, autopilot logs
- **Iridium History**: Position reports during surface phases
- **Mission Timeline**: Deployment, ballast release, surface arrival

## Safety Considerations

1. **Redundancy**: Consider backup release mechanism (galvanic timed release)
2. **Battery Margin**: Always size battery with 50% margin
3. **Testing**: Test full mission profile on bench before deployment
4. **GPS Sync**: Ensure RTC synchronized before deployment
5. **Weather**: Monitor for surface recovery conditions
6. **Visibility**: Ensure NeoPixels visible in expected sea state
7. **Communications**: Test both Iridium and Meshtastic before deployment

## Recommended Deployment Checklist

```
□ System Checks
  □ AGT firmware version verified
  □ GPS fix acquired (≥6 satellites)
  □ RTC synchronized to GPS
  □ Iridium signal quality ≥2
  □ Meshtastic link confirmed
  □ Battery voltage ≥13.5V (4S LiPo)
  □ PSM reading correctly

□ Configuration
  □ Drop weight release time configured
  □ Iridium interval appropriate for recovery plan
  □ Power save voltage set correctly
  □ Configuration saved to EEPROM
  □ Configuration verified via BlueOS

□ Hardware
  □ Drop weight mechanism tested
  □ Relay 1 (power mgmt) tested
  □ Relay 2 (drop weight) tested
  □ NeoPixels visible and functioning
  □ All cables secure and waterproof
  □ Pressure housing sealed

□ Recording Systems
  □ Camera recording confirmed
  □ Navigator/Pi logging data
  □ Storage capacity sufficient (>24hr)
  □ Lights functioning

□ Pre-Deployment
  □ Deployment time logged
  □ GPS position at deployment logged
  □ Expected recovery time calculated
  □ Recovery team briefed
  □ Emergency contact procedures confirmed

□ Deployment
  □ System armed and operational
  □ Over-the-side deployment safe
  □ Initial Iridium report received
  □ System descent observed (if shallow)
```

## Troubleshooting

| Symptom | Cause | Solution |
|---------|-------|----------|
| No GPS fix | Antenna blocked | Check antenna, wait longer |
| Iridium failed | No supercap charge | Check PGOOD signal, wait for charge |
| No NeoPixels | Power issue | Check 5V supply, GPIO32 connection |
| Drop weight early | RTC not synced | Verify GPS sync before deployment |
| Relay not switching | Configuration error | Test relays before deployment |
| PSM no reading | Connection issue | Check GPIO11/12 analog connections |

## References

- [AGT Firmware Documentation](../README_FIRMWARE.md)
- [BlueOS Integration Guide](BLUEOS_INTEGRATION.md)
- [Wiring Diagram](WIRING_DIAGRAM.md)
- [Quick Start Guide](QUICK_START.md)
