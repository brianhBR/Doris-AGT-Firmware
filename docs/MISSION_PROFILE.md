# Oceanographic Drop Camera Mission Profile

## Mission Overview

The drop camera system performs autonomous deep-sea deployments for oceanographic research, recording video and environmental data at depth before automatically surfacing for recovery.

## System States

```
PRE_MISSION → SELF_TEST → MISSION → RECOVERY
```

- **PRE_MISSION**: Power on, GPS fix, configure
- **SELF_TEST**: Verify systems, Iridium position check; depth > 2m → MISSION
- **MISSION**: Underwater recording; failsafe monitoring active
- **RECOVERY**: Surface, strobe LEDs, Iridium position reports

## Mission Phases

### Phase 1: Pre-Mission (Surface)
**Duration**: Variable (minutes to hours)
**Location**: On research vessel
**State**: PRE_MISSION

**System State:**
- AGT powered ON
- GPS acquiring fix
- Configuration verified via serial/BlueOS
- All systems operational for testing
- Battery fully charged

**AGT Operations:**
- GPS fix acquisition and RTC synchronization
- Iridium signal quality check (after `start_self_test`)
- Meshtastic NMEA output active
- Battery voltage/current monitoring (if PSM enabled)
- NeoPixel status: Pre-mission pattern

**Critical Checks:**
- [ ] GPS fix acquired (minimum 4 satellites)
- [ ] RTC synchronized to GPS time
- [ ] Iridium signal quality >= 2 (test in SELF_TEST state)
- [ ] Battery voltage > 13.5V (for 4S LiPo)
- [ ] Configuration saved to EEPROM
- [ ] Test release relay with `release_now` (then `reset`)
- [ ] Verify NeoPixel visibility
- [ ] Confirm Meshtastic NMEA received by RAK (`mesh_test_gps`)

---

### Phase 2: Self-Test (Surface, Ready to Deploy)
**Duration**: Minutes (until deployed)
**Location**: On research vessel, ready for deployment
**State**: SELF_TEST

**System State:**
- `start_self_test` command issued
- Iridium can transmit (pre-deployment position check)
- Waiting for depth > 2m to transition to MISSION
- All systems verified

**AGT Operations:**
- Iridium sends position report (confirms satellite link)
- GPS tracking continues
- Meshtastic NMEA output active
- MAVLink data flowing to/from Navigator

**Transition to MISSION:**
- Automatic when MAVLink reports depth > 2m (from SCALED_PRESSURE or VFR_HUD)
- This confirms the system is actually submerged

---

### Phase 3: Deployment (Descent)
**Duration**: ~10-60 minutes (depth dependent)
**Location**: Water column
**State**: MISSION

**System State:**
- Free-falling through water column
- All systems recording
- GPS fix lost below surface (expected)
- Navigator/Pi recording video and sensor data
- Failsafe monitoring active

**AGT Operations:**
- Monitors depth from autopilot via MAVLink
- Tracks max depth for mission reports
- Monitors failsafe conditions (voltage, leak, depth limit, heartbeat)
- Meshtastic NMEA continues (no GPS fix, sends empty NMEA)
- NeoPixel status: Yellow pulse (no GPS fix)

**Failsafe Conditions Monitored:**
- Battery voltage < 11.0V (autopilot-confirmed)
- Leak detected
- Depth > 200m (configurable)
- No autopilot heartbeat for > 30s

**Power Budget:**
- Navigator/Pi: 5-10W
- Camera/Lights: 10-50W
- AGT: 1-2W
- **Total**: 16-62W

---

### Phase 4: Seafloor Recording (On Bottom)
**Duration**: Hours to days (mission dependent)
**Location**: Seafloor
**State**: MISSION

**System State:**
- Stationary on seafloor
- Camera and sensors recording continuously
- No GPS signal (underwater)
- No Iridium communication (underwater)
- Failsafe monitoring continues

**AGT Operations:**
- Depth monitoring via MAVLink
- Battery voltage monitoring via MAVLink
- Leak detection monitoring
- Heartbeat watchdog active
- Relay 1 ON (Navigator/Pi powered)
- Relay 2 OFF (release not triggered yet)

**Critical Function:**
- Failsafe system provides hardware-level safety independent of mission planning
- If any failsafe triggers, release relay fires and system enters RECOVERY

**Power Budget:**
- Navigator/Pi: 5-10W
- Camera/Lights: 10-50W (may cycle)
- AGT: 1-2W
- **24-hour energy requirement**: 384-1488 Wh

**Recommended Battery:**
- Minimum: 2000 Wh (4S 20Ah LiPo at ~14V)
- Recommended: 3000+ Wh for safety margin and surface wait

---

### Phase 5: Surfacing
**Duration**: ~10-60 minutes (depth dependent)
**Location**: Water column, ascending
**State**: Transitions MISSION → RECOVERY

**Transition Trigger:**
- Depth < 3m (from MAVLink) OR GPS fix acquired
- Either condition confirms the system has surfaced

**What Happens on Transition to RECOVERY:**
1. Relay 1 turns OFF (Navigator/Pi, camera, lights powered down)
2. Strobe LEDs activate for visual location
3. Iridium begins position reporting at configured interval
4. Meshtastic NMEA continues

If surfacing was triggered by failsafe:
1. Release relay fires for 1500 seconds (25 minutes, electrolytic dissolution)
2. Same RECOVERY behavior as above

---

### Phase 6: Surface Recovery Wait
**Duration**: Hours to days (unpredictable)
**Location**: Surface, drifting
**State**: RECOVERY

