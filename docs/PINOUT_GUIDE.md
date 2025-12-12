# Artemis Global Tracker - Pinout Guide

Visual reference for connecting peripherals to the Doris AGT firmware.

## Board Overview

![AGT Top View](../SparkFun_Artemis_Global_Tracker/Hardware/Artemis_Global_Tracker_TOP_VIEW.png)

## Quick Reference Table

| Connection | Location | Pins | Notes |
|------------|----------|------|-------|
| **Meshtastic RAK4603** | J10 (Qwiic I2C Port 4) | D39/D40 | UART1 TX/RX on I2C pins |
| **PSM Voltage** | Breakout Pins | GPIO11 (AD11) | Analog input |
| **PSM Current** | Breakout Pins | GPIO12 (AD12) | Analog input |
| **Relay 1 (Power)** | Breakout Pins | GPIO4 (D4) | Navigator/Pi/Camera/Lights |
| **Relay 2 (Drop Weight)** | Breakout Pins | GPIO35 (AD35) | Ballast release |
| **NeoPixel Strip** | Breakout Pins | GPIO32 (AD32) | 30 LED WS2812B strip |

## Detailed Connection Diagrams

---

### 1. Meshtastic RAK4603 Connection (J10 Qwiic Connector)

**Location:** J10 - Qwiic connector labeled "I2C Port 4" on the TOP_VIEW diagram

```
J10 Qwiic Connector Pinout (standard Qwiic 4-pin):
┌─────────────────┐
│  1   2   3   4  │
│SCL SDA VCC GND  │
│(D39)(D40)       │
└─────────────────┘
```

**Wiring:**
```
AGT J10             →  RAK4603
──────────────────────────────────
Pin 1 D39 (SCL4)    →  RX (UART RX)
Pin 2 D40 (SDA4)    →  TX (UART TX)
Pin 3 (3.3V)        →  VCC
Pin 4 (GND)         →  GND
```

**Pin Functions:**
- **GPIO39 (D39)** - Default: I2C Port 4 SCL | **Using as: UART1 TX**
- **GPIO40 (D40)** - Default: I2C Port 4 SDA | **Using as: UART1 RX**
- **3.3V** - Power supply (max 600mA from regulator)
- **GND** - Common ground

**Important:** We're repurposing the I2C Port 4 pins for UART1 serial communication. The Apollo3 chip allows these pins to function as either I2C or UART.

**Configuration:**
```cpp
// Create UART instance 0 using pins 39 (TX) and 40 (RX)
UART MeshtasticSerial(0, 39, 40);  // UART0 instance, TX=39, RX=40
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

**Location:** Breakout pins on TOP_VIEW (multiple pins broken out on board edges)

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

#### Relay 1 - Power Management (GPIO4/D4)

**Location:** Breakout pin labeled "D4" on TOP_VIEW

```
AGT Breakout       Relay Module
────────────────────────────────
GPIO4 (D4)     →  IN/Signal
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

#### Relay 2 - Drop Weight Release (GPIO35/AD35)

**Location:** Breakout pin labeled "AD35" on TOP_VIEW

```
AGT Breakout       Relay Module       Electrolytic Release
──────────────────────────────────────────────────────────
GPIO35 (AD35)  →  IN/Signal
Battery V+     →  VCC (relay coil)  →  Positive terminal
GND            →  GND                →  Negative terminal
```

**Controls:** Electrolytic/galvanic ballast release mechanism
**Active:** HIGH (3.3V signal triggers relay)
**Power Source:** Battery voltage (12-14.8V from 4S LiPo)
**Duration:** Configured (typically 1200+ seconds / 20+ minutes for electrolytic dissolution)
**Trigger:** Programmed time (GMT or delay) or emergency

**Important:**
- Relay coil powered by battery voltage (NOT 3.3V/5V)
- GPIO35 provides 3.3V signal to trigger relay
- Relay switches battery voltage to electrolytic release mechanism
- Requires high-current relay suitable for extended activation

**Configuration:**
```cpp
#define RELAY_POWER_MGMT   4   // GPIO4 - CS1
#define RELAY_TIMED_EVENT  35  // GPIO35 - CS2

pinMode(RELAY_POWER_MGMT, OUTPUT);
pinMode(RELAY_TIMED_EVENT, OUTPUT);
```

---

### 4. NeoPixel LED Strip (GPIO32/AD32)

