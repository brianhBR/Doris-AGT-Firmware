# Artemis Global Tracker - Pinout Guide

Visual reference for connecting peripherals to the Doris AGT firmware.

## Assembly Pinout (one-page reference)

Use this table while wiring. All pin numbers are **Artemis GPIO (D)**. Cross-reference with the board label (e.g. D4, AD32).

| GPIO | Board label | Function | Direction | Connect to |
|------|-------------|----------|-----------|------------|
| **4** | D4 | Payload power | OUT | Default: NC relay IN. `pololu` build: Pololu OFF pulse |
| **11** | AD11 | PSM voltage | Analog IN | PSM V_OUT |
| **12** | AD12 | PSM current | Analog IN | PSM I_OUT |
| **32** | AD32 | NeoPixel data | OUT | WS2812B strip DIN (30 LEDs, external 5V) |
| **35** | AD35 | Pololu ON, or unused | OUT | `pololu` build only. Default build: leave open. Never the ballast release |
| **39** | J10 pin 1 (SCL4) | Meshtastic TX (NMEA) | OUT | RAK J10 RX (external GPS UART) |
| **40** | J10 pin 2 (SDA4) | Meshtastic RX | IN | RAK J10 TX (optional) |
| — | J10 pin 3 | 3.3V | — | RAK VCC (if powering from AGT) |
| — | J10 pin 4 | GND | — | RAK GND |

**Onboard (no wiring):** GPS (ZOE-M8Q) on I2C; Iridium 9603N on Serial1 (D24/D25).  
**USB:** Debug + MAVLink to Navigator (single USB-C, 57600 baud).

**Notes:**
- **J10:** AGT TX (39) → RAK RX. Baud **9600** (NMEA via SoftwareSerial). Configure RAK for external GPS on J10.
- **Release:** Connect only to the Navigator relay. GPIO35 is not a release output.
- **Payload power:** Default firmware drives a normally-closed relay from GPIO4. The `pololu` build pulses Pololu ON from GPIO35 and Pololu OFF from GPIO4. Do not connect CTRL.
- **NeoPixels:** Data from GPIO32 only; **power strip from external 5 V** (do not use AGT 3.3 V).
- **PSM:** GND and analog only; PSM powered from battery sense side.

---

## Board Overview

![AGT Top View](../SparkFun_Artemis_Global_Tracker/Hardware/Artemis_Global_Tracker_TOP_VIEW.png)

## Quick Reference Table

| Connection | Location | Pins | Notes |
|------------|----------|------|-------|
| **Meshtastic RAK4603** | J10 (Qwiic I2C Port 4) | D39/D40 | NMEA GPS out (TX) to RAK J10 (external GPS), 9600 baud via SoftwareSerial |
| **PSM Voltage** | Breakout Pins | GPIO11 (AD11) | Analog input |
| **PSM Current** | Breakout Pins | GPIO12 (AD12) | Analog input |
| **Relay 1 (Power)** | Breakout Pins | GPIO4 (D4) | Default NC relay: Navigator/Pi/Camera/Lights |
| **Pololu 2813** | Breakout Pins | GPIO35 ON, GPIO4 OFF | `pololu` build only. Leave CTRL open |
| **Release relay** | Navigator | Navigator relay output | Ballast release |
| **NeoPixel Strip** | Breakout Pins | GPIO32 (AD32) | 30 LED WS2812B strip |

## Detailed Connection Diagrams

---

### 1. Meshtastic RAK4603 Connection (J10 Qwiic Connector)

**Location:** J10 - Qwiic connector labeled "I2C Port 4" on the TOP_VIEW diagram

