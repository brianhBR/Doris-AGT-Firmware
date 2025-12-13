# Wiring Diagram

## System Overview

```
┌─────────────────────────────────────────────────────────────────┐
│                  Doris AGT System Architecture                   │
└─────────────────────────────────────────────────────────────────┘

                    ┌──────────────────────┐
                    │  SparkFun Artemis    │
                    │  Global Tracker      │
                    │      (AGT)           │
                    └──────────┬───────────┘
                               │
        ┌──────────────────────┼──────────────────────┐
        │                      │                      │
    [I2C Bus]             [Serial]              [GPIO/Analog]
        │                      │                      │
        ├─────────┬────────────┼────────────┬─────────┤
        │         │            │            │         │
    ┌───▼───┐ ┌──▼──────┐ ┌───▼────┐  ┌───▼───┐ ┌──▼──┐
    │  GPS  │ │Meshtastic│ │Iridium │  │ Relay │ │ LED │
    │ ZOE-M8│ │ RAK4603  │ │ 9603N  │  │  x2   │ │Strip│
    └───────┘ │ [UART0]  │ │[Serial1]│ └───────┘ └─────┘
              │ D39/D40  │ │D24/D25 │
              └──────────┘ └────┬───┘
                                │
                           [USB Serial]
                                │
                                │
                          ┌─────▼──────┐
                          │ ArduPilot  │
                          │ Navigator  │
                          │   [USB]    │
                          └────────────┘

    PSM (Analog): GPIO11/12 - Battery voltage/current monitoring
```

## Detailed Pin Connections

### 1. GPS (u-blox ZOE-M8Q)
**Built into AGT - No external connections needed**

| AGT Pin | Signal | Description |
|---------|--------|-------------|
| GPIO8 | SCL | I2C Clock to GPS |
| GPIO9 | SDA | I2C Data to GPS |
| GPIO10 | PIO14 | Geofence alert input |
| GPIO26 | GPS_EN | GPS power enable (active LOW) |

### 2. Blue Robotics PSM (Power Sense Module)

**IMPORTANT: PSM uses ANALOG outputs, NOT I2C**

| PSM Pin | AGT Pin | Signal | Notes |
|---------|---------|--------|-------|
| V_OUT | GPIO11 (AD11) | Voltage Analog | PSM voltage measurement |
| I_OUT | GPIO12 (AD12) | Current Analog | PSM current measurement |
| GND | GND | Ground | Common ground |
| +BATT | Battery + | Battery input | High voltage side |
| -BATT | Battery - | Battery ground | High voltage side |

**Analog Calibration:**
- Voltage: 11.0 V/V divider (11V input = 1V output)
- Current: 37.8788 A/V with 0.330V offset
- AGT reads via 14-bit ADC (2.0V reference)

### 3. Iridium 9603N Modem
**Built into AGT - No external connections needed**

| AGT Pin | Signal | Description |
|---------|--------|-------------|
| GPIO24 | TX1 | Serial TX to Iridium |
| GPIO25 | RX1 | Serial RX from Iridium |
| GPIO17 | ON/OFF | Iridium sleep control |
| GPIO18 | Net Avail | Network available input |
| GPIO22 | PWR_EN | Iridium power enable |
| GPIO27 | Supercap EN | LTC3225 charger enable |
| GPIO28 | PGOOD | Supercap charge status |
| GPIO41 | Ring | Ring indicator input |

### 4. Meshtastic RAK4603

**Connects to J10 Qwiic connector (I2C Port 4)**

| RAK4603 Pin | AGT Pin | J10 Pin | Signal | Wire Color Suggestion |
|-------------|---------|---------|--------|----------------------|
| RX | GPIO39 (D39) | Pin 1 (SCL4) | UART0 TX from AGT | Yellow |
| TX | GPIO40 (D40) | Pin 2 (SDA4) | UART0 RX to AGT | Orange |
| VCC | 3.3V | Pin 3 | Power | Red |
| GND | GND | Pin 4 | Ground | Black |

**Connection Point:** J10 Qwiic connector on AGT (4-pin JST connector)

**Baud Rate:** 115200

**Notes:**
- D39/D40 are repurposed I2C pins configured as UART0
- Cross TX/RX: RAK4603 TX → AGT RX (D40), RAK4603 RX → AGT TX (D39)
- MeshtasticSerial is configured in firmware as UART instance 0 on these pins
- RAK4603 must be configured in PROTO mode: `meshtastic --set serial.mode PROTO`

### 5. ArduPilot Navigator

| Navigator | AGT | Connection |
|-----------|-----|------------|
| USB | USB | USB cable (Type-C or Micro) |

**Baud Rate: 57600** (MAVLink standard)

