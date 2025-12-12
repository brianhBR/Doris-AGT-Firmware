# Meshtastic Interface Update

> **⚠️ ARCHIVED**: This document has been consolidated into [CHANGELOG.md](CHANGELOG.md).
> Please refer to the CHANGELOG for current project history and changes.
> **Note**: This TEXT mode implementation was superseded by PROTOBUF mode (see CHANGELOG v0.1.0).

## Summary

Updated Meshtastic RAK4603 communication from **incorrect AT command protocol** to **correct TEXT mode (TEXTMSG)**.

## Problem

The initial implementation incorrectly used AT commands like:
```cpp
MESHTASTIC_SERIAL.print("AT+SEND=");
MESHTASTIC_SERIAL.println(message);
```

This was wrong because:
- Meshtastic doesn't use AT commands
- Meshtastic uses Protocol Buffers (binary) over serial
- AT commands would not work with RAK4603 running Meshtastic firmware

## Solution

Configure RAK4603 in **TEXTMSG mode**, which accepts plain text over serial and automatically handles the Meshtastic mesh protocol.

### RAK4603 Pre-Configuration

Connect RAK4603 to computer via USB and configure using Meshtastic CLI:

```bash
meshtastic --set serial.enabled true
meshtastic --set serial.mode TEXTMSG
meshtastic --set serial.baud BAUD_115200
meshtastic --set serial.rxd 21       # RAK4603 RX pin
meshtastic --set serial.txd 22       # RAK4603 TX pin
meshtastic --set serial.timeout 0
meshtastic --set serial.echo false
meshtastic --commit
```

### AGT Wiring

- AGT GPIO6 (TX2, SPI MISO header) → RAK4603 RX (pin 21)
- AGT GPIO7 (RX2, SPI MOSI header) → RAK4603 TX (pin 22)
- Baud: 115200, 8N1

### New Protocol

AGT sends simple text messages terminated with newline (`\n`):

```cpp
// Position message
Serial2.println("POS:37.422408,-122.084108,15.2m,12sat");

// State message
Serial2.println("STATE:MISSION,3600s");

// Telemetry message
Serial2.println("TELEM:V=12.45V,I=1.23A");

// Alert message
Serial2.println("ALERT:DROP_WEIGHT_RELEASED");
```

RAK4603 automatically:
1. Receives the text
2. Wraps it in a Meshtastic TEXT_MESSAGE packet
3. Broadcasts over LoRa mesh
4. Other Meshtastic devices see the message as text

### Incoming Messages

RAK4603 forwards received mesh messages to AGT:

```cpp
void MeshtasticInterface_update() {
    while (Serial2.available()) {
        String msg = Serial2.readStringUntil('\n');
        Serial.print("Mesh RX: ");
        Serial.println(msg);
        // Can parse and act on commands if needed
    }
}
```

## Message Format Standards

### Position Messages
```
Format: POS:<lat>,<lon>,<alt>m,<sats>sat
Example: POS:37.422408,-122.084108,15.2m,12sat
```

### State Messages
```
Format: STATE:<state>,<time>s
Example: STATE:MISSION,3600s
Example: STATE:RECOVERY,7200s
Example: STATE:EMERGENCY,DROP_WEIGHT_RELEASED
```

### Telemetry Messages
```
Format: TELEM:V=<volts>V,I=<amps>A
Example: TELEM:V=12.4V,I=1.23A
Example: TELEM:V=11.8V,I=2.45A
```

### Alert Messages
```
Format: ALERT:<message>
Example: ALERT:DROP_WEIGHT_RELEASED
Example: ALERT:EMERGENCY_DEPTH
Example: ALERT:SURFACE_GPS_FIX
```

## Code Changes

### Files Modified

1. **[src/modules/meshtastic_interface.cpp](src/modules/meshtastic_interface.cpp)**
   - Removed all AT command code
   - Simplified to plain text `println()` calls
   - Added new functions: `sendState()`, `sendAlert()`
   - Updated comments with TEXTMSG configuration instructions
   - Improved incoming message handling

2. **[include/modules/meshtastic_interface.h](include/modules/meshtastic_interface.h)**
   - Added header documentation
   - Added function declarations: `sendState()`, `sendAlert()`
   - Documented message formats

### New Functions

```cpp
// Send state update to mesh
bool MeshtasticInterface_sendState(const char* stateName, uint32_t timeInState);

// Send alert message to mesh
bool MeshtasticInterface_sendAlert(const char* alertMessage);
```

### Updated Functions

All functions simplified - no more AT commands or response waiting:

```cpp
bool MeshtasticInterface_sendPosition(GPSData* gpsData) {
    // Format message
    char msg[100];
    snprintf(msg, sizeof(msg), "POS:%.6f,%.6f,%.1fm,%dsat",
             gpsData->latitude, gpsData->longitude,
             gpsData->altitude, gpsData->satellites);

    // Send directly - RAK4603 handles mesh
    Serial2.println(msg);

    return true;  // Fire and forget
}
```

## Advantages

1. **Simple Implementation**: No protobuf library needed on AGT
2. **Human Readable**: Messages visible in Meshtastic apps
3. **Easy Debug**: Can monitor with serial terminal
4. **Flexible**: Can send any text data
5. **Compatible**: Works with all Meshtastic devices
6. **No ACK Needed**: Fire-and-forget (appropriate for mesh)

## Limitations

1. **No ACK**: No confirmation message was received (normal for mesh)
2. **Text Only**: Not as efficient as binary protobuf
3. **No Encryption on Serial**: Meshtastic encrypts mesh traffic, but AGT→RAK4603 serial is plain
4. **One-Way Focus**: Primarily AGT → Mesh (receiving possible but not heavily used)

## Testing

### 1. Configure RAK4603
```bash
# Connect RAK4603 to computer
meshtastic --set serial.mode TEXTMSG
meshtastic --set serial.enabled true
meshtastic --set serial.baud BAUD_115200
meshtastic --commit
```

### 2. Test with Serial Terminal
Before connecting to AGT:
```bash
# Open serial terminal to RAK4603
screen /dev/ttyUSB0 115200

# Type test message:
Hello from serial!

# Check Meshtastic app - message should appear
```

### 3. Connect to AGT
Wire GPIO6/7 to RAK4603 pins 21/22, upload firmware

### 4. Monitor AGT Serial Output
```
Mesh TX: POS:37.422408,-122.084108,15.2m,12sat
Mesh TX: STATE:MISSION,3600s
```

### 5. Verify on Meshtastic App
- Open Meshtastic iOS/Android app
- Should see text messages from RAK4603
- Messages appear in text message list
- Other mesh nodes can see and relay

## References

- [Meshtastic Serial Module Configuration](https://meshtastic.org/docs/configuration/module/serial/) - Official docs
- [MESHTASTIC_PROTOCOL.md](docs/MESHTASTIC_PROTOCOL.md) - Detailed protocol explanation
- [Meshtastic Client API](https://meshtastic.org/docs/development/device/client-api/) - Protocol overview

## Compatibility Notes

**Apollo3 Serial2 Pin Mapping**: The Apollo3 Arduino core may need variant file modifications to properly map Serial2 to GPIO6/7. If Serial2 doesn't work on these pins:

1. Check if `Serial2.setPins()` is available in your Apollo3 core
2. May need to modify board variant files
3. Alternative: Use software serial library

This is documented as a potential issue in the code comments.