The AGT outputs **NMEA 0183** GPS sentences to Meshtastic's **J10** (UART1 / external GPS port on the RAK board). The RAK treats the AGT as an external GPS source.

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
AGT J10             →  RAK4603 J10 (external GPS UART)
──────────────────────────────────────────────────────
Pin 1 D39 (TX)      →  RAK J10 RX  (NMEA GPS in)
Pin 2 D40 (RX)      →  RAK J10 TX  (optional)
Pin 3 (3.3V)        →  VCC
Pin 4 (GND)         →  GND
```

**Pin Functions:**
- **GPIO39 (D39)** - **AGT TX** → NMEA sentences (GGA, RMC) to RAK J10 RX
- **GPIO40 (D40)** - **AGT RX** → optional (e.g. from RAK J10 TX)
- **3.3V** - Power supply (max 600mA from regulator)
- **GND** - Common ground

**Configuration:**
- **Baud:** 9600 (SoftwareSerial, configured in config.h as `MESHTASTIC_BAUD`)
- **Format:** Standard NMEA 0183 (GPGGA, GPRMC); RAK expects external GPS on J10

**RAK4603 / Meshtastic setup:** Configure the device to use **external GPS** on the J10 UART (UART1). No PROTO mode needed—the RAK just reads NMEA from that port. The AGT uses SoftwareSerial because both hardware UARTs are occupied (USB + Iridium).

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
- PRE_MISSION: ON
- SELF_TEST: ON
- MISSION: ON
- RECOVERY: ON until sustained surface qualification + BlueOS ACK + final grace

This is the default production wiring. GPIO4 LOW or floating leaves the coil
off, the NC contact closed, and the payload powered.

#### Pololu 2813 (optional `pololu` build)

Use this instead of the NC relay module, and flash `pio run -e pololu`. Do not
mix the two drivers on one image: the default firmware holds GPIO4 as a coil
output and will not generate Pololu pulses.

```
AGT Breakout              Pololu 2813
────────────────────────────────────────
GPIO35 (AD35)         →   ON
GPIO4 (D4)            →   OFF
GND                   →   GND
(unconnected)             CTRL
```

ON and OFF are active-high pulses (`PAYLOAD_POWER_PULSE_MS`, 100 ms). Both AGT
pins idle low. A high level above 1 V is enough; the Artemis drives 3.3 V.
Leave CTRL disconnected. Do not hold ON or OFF high.

- The Pololu latch retains state across an AGT USB reset or firmware flash if Pololu VIN remains continuously powered.
- The Pololu typically defaults OFF after its own VIN is removed and reapplied.
- Firmware startup pulses ON.
- As with the current NC relay, an AGT reboot after surface cutoff will restore payload power.
- There is no hardware readback confirming the Pololu's physical state.

AD35 is safe for this. On the AGT schematic it is net `ARTEMIS_D35`: the
Artemis pad to SPI header pin 5 only, with no onboard part and no pull-up.
This firmware does not start SPI. Iridium UART remains on GPIO24/25. The
hookup guide labels that header pin SPI CS1; SparkFun examples call the same
pad `spiCS2`. Either name is the bare breakout, not a device on the board.

#### Drop Weight Release (Navigator Only)

Wire the ballast release to the Navigator relay configured by the Doris frame.
Do not connect it to AGT GPIO35. The default build does not configure GPIO35.
The `pololu` build configures GPIO35 only as the payload ON pulse. Neither
build consumes Lua's `RELAY` named value or provides a release output.

**Configuration:**
```cpp
#define RELAY_POWER_MGMT       4   // GPIO4 - NC coil, or Pololu OFF
#define PAYLOAD_POWER_ON_PIN   35  // GPIO35 - Pololu ON
#define PAYLOAD_POWER_PULSE_MS 100
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
│  │ SoftwareSerial NMEA  │                                  │
│  │                      │                                  │
│  │ 1. D39 (TX) NMEA ────┼───► RAK J10 RX (external GPS)   │
│  │ 2. D40 (RX) ─────────┼───► RAK J10 TX (optional)      │
│  │ 3. 3.3V          ────┼───► VCC                          │
│  │ 4. GND           ────┼───► GND                          │
│  └──────────────────────┘                                  │
│                                                             │
│  Breakout Pins (see TOP_VIEW for locations):               │
│  ┌───────────────────────────────────────┐                 │
│  │  D4 (GPIO4)    ───► NC relay / Pololu OFF │             │
│  │  AD35 (GPIO35) ───► Pololu ON, else open │             │
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

**For payload power:**
- D4 (GPIO4) - default NC relay coil, or Pololu OFF in the `pololu` build
- AD35 (GPIO35) - Pololu ON in the `pololu` build; leave open on the default build
- Navigator relay output - ballast release

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
| **Release relay** | Navigator output | As required by actuator | Drop weight |
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
- Size and power the Navigator release relay for the selected actuator.
- Isolate high voltage/current loads from AGT

⚠️ **NeoPixel Power:**
- Do NOT power 30 LEDs from AGT 3.3V regulator
- Use separate 5V power supply
- Connect common ground

⚠️ **Electrolytic Release:**
- Validate Navigator/Lua activation timing before deployment.
- Have backup release mechanism

---

## Testing Checklist

Before deployment, verify:

- [ ] RAK4603 receives NMEA on J10 (external GPS); node shows position
- [ ] PSM voltage/current readings correct
- [ ] Relay 1 switches Navigator/Pi power
- [ ] Navigator relay activates drop weight mechanism
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