**Note:** Navigator receives GPS via MAVLink protocol over USB serial.

### 6. NeoPixel LED Strip (WS2812B / WS2812)

| LED Strip | AGT Pin | Signal | Notes |
|-----------|---------|--------|-------|
| DIN | GPIO32 | Data | WS2812 data input |
| VCC | 5V | Power | External 5V supply |
| GND | GND | Ground | Common ground with AGT |

**Important Power Considerations:**
- 30 LEDs × 60mA max = 1.8A maximum current
- Do NOT power from AGT's onboard regulator
- Use external 5V power supply (2A+ recommended)
- Connect grounds together (AGT GND to LED GND)
- Add 470Ω resistor on data line if needed
- Add 1000µF capacitor across LED strip power

### 7. Relay Module 1 (Power Management)

| Relay Module | AGT Pin | Signal | Notes |
|--------------|---------|--------|-------|
| VCC | 3.3V | Power | Or 5V if relay requires |
| GND | GND | Ground | |
| IN | GPIO4 | Control | Active HIGH by default |

**Load:** Navigator/Pi, Camera, Lights power control

**Control Logic:**
- HIGH = Relay ON = Systems powered
- LOW = Relay OFF = Systems shut down (power save mode)
- Automatically controlled by battery voltage
- Conserves power during extended surface recovery wait

### 8. Relay Module 2 (Timed Event - Drop Weight)

| Relay Module | AGT Pin | Signal | Notes |
|--------------|---------|--------|-------|
| Signal IN | GPIO35 (AD35) | Control | 3.3V trigger signal |
| Relay Coil VCC | Battery + | Power | 12-14.8V from 4S LiPo |
| Relay Coil GND | Battery - | Ground | High voltage ground |
| Load | Electrolytic Release | Switched | Battery voltage to release mechanism |

**Load:** Electrolytic/galvanic drop weight ballast release mechanism

**Control Logic:**
- Triggered via configuration (GMT time or delay from deployment)
- Activates for configured duration (typically 1200+ seconds / 20+ minutes)
- Extended activation required for electrolytic dissolution
- One-shot activation for ballast release
- Critical for mission success (surface recovery)

**IMPORTANT:**
- GPIO35 provides LOW POWER 3.3V trigger signal only
- Relay coil must be powered by BATTERY VOLTAGE (12-14.8V)
- Relay switches high current battery voltage to electrolytic release
- Use relay rated for extended activation and high current

## Power System

```
Battery (4S LiPo)
    │
    ├──► Blue Robotics PSM ──► AGT Main Power
    │                          (11-16.8V)
    │
    └──► 5V Buck Converter ──► NeoPixel Strip
                               (2A+ capacity)
```

### Battery Specifications
- Type: 4S LiPo (Lithium Polymer)
- Voltage Range: 14.8V nominal (11.0V - 16.8V)
- Capacity: Based on deployment duration
- Monitor via PSM

### Power Budget (Approximate)

| Component | Current Draw | Notes |
|-----------|--------------|-------|
| AGT (Artemis) | 5-10mA | Sleep mode |
| AGT (Active) | 50-100mA | GPS + processing |
| Iridium TX | 1.5A peak | During transmission |
| GPS | 30-50mA | Continuous |
| NeoPixels | 0-1800mA | Depends on brightness |
| RAK4603 | 10-100mA | Depends on mode |
| Relays | 20-50mA each | When energized |
| PSM | 5mA | Monitoring |

**Total Active:** 2-4A peak (during Iridium TX with full LEDs)

## I2C Bus Configuration

```
AGT I2C Bus (400kHz) - GPIO8/9
├── GPS (ZOE-M8Q) - 0x42
└── PHT Sensor (MS8607) - 0x40, 0x76 (if installed)

IMPORTANT NOTES:
- PSM uses analog pins (GPIO11/12), NOT I2C!
- J10 Qwiic connector (GPIO39/40) is repurposed for UART0 (Meshtastic)
- Only I2C Port 1 (GPIO8/9) is used for actual I2C devices
```

## Serial Port Summary

| Port | Baud | Purpose | Connected Device | Pins |
|------|------|---------|------------------|------|
| Serial (USB) | 115200/57600 | Debug/Config/MAVLink | Navigator | USB |
| Serial1 (UART1) | 19200 | Iridium | 9603N Modem | GPIO24/25 (built-in) |
| MeshtasticSerial (UART0) | 115200 | Mesh Network | RAK4603 | GPIO39/40 (J10 Qwiic) |

## GPIO Usage Summary

