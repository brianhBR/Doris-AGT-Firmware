# Meshtastic Interface — NMEA GPS Output

## Overview

The AGT outputs **standard NMEA 0183 GPS sentences** to the Meshtastic RAK4603 via SoftwareSerial. The RAK treats the AGT as an **external GPS source** on its J10 connector (UART1). No protobuf encoding, no AT commands, no serial module configuration — the RAK just reads NMEA and uses the position for its mesh node.

## How It Works

```
AGT (SoftwareSerial TX, pin D39)  ──NMEA──►  RAK4603 J10 RX (external GPS UART)
                                              │
                                              ▼
                                     RAK reads NMEA GGA/RMC
                                     Uses position for mesh node
                                     Other Meshtastic nodes see position on map
```

1. The AGT reads GPS from the onboard ZOE-M8Q (I2C)
2. Formats the position as standard NMEA sentences (GPGGA, GPRMC)
3. Sends them over SoftwareSerial at 9600 baud to the RAK's J10 connector
4. The RAK4603 treats J10 as its GPS input and uses the position

## Wiring

| AGT Pin | J10 Pin | RAK4603 | Signal |
|---------|---------|---------|--------|
| D39 (GPIO39) | Pin 1 (SCL4) | J10 RX | AGT TX → NMEA GPS sentences |
| D40 (GPIO40) | Pin 2 (SDA4) | J10 TX | AGT RX (optional, not used) |
| 3.3V | Pin 3 | VCC | Power |
| GND | Pin 4 | GND | Ground |

The J10 connector on the AGT is a standard Qwiic 4-pin JST connector (I2C Port 4), repurposed as a UART output.

## Serial Configuration

```cpp
// config.h
#define MESHTASTIC_TX_PIN    39   // D39 on J10
#define MESHTASTIC_RX_PIN    40   // D40 on J10
#define MESHTASTIC_BAUD      9600 // GPS baud rate
```

The firmware uses `SoftwareSerial` because the Apollo3 only has two hardware UARTs:
- **Serial** (USB) — debug + MAVLink to Navigator
- **Serial1** (UART1, D24/D25) — Iridium 9603N

SoftwareSerial on pins 39/40 provides the third serial channel for Meshtastic.

## NMEA Sentences

### GPGGA (Fix Data)

```
$GPGGA,120000.00,3742.1445,N,12205.0466,W,1,10,0.9,15.2,M,0.0,M,,*XX
```

Fields: time, latitude, N/S, longitude, E/W, fix quality, satellites, HDOP, altitude, units, geoid separation, units, DGPS age, DGPS station ID.

### GPRMC (Recommended Minimum)

```
$GPRMC,120000.00,A,3742.1445,N,12205.0466,W,0.0,0.0,150326,,,A*XX
```

Fields: time, status, latitude, N/S, longitude, E/W, speed (knots), course, date, magnetic variation, mode.

Both sentences include proper NMEA checksums.

### No-Fix Sentence

When the AGT has no GPS fix, it still sends an empty GPGGA to keep the UART active:

```
$GPGGA,000000.00,,,,,0,00,99.9,,,,,,,*XX
```

## RAK4603 Configuration

Configure the RAK4603 to use **external GPS** on its J10 port. The exact configuration depends on your Meshtastic firmware version, but the key setting is telling the RAK to read GPS data from J10 (UART1) instead of its internal GPS module.

No `serial.mode` configuration is needed — the RAK's J10 port is natively an external GPS UART input, not a Meshtastic serial module port.

## Update Interval

The NMEA output interval is controlled by `sysConfig.meshtasticInterval`:

- **Default:** 3000ms (3 seconds) — `DEFAULT_MESHTASTIC_INTERVAL`
- **Compile-time minimum:** `MESHTASTIC_UPDATE_MS` = 1000ms (1 second)
- Configurable via: `set_meshtastic_interval <seconds>` + `save`

## Testing

### Serial Commands

```
mesh_test          # Send "AGT test" text (stub, not sent as NMEA)
mesh_test_gps      # Send hardcoded test NMEA (37.7024, -122.0841)
mesh_send <text>   # Send custom text (stub, not sent as NMEA)
```

### Verifying NMEA Output

1. Connect a USB-serial adapter to AGT pin D39 (TX) at 9600 baud
2. You should see GPGGA and GPRMC sentences when GPS has a fix
3. When no fix, you'll see empty GPGGA sentences

### Verifying Meshtastic Reception

1. Wire AGT D39 → RAK J10 RX, common ground
2. Open the Meshtastic app on phone/computer
3. The RAK node should show a GPS position on the map
4. Position updates at the configured Meshtastic interval

## Implementation Details

The NMEA formatting avoids `%f` (not available on Apollo3 `snprintf`) and uses integer math to build latitude/longitude in `ddmm.mmmm` NMEA format.

At 9600 baud, bit timing is relaxed enough that SoftwareSerial works reliably without disabling interrupts (which would crash MbedOS RTOS on Apollo3).

## Limitations

- **TX-only** — AGT sends NMEA to RAK but does not read responses
- **GPS position only** — no telemetry, alerts, or custom messages over NMEA
- **No acknowledgment** — NMEA is fire-and-forget
- The `sendText`, `sendTelemetry`, `sendState`, and `sendAlert` functions exist in the API but are stubs (return false / no-op)

## Previous Implementations

The Meshtastic interface has gone through several iterations:
1. **AT commands** (v0.0.5) — never worked with Meshtastic
2. **Protobuf/PROTO mode** (v0.1.0) — complex, required nanopb library
3. **NMEA GPS output** (current) — simple, reliable, RAK treats AGT as external GPS

## References

- [Meshtastic External GPS Documentation](https://meshtastic.org/docs/hardware/gps/)
- [NMEA 0183 Sentence Format](https://gpsd.gitlab.io/gpsd/NMEA.html)
- [Pinout Guide](PINOUT_GUIDE.md) — J10 connector details
- [Wiring Diagram](WIRING_DIAGRAM.md) — full connection diagram