**System State:**
- Floating on surface
- GPS fix reacquired
- Iridium reporting position for recovery
- Meshtastic NMEA active (if recovery vessel nearby with Meshtastic)
- **Navigator/Pi OFF** (Relay 1 OFF, conserving power)
- **Strobe LEDs** active for visual location

**AGT Operations:**
- GPS fix reacquired (RTC re-synchronized)
- Iridium reports position + mission stats at configured interval (default 10 min)
- Meshtastic NMEA output active
- NeoPixel strobe pattern for visual recovery aid
- Minimal power consumption

**Iridium Reports Include:**
- GPS position (lat, lon, alt)
- Mission statistics (max depth, battery voltage, failsafe source if triggered)

**Power Budget (RECOVERY Mode — Relay 1 OFF):**
- Navigator/Pi: OFF (0W)
- Camera: OFF (0W)
- Lights: OFF (0W)
- AGT + Iridium: 2-5W (average, spikes during TX)
- Meshtastic: 0.5-1W
- NeoPixels: 0.5-2W
- **Total: ~3-8W**

**Extended Surface Wait Calculations:**

Assuming 2000Wh battery, with 1500Wh remaining at surface:

| Mode | Power Draw | Runtime |
|------|------------|---------|
| RECOVERY (AGT only) | 5W avg | 300 hours (12.5 days) |

**Recovery Visual Aids:**
- NeoPixel strobe pattern visible from recovery vessel

---

### Phase 7: Recovery (Mission Complete)
**Duration**: Minutes
**Location**: Surface, at vessel

**System State:**
- Retrieved by recovery team
- Visual confirmation via NeoPixel strobe
- GPS position verified via Iridium reports

**AGT Operations:**
- Continues GPS tracking and Iridium reports until powered off
- Final position logged

**Post-Recovery:**
1. Power OFF system
2. Download data from Navigator/Pi (reconnect power if needed)
3. Download AGT logs via USB serial (57600 baud)
4. Recharge batteries
5. Inspect release mechanism
6. `reset` to return to PRE_MISSION for next deployment

---

## Configuration Recommendations

### Standard Deployment
```
set_iridium_interval 600          # 10 minutes
set_meshtastic_interval 3         # 3 seconds
save

start_self_test                   # Enter self-test, wait for deployment
```

### Extended Surface Wait (Conserve Iridium Credits)
```
set_iridium_interval 1800         # 30 minutes
set_meshtastic_interval 3         # 3 seconds
save
```

### Timed Release (Instead of Failsafe)
```
# 24-hour mission, 25-minute electrolytic release
set_timed_event delay 86400 1500
save
```

## Failsafe Procedures

### Failsafe Triggers During MISSION

Any of these conditions automatically fire the release relay and enter RECOVERY:

| Trigger | Threshold | Description |
|---------|-----------|-------------|
| Low Voltage | < 11.0V | Autopilot battery critically low |
| Leak | Detected | Water ingress detected |
| Max Depth | > 200m | Exceeded safe operating depth |
| No Heartbeat | > 30s | Lost communication with autopilot |
| Manual | `release_now` | Operator abort |

### Lost Communication
- AGT continues Iridium position reports at configured interval
- Meshtastic provides local range backup (if recovery vessel has Meshtastic)
- NeoPixel strobe provides visual signal if within sight

### Release Mechanism Failure
- If electrolytic release does not dissolve, system remains on seafloor
- Battery will eventually deplete
- Monitor via absence of Iridium reports at expected time

## Deployment Checklist

```
Pre-Deployment
  [ ] AGT firmware version verified
  [ ] GPS fix acquired (>= 4 satellites)
  [ ] RTC synchronized to GPS
  [ ] Iridium signal quality >= 2 (test in SELF_TEST)
  [ ] Meshtastic NMEA confirmed (mesh_test_gps)
  [ ] Battery voltage >= 13.5V (4S LiPo)
  [ ] Configuration saved (config, then save)
  [ ] Release relay tested (release_now, then reset)
  [ ] NeoPixels visible and functioning

Hardware
  [ ] Release mechanism loaded and tested
  [ ] Relay 1 (power mgmt) wiring verified
  [ ] Relay 2 (release) wiring verified — coil from battery
  [ ] All cables secure and waterproof
  [ ] Pressure housing sealed
  [ ] Antenna secured

Recording Systems
  [ ] Camera recording confirmed
  [ ] Navigator/Pi logging data
  [ ] Storage capacity sufficient
  [ ] Lights functioning

Deployment
  [ ] start_self_test issued
  [ ] Iridium position report received
  [ ] System armed and operational
  [ ] Deployment time and GPS position logged
  [ ] Expected recovery time calculated
  [ ] Recovery team briefed
```

## Troubleshooting

| Symptom | Cause | Solution |
|---------|-------|----------|
| No GPS fix | Antenna blocked | Check antenna, wait longer |
| Iridium failed | No supercap charge | Check PGOOD signal, wait for charge |
| No NeoPixels | Power issue | Check 5V supply, GPIO32 connection |
| Premature RECOVERY | Depth < 3m or GPS fix | Check depth sensor calibration |
| Relay not switching | Configuration error | Test relays before deployment |
| PSM no reading | Not enabled or wiring | `enable_psm`, check GPIO11/12 |

## References

- [AGT Firmware Documentation](../README_FIRMWARE.md)
- [State Machine Architecture](STATE_MACHINE.md)
- [BlueOS Integration Guide](BLUEOS_INTEGRATION.md)
- [Wiring Diagram](WIRING_DIAGRAM.md)
- [Quick Start Guide](QUICK_START.md)