| GPIO | Function | Direction | Notes |
|------|----------|-----------|-------|
| 4 | Relay 1 Control | Output | Power management |
| 8 | GPS SCL | I2C | Built-in |
| 9 | GPS SDA | I2C | Built-in |
| 10 | Geofence Alert | Input | From GPS |
| 11 | PSM Voltage | Analog In | Battery voltage sensing |
| 12 | PSM Current | Analog In | Battery current sensing |
| 13 | Bus Voltage | Analog In | Voltage monitor |
| 17 | Iridium Sleep | Output | Sleep control |
| 18 | Iridium Net Avail | Input | Network status |
| 19 | White LED | Output | Onboard LED |
| 22 | Iridium Power | Output | Power enable |
| 24 | Iridium TX | Serial1 | To modem |
| 25 | Iridium RX | Serial1 | From modem |
| 26 | GPS Enable | Output | Active LOW |
| 27 | Supercap Enable | Output | Charger control |
| 28 | Supercap PGOOD | Input | Charge status |
| 32 | NeoPixel Data | Output | LED strip |
| 34 | Bus Volt Enable | Output | Voltage monitor |
| 35 | Relay 2 Control | Output | Drop weight release |
| 39 | Meshtastic TX | UART0 | D39 on J10 (repurposed I2C SCL4) |
| 40 | Meshtastic RX | UART0 | D40 on J10 (repurposed I2C SDA4) |
| 41 | Iridium Ring | Input | Ring indicator |

## Assembly Tips

### 1. Start with Core Systems
1. Power up AGT alone and test
2. Add GPS antenna and verify fix
3. Add Iridium antenna and test transmission
4. Add peripherals one at a time

### 2. I2C Bus and Analog Connections
- PSM uses ANALOG outputs (GPIO11/12), not I2C - use simple wires
- J10 Qwiic connector is used for Meshtastic UART, not I2C
- Keep I2C wires short (<1m) for GPS and PHT sensor
- Add pull-ups if bus is long or has many devices

### 3. Serial Connections
- Always cross TX/RX (TX → RX, RX → TX)
- Verify logic levels (3.3V)
- Test each serial device independently

### 4. Power Distribution
- Use proper wire gauge for current
- Place decoupling capacitors near devices
- Monitor battery voltage continuously
- Test under full load

### 5. LED Strip
- Mount far from antennas to avoid interference
- Use proper 5V regulator with adequate current
- Add large capacitor at strip power connection
- Consider dimming for battery life

### 6. Relays
- Use optoisolated relays for high voltage loads
- Add flyback diodes for inductive loads
- Verify relay coil voltage matches supply
- Size relay contacts for load current

## Testing Checklist

- [ ] Power system provides stable voltage
- [ ] GPS acquires fix outdoors
- [ ] Iridium supercap charges (PGOOD high)
- [ ] Iridium sends test message
- [ ] PSM reads battery voltage/current
- [ ] NeoPixels display status
- [ ] Meshtastic communicates
- [ ] Navigator receives MAVLink GPS
- [ ] Relay 1 switches with battery voltage
- [ ] Relay 2 triggers on timed event
- [ ] Configuration saves to EEPROM
- [ ] System survives power cycle

## Safety Notes

⚠️ **IMPORTANT SAFETY CONSIDERATIONS:**

1. **Iridium Supercapacitor**: Can store significant energy. Handle carefully.
2. **Battery**: Use proper 4S LiPo charging and safety practices.
3. **High Current**: Ensure wiring can handle peak currents safely.
4. **Relay Loads**: Follow electrical codes for switched loads.
5. **Waterproofing**: Use appropriate enclosure for marine deployment.
6. **Antennas**: Keep clear of metallic objects and other antennas.

## Troubleshooting Wire Connections

| Symptom | Check |
|---------|-------|
| GPS not working | Antenna connected, GPS_EN LOW, I2C bus on GPIO8/9 |
| Iridium fails | Supercap charged, antennas clear, power adequate |
| No serial from RAK4603 | TX/RX crossed (D39→RX, D40→TX), common ground, baud 115200, PROTO mode |
| PSM reads zero | Analog connections on GPIO11/12, calibration values, PSM powered |
| NeoPixels dark | Data pin GPIO32, 5V power, ground connection |
| Relays don't switch | Control pin, relay coil voltage, ground, active HIGH/LOW |

## Reference Documents

- [AGT Hardware Overview](../SparkFun_Artemis_Global_Tracker/Documentation/Hardware_Overview/README.md)
- [AGT Pin Definitions](../SparkFun_Artemis_Global_Tracker/Documentation/Hardware_Overview/ARTEMIS_PINS.md)
- [AGT Schematic](../SparkFun_Artemis_Global_Tracker/Documentation/SparkFun_Artemis_Global_Tracker_SCHEMATIC_v10.pdf)
- Blue Robotics PSM Manual
- RAK4603 Datasheet
- ArduPilot Navigator Documentation
