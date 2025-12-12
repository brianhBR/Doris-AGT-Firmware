# Artemis Global Tracker - Pinout Guide

Visual reference for connecting peripherals to the Doris AGT firmware.

## Board Overview

![AGT Top View](../SparkFun_Artemis_Global_Tracker/Hardware/Artemis_Global_Tracker_TOP_VIEW.png)

## Quick Reference Table

| Connection | Location | Pins | Notes |
|------------|----------|------|-------|
| **Meshtastic RAK4603** | J10 (Qwiic) | D39/D40 | UART0 on I2C pins |
| **PSM Voltage** | GPIO Header | GPIO11 (AD11) | Analog input |
| **PSM Current** | GPIO Header | GPIO12 (AD12) | Analog input |
| **Relay 1 (Power)** | SPI Header | GPIO4 (CS1) | Navigator/Pi/Camera/Lights |
| **Relay 2 (Drop Weight)** | SPI Header | GPIO35 (CS2) | Ballast release |
| **NeoPixel Strip** | GPIO Header | GPIO32 (AD32) | 30 LED WS2812B strip |

## Detailed Connection Diagrams

---

### 1. Meshtastic RAK4603 Connection (J10 Qwiic Connector)

**Location:** J10 - Top center of board (Qwiic connector)

```
J10 Qwiic Connector Pinout (looking at board):
┌─────────────────┐
│  1   2   3   4  │
│ TX  RX  VCC GND │
└─────────────────┘
```

**Wiring:**
```
AGT J10          →  RAK4603
────────────────────────────
Pin 1 (D39/SCL4) →  RX
Pin 2 (D40/SDA4) →  TX
Pin 3 (3.3V)     →  VCC
Pin 4 (GND)      →  GND
```

**Pin Functions:**
- **D39** - UART0 TX (also SCL4 for I2C Port 4)
- **D40** - UART0 RX (also SDA4 for I2C Port 4)
- **3.3V** - Power supply (max 600mA from regulator)
- **GND** - Common ground

**Configuration:**
```cpp
UART MeshtasticSerial(0, 39, 40);  // UART0 on pins D39/D40
MeshtasticSerial.begin(115200);
```

**RAK4603 Setup:**
```bash
meshtastic --set serial.mode PROTO
meshtastic --set serial.enabled true
meshtastic --set serial.baud BAUD_115200
meshtastic --commit
```

---

### 2. Blue Robotics PSM (Power Sense Module)

**Location:** GPIO header (right side of board)

#### Voltage Sensing (GPIO11)

```
AGT GPIO Header    PSM
─────────────────────────
GPIO11 (AD11)  ←  V OUT (analog voltage)
GND            -  GND
```

**Calibration:**
- PSM voltage divider: 11.0 V/V
- Artemis ADC: 14-bit, 2.0V reference
- Formula: `voltage = (ADC_value / 16383.0) * 2.0 * 11.0`

#### Current Sensing (GPIO12)

```
AGT GPIO Header    PSM
─────────────────────────
GPIO12 (AD12)  ←  I OUT (analog current)
GND            -  GND
```

**Calibration:**
- PSM current ratio: 37.8788 A/V
- Offset: 0.330V
- Formula: `current = ((ADC_value / 16383.0) * 2.0 - 0.330) * 37.8788`

**Configuration:**
```cpp
#define PSM_VOLTAGE_PIN  11  // GPIO11 (AD11)
#define PSM_CURRENT_PIN  12  // GPIO12 (AD12)

uint16_t voltageADC = analogRead(PSM_VOLTAGE_PIN);
uint16_t currentADC = analogRead(PSM_CURRENT_PIN);
```

---

### 3. Relay Connections

#### Relay 1 - Power Management (GPIO4)

**Location:** SPI header - CS1 pin

```
AGT SPI Header     Relay Module
────────────────────────────────
GPIO4 (CS1)    →  IN/Signal
3.3V or 5V     →  VCC
GND            →  GND
```

**Controls:** Navigator/Pi, Camera, Lights
**Active:** HIGH (3.3V/5V triggers relay)
**States:**
- PREDEPLOYMENT: ON
- MISSION: ON
- RECOVERY: OFF
- EMERGENCY: OFF

#### Relay 2 - Drop Weight Release (GPIO35)

