# Meshtastic Serial Protocol

## Overview

The current implementation uses **AT commands which is INCORRECT**. Meshtastic devices communicate using **Protocol Buffers (protobuf)** over serial.

## Correct Protocol

### Serial Mode: Protobuf

Meshtastic uses the **Client API** which communicates via Protocol Buffers over serial/USB.

**Key Points:**
- **NOT** AT command based
- Uses Google Protocol Buffers (binary format)
- Stream of `ToRadio` (to device) and `FromRadio` (from device) packets
- Same protocol across BLE, Serial, and TCP transports

### Framing Format

Each packet has a **4-byte header**:
- START1: `0x94`
- START2: `0xC3`
- MSB of packet length
- LSB of packet length

Followed by the protobuf-encoded packet payload.

### Message Flow

```
AGT → RAK4603:  ToRadio protobuf packets
RAK4603 → AGT:  FromRadio protobuf packets
```

## Implementation Options

Since implementing full protobuf encoding/decoding on the Artemis is complex, you have three options:

### Option 1: Text-Based Serial Module (RECOMMENDED)

Configure the RAK4603's Serial Module in **TEXT mode** or **TEXTMSG mode**:

**TEXT Mode:**
- Simple text in, text out
- RAK4603 handles Meshtastic protocol internally
- AGT sends plain text strings
- Meshtastic wraps them in messages

**Configuration (via Meshtastic CLI on RAK4603):**
```bash
meshtastic --set serial.enabled true
meshtastic --set serial.mode TEXTMSG
meshtastic --set serial.baud BAUD_115200
meshtastic --set serial.timeout 0
```

**AGT Implementation:**
```cpp
// Simply write text to serial
Serial2.println("POS:37.422408,-122.084108,15.2m,12sat");
```

RAK4603 automatically:
1. Receives the text
2. Wraps it in a Meshtastic TEXT_MESSAGE
3. Broadcasts over LoRa mesh
4. Other nodes receive and can display

**Advantages:**
- Simple implementation
- No protobuf library needed
- Easy to test and debug
- Text visible on other Meshtastic devices

**Disadvantages:**
- Text only (no structured data)
- Less efficient than binary
- No guaranteed delivery acknowledgment

---

### Option 2: Simple Protobuf (NMEA-Style)

Use Serial Module in **NMEA mode** which accepts GPS NMEA sentences:

**Configuration:**
```bash
meshtastic --set serial.mode NMEA
```

**AGT sends standard NMEA:**
```
$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47
```

RAK4603 automatically creates Position messages from NMEA.

**Advantages:**
- Standard GPS format
- Position handled natively by Meshtastic
- Shows on maps in Meshtastic apps

**Disadvantages:**
- GPS only (no custom telemetry)
- Must format valid NMEA sentences

---

### Option 3: Full Protobuf Implementation (COMPLEX)

Implement full protobuf encoding/decoding on AGT.

**Requirements:**
- Protocol Buffer library for Arduino
- Define Meshtastic protobuf messages
- Implement framing protocol
- Handle ToRadio/FromRadio packets

**Not recommended** due to:
- Complexity
- Memory constraints on Artemis
- Maintenance burden
- Library dependencies

---

## RECOMMENDED APPROACH

### Use Serial Module in TEXTMSG Mode

This is the simplest and most practical approach for your use case.

### RAK4603 Configuration

Before connecting to AGT, configure RAK4603 via Meshtastic CLI:

```bash
# Connect RAK4603 to computer via USB
meshtastic --set serial.enabled true
meshtastic --set serial.mode TEXTMSG
meshtastic --set serial.baud BAUD_115200
meshtastic --set serial.rxd 21   # RAK4603 RX pin (to AGT TX)
meshtastic --set serial.txd 22   # RAK4603 TX pin (to AGT RX)
meshtastic --set serial.timeout 0
meshtastic --set serial.echo false

# Save configuration
meshtastic --commit
```

### AGT Serial Communication

**Connection:**
- AGT GPIO6 (TX2) → RAK4603 RX (pin 21)
- AGT GPIO7 (RX2) → RAK4603 TX (pin 22)
- Baud: 115200
- Format: 8N1 (8 data bits, no parity, 1 stop bit)

**Message Format:**

Send plain text strings terminated with newline (`\n`):

```cpp
// Position message
Serial2.println("POS:37.422408,-122.084108,15.2m,12sat");

// Telemetry message
Serial2.println("TELEM:V=12.45,I=1.23,SOC=85");

// State message
Serial2.println("STATE:MISSION,time=3600");

// Custom message
Serial2.println("DROP WEIGHT RELEASED");
```