**Location:** Breakout pin labeled "AD32" on TOP_VIEW

```
AGT Breakout       WS2812B Strip
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

**Reference the TOP_VIEW image above for exact pin locations**

```
┌─────────────────────────────────────────────────────────────┐
│         Artemis Global Tracker (Top View)                   │
│                                                              │
│  ┌─────────┐                               ┌──────────┐     │
│  │  USB-C  │                               │ Antenna  │     │
│  └────┬────┘                               │   SMA    │     │
│       │                                    └────┬─────┘     │
│  Navigator/Pi                                  │           │
│   (Serial/MAVLink)                        (Maxtena         │
│                                            M1600HCT)        │
│                                                             │
│  ┌──────────────────────┐                                  │
│  │ J10 (I2C Port 4)     │  Qwiic Connector                │
│  │ Repurposed as UART1  │                                  │
│  │                      │                                  │
│  │ 1. D39 (SCL4/TX1)────┼───► RAK4603 RX                  │
│  │ 2. D40 (SDA4/RX1)────┼───► RAK4603 TX                  │
│  │ 3. 3.3V          ────┼───► VCC                          │
│  │ 4. GND           ────┼───► GND                          │
│  └──────────────────────┘                                  │
│                                                             │
│  Breakout Pins (see TOP_VIEW for locations):               │
│  ┌───────────────────────────────────────┐                 │
│  │  D4 (GPIO4)    ───► Relay 1 (Power)  │                 │
│  │  AD35 (GPIO35) ───► Relay 2 (Drop)   │                 │
│  │  AD11 (GPIO11) ◄─── PSM Voltage       │                 │
│  │  AD12 (GPIO12) ◄─── PSM Current       │                 │
│  │  AD32 (GPIO32) ───► NeoPixel Data     │                 │
│  │  GND           ───  Common Ground     │                 │
│  │  3.3V          ───  Power             │                 │
│  └───────────────────────────────────────┘                 │
│                                                             │
│  Onboard Components (No External Wiring):                  │
│  • GPS (ZOE-M8Q) - I2C Port 1                             │
│  • Iridium (9603N) - Serial1 (D24/D25)                    │
│  • MS8607 (PHT sensor) - I2C Port 1                       │
└─────────────────────────────────────────────────────────────┘
```

---

## Physical Connector Locations

**All locations reference the TOP_VIEW image at the top of this document**

### Main Connectors on TOP_VIEW

1. **USB-C Connector** - Left side of board
2. **Antenna SMA Connector** - Right side of board
3. **J10 (I2C Port 4 / Qwiic)** - Labeled on TOP_VIEW (4-pin Qwiic connector)
4. **Battery JST Connector** - Labeled on TOP_VIEW

### Breakout Pins on TOP_VIEW

The TOP_VIEW image shows all breakout pins with labels. Key pins for this project:

**For Meshtastic:**
- J10 connector (I2C Port 4): D39, D40, 3.3V, GND

**For Relays:**
- D4 (GPIO4) - Relay 1
- AD35 (GPIO35) - Relay 2

**For PSM:**
- AD11 (GPIO11) - Voltage sensing
- AD12 (GPIO12) - Current sensing

**For NeoPixels:**
- AD32 (GPIO32) - Data line

**Power and Ground:**
- Multiple GND pins available
- Multiple 3.3V pins available

**Note:** Refer to the TOP_VIEW diagram to locate the exact physical position of each labeled pin on the board.

---

## External Connections Summary

### Required for Doris Drop Camera:

| Device | AGT Connection | Power | Notes |
|--------|---------------|-------|-------|
| **Navigator/Pi** | USB-C | Via Relay 1 | MAVLink communication |
| **RAK4603** | J10 Qwiic | 3.3V from J10 | Meshtastic mesh |
| **PSM** | GPIO11, GPIO12 | Independent | Battery monitoring |
| **Relay 1** | GPIO4 (D4) | External 5V/12V | High-current relay |
| **Relay 2** | GPIO35 (AD35) | Battery voltage (12-14.8V) | Drop weight, extended activation |
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
- Relay 1 must handle Navigator/Pi + Camera + Lights (typically 5V/12V)
- **Relay 2 (Drop Weight) uses battery voltage** (12-14.8V from 4S LiPo)
  - GPIO35 provides 3.3V trigger signal only
  - Relay coil and load powered from main battery
  - Must handle extended activation (20+ minutes)
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