**Location:** SPI header - CS2 pin

```
AGT SPI Header     Relay Module
────────────────────────────────
GPIO35 (CS2)   →  IN/Signal
3.3V or 5V     →  VCC
GND            →  GND
```

**Controls:** Electrolytic/galvanic ballast release
**Active:** HIGH (3.3V/5V triggers relay)
**Duration:** Configured (typically 1200+ seconds for electrolytic release)
**Trigger:** Programmed time (GMT or delay) or emergency

**Configuration:**
```cpp
#define RELAY_POWER_MGMT   4   // GPIO4 - CS1
#define RELAY_TIMED_EVENT  35  // GPIO35 - CS2

pinMode(RELAY_POWER_MGMT, OUTPUT);
pinMode(RELAY_TIMED_EVENT, OUTPUT);
```

---

### 4. NeoPixel LED Strip (GPIO32)

**Location:** GPIO header - GPIO32/AD32

```
AGT GPIO Header    WS2812B Strip
────────────────────────────────
GPIO32 (AD32)  →  DIN (Data In)
-              -  5V (external power)
GND            →  GND
```

**IMPORTANT:**
- NeoPixels require **external 5V power supply**
- Do NOT power 30 LEDs from AGT 3.3V regulator
- Connect AGT GND to LED strip GND (common ground)
- Data signal is 3.3V (compatible with most WS2812B)

**Strip Configuration:**
```cpp
#define NEOPIXEL_PIN        32
#define NEOPIXEL_COUNT      30
#define NEOPIXEL_BRIGHTNESS 50  // 0-255

Adafruit_NeoPixel pixels(NEOPIXEL_COUNT, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);
```

**Power Requirements:**
- 30 LEDs @ 20mA each = 600mA max
- Typical: 200-300mA with brightness = 50
- Use 5V 1A+ power supply

---

## Complete Wiring Diagram

```
┌─────────────────────────────────────────────────────────────┐
│         Artemis Global Tracker (Top View)                   │
│                                                              │
│  ┌─────────┐                               ┌──────────┐     │
│  │   USB   │                               │ Antenna  │     │
│  └────┬────┘                               └────┬─────┘     │
│       │                                         │           │
│  Navigator/Pi                              (Maxtena)        │
│   (Serial)                                                  │
│                                                              │
│  ┌──────────────┐                    ┌──────────────┐       │
│  │ J10 (Qwiic)  │                    │  SPI Header  │       │
│  │              │                    │              │       │
│  │ 1. D39 (TX)──┼───► RAK4603 RX    │ CS1 (GPIO4)──┼───► Relay 1 │
│  │ 2. D40 (RX)──┼───► RAK4603 TX    │ CS2 (GPIO35)─┼───► Relay 2 │
│  │ 3. 3.3V  ────┼───► VCC            │ MISO         │       │
│  │ 4. GND   ────┼───► GND            │ MOSI         │       │
│  └──────────────┘                    │ SCK          │       │
│                                      └──────────────┘       │
│  ┌──────────────────────────────────────────────┐           │
│  │          GPIO Header (Right Side)            │           │
│  │                                               │           │
│  │  GPIO11 (AD11) ──► PSM Voltage Out           │           │
│  │  GPIO12 (AD12) ──► PSM Current Out           │           │
│  │  GPIO32 (AD32) ──► NeoPixel Data In          │           │
│  │  GND           ──► Common Ground              │           │
│  └──────────────────────────────────────────────┘           │
│                                                              │
│  Onboard Components:                                        │
│  • GPS (ZOE-M8Q) - Internal, I2C                           │
│  • Iridium (9603N) - Internal, Serial1                     │
│  • MS8607 (Pressure/Humidity/Temp) - Internal, I2C        │
└─────────────────────────────────────────────────────────────┘
```

---

## Physical Connector Locations

### Top View Reference

Using the image above as reference:

1. **USB-C** - Left edge, middle
2. **Antenna SMA** - Right edge, upper
3. **J10 (Qwiic)** - Top center (4-pin connector)
4. **SPI Header** - Top right (6 pins)
5. **GPIO Header** - Right side (multiple pins)
6. **Battery JST** - Bottom left

### GPIO Header Pinout (Right Side)