**Receiving Messages:**

RAK4603 can also forward received mesh messages to AGT:

```cpp
void MeshtasticInterface_update() {
    while (Serial2.available()) {
        String msg = Serial2.readStringUntil('\n');
        // Process received mesh message
        Serial.print("Mesh RX: ");
        Serial.println(msg);
    }
}
```

### Message Format Recommendations

**Position Messages:**
```
Format: POS:<lat>,<lon>,<alt>m,<sats>sat
Example: POS:37.422408,-122.084108,15.2m,12sat
```

**State Messages:**
```
Format: STATE:<state>,<time_in_state>s
Example: STATE:MISSION,3600s
Example: STATE:RECOVERY,7200s
Example: STATE:EMERGENCY,DROP_WEIGHT_RELEASED
```

**Telemetry Messages:**
```
Format: TELEM:<key>=<value>[,<key>=<value>...]
Example: TELEM:depth=2850m,temp=4.2C
Example: TELEM:batt=12.4V,soc=85%
```

**Alert Messages:**
```
Format: ALERT:<message>
Example: ALERT:DROP_WEIGHT_RELEASED
Example: ALERT:EMERGENCY_DEPTH
Example: ALERT:SURFACE_GPS_FIX
```

### Advantages of Text Format

1. **Human Readable**: Can see messages in Meshtastic apps
2. **Easy Debug**: Can monitor with serial terminal
3. **Simple Implementation**: No protobuf library needed
4. **Flexible**: Can send any text data
5. **Compatible**: Works with all Meshtastic devices

### Limitations

1. **No ACK**: No confirmation message was received
2. **Text Only**: Not as efficient as binary
3. **No Encryption**: Meshtastic encrypts mesh traffic, but serial is plain
4. **One-Way**: Primarily AGT → Mesh (receiving possible but not structured)

## Updated Implementation

I'll create an updated Meshtastic interface module that uses TEXT mode:

```cpp
// Simplified text-based interface
bool MeshtasticInterface_sendPosition(GPSData* gpsData) {
    if (!initialized || !gpsData->valid) {
        return false;
    }

    // Format as simple text message
    char msg[100];
    snprintf(msg, sizeof(msg),
             "POS:%.6f,%.6f,%.1fm,%dsat",
             gpsData->latitude,
             gpsData->longitude,
             gpsData->altitude,
             gpsData->satellites);

    // Send directly - RAK4603 handles Meshtastic protocol
    Serial2.println(msg);

    Serial.print(F("Mesh TX: "));
    Serial.println(msg);

    return true;
}
```

No AT commands, no complex protocol - just send text!

## Testing

### 1. Configure RAK4603

```bash
# Connect RAK4603 to computer
meshtastic --set serial.mode TEXTMSG
meshtastic --set serial.enabled true
meshtastic --set serial.baud BAUD_115200
```

### 2. Test with Serial Terminal

Before connecting to AGT, test RAK4603:

```bash
# Open serial terminal to RAK4603
screen /dev/ttyUSB0 115200

# Type test message:
Hello from serial!

# Check Meshtastic app - message should appear
```

### 3. Connect to AGT

Wire GPIO6/7 to RAK4603, AGT will send position messages.

### 4. Verify on Meshtastic App

Open Meshtastic iOS/Android app:
- Should see messages from RAK4603
- Position messages visible as text
- Other mesh nodes can see messages

## References

- [Meshtastic Client API](https://meshtastic.org/docs/development/device/client-api/) - Protocol overview
- [Serial Module Configuration](https://meshtastic.org/docs/configuration/module/serial/) - Serial modes
- [Python Meshtastic](https://python.meshtastic.org/serial_interface.html) - Example implementation

## Alternative: Use Existing Meshtastic App

If you want more robust Meshtastic integration:

**Option:** Connect RAK4603 to Navigator/Pi via USB:
- Navigator runs Meshtastic Python client
- BlueOS extension sends messages via Meshtastic API
- More features: ACKs, encryption keys, device management
- AGT focuses on GPS/Iridium only

This separates concerns and uses mature Meshtastic tools.

## Recommendation Summary

**For your drop camera:**

1. **Configure RAK4603 in TEXTMSG mode**
2. **AGT sends simple text messages** (no protobuf)
3. **RAK4603 handles mesh protocol** automatically
4. **Simple, reliable, easy to debug**

This is the pragmatic approach that gets you mesh connectivity without complex protocol implementation.