```
Looking at board from top, right edge:

┌─────────┐
│  GPIO2  │
│  GPIO4  │  ← Relay 1 (also on SPI CS1)
│  GPIO5  │
│  GPIO6  │  (MISO - can use for custom)
│  GPIO7  │  (MOSI - can use for custom)
│  GPIO8  │  (SCL - I2C Port 1)
│  GPIO9  │  (SDA - I2C Port 1)
│  GPIO10 │  (Geofence)
│  GPIO11 │  ← PSM Voltage
│  GPIO12 │  ← PSM Current
│  ...     │
│  GPIO32 │  ← NeoPixel Data
│  ...     │
│  GPIO35 │  ← Relay 2 (also on SPI CS2)
│  GND    │
│  3.3V   │
└─────────┘
```

---

## External Connections Summary

### Required for Doris Drop Camera:

| Device | AGT Connection | Power | Notes |
|--------|---------------|-------|-------|
| **Navigator/Pi** | USB-C | Via Relay 1 | MAVLink communication |
| **RAK4603** | J10 Qwiic | 3.3V from J10 | Meshtastic mesh |
| **PSM** | GPIO11, GPIO12 | Independent | Battery monitoring |
| **Relay 1** | GPIO4 (CS1) | External 5V/12V | High-current relay |
| **Relay 2** | GPIO35 (CS2) | External 5V/12V | Drop weight |
| **NeoPixels** | GPIO32 | External 5V | 30 LED strip |
| **Antenna** | SMA | - | Maxtena M1600HCT |
| **Battery** | JST | 4S LiPo or similar | Main power |

### Internal (Onboard):
- GPS (ZOE-M8Q) - No external wiring
- Iridium (9603N) - No external wiring
- MS8607 (PHT sensor) - No external wiring

---

## Power Budget

| Component | Current Draw | Notes |
|-----------|-------------|-------|
| AGT (sleep) | ~50µA | Ultra low power |
| AGT (active) | ~100mA | GPS + processing |
| Iridium TX | ~145mA avg, 1.3A peak | Supercaps handle peak |
| RAK4603 | ~100mA TX, ~20mA RX | Mesh radio |
| NeoPixels | ~300mA @ brightness 50 | 30 LEDs |
| Navigator/Pi | ~500mA - 2A | Via Relay 1 |

**Total (all active):** ~1-3A depending on Navigator load

**Recommended Battery:** 4S LiPo, 5000-10000mAh for 24hr+ mission

---

## Safety Notes

⚠️ **CRITICAL - Antenna Switch:**
- GPS and Iridium share antenna via RF switch
- **NEVER enable both simultaneously**
- Firmware prevents this automatically
- Bad things happen to AS179 switch if violated

⚠️ **Relay Power:**
- Use proper relay modules rated for your load
- Relay 1 must handle Navigator/Pi + Camera + Lights
- Isolate high voltage/current loads from AGT

⚠️ **NeoPixel Power:**
- Do NOT power 30 LEDs from AGT 3.3V regulator
- Use separate 5V power supply
- Connect common ground

⚠️ **Electrolytic Release:**
- Relay 2 timing for electrolytic release: 1200+ seconds
- Test timing before deployment
- Have backup release mechanism

---

## Testing Checklist

Before deployment, verify:

- [ ] RAK4603 communicates (check serial output)
- [ ] PSM voltage/current readings correct
- [ ] Relay 1 switches Navigator/Pi power
- [ ] Relay 2 activates drop weight mechanism
- [ ] NeoPixels show status correctly
- [ ] GPS acquires fix
- [ ] Iridium transmits position
- [ ] All grounds connected (common ground)
- [ ] No shorts between power rails
- [ ] Battery charged and connected
- [ ] Antenna secured

---

## Additional Resources

- [SparkFun AGT Hardware Overview](../SparkFun_Artemis_Global_Tracker/Documentation/Hardware_Overview/README.md)
- [Artemis Pin Definitions](../SparkFun_Artemis_Global_Tracker/Documentation/Hardware_Overview/ARTEMIS_PINS.md)
- [AGT Schematic](../agt_schematic.pdf)
- [Wiring Diagram](WIRING_DIAGRAM.md) - Detailed technical wiring
- [Quick Start Guide](QUICK_START.md)
