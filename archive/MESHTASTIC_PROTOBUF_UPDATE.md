# Meshtastic Protobuf Protocol Update

> **⚠️ ARCHIVED**: This document has been consolidated into [CHANGELOG.md](CHANGELOG.md).
> Please refer to the CHANGELOG for current project history and changes.

## Summary

Updated Meshtastic interface from **TEXT mode** to **PROTOBUF mode (Client API)** to properly send GPS position coordinates using the official Meshtastic protocol.

## Serial Mode Configuration

**IMPORTANT:** RAK4603 defaults to **SIMPLE mode** (UART pass-through). You must configure it to **PROTO mode** for protobuf support:

```bash
meshtastic --set serial.mode PROTO
meshtastic --set serial.enabled true
meshtastic --set serial.baud BAUD_115200
meshtastic --commit
```

## Why PROTO Mode?

**Advantages over SIMPLE/TEXTMSG modes:**
- ✅ **GPS positions displayed on maps** - Proper POSITION_APP messages show on Meshtastic map
- ✅ **Efficient binary encoding** - Much smaller packet sizes
- ✅ **Native Meshtastic integration** - Works with all Meshtastic apps
- ✅ **Structured data** - Proper message types (Position, Text, Telemetry, etc.)
- ✅ **Programmatic control** - Full access to Meshtastic Client API

## Protocol Details

### Packet Format

Every packet uses a 4-byte header followed by protobuf data:

```
┌────────┬────────┬───────────┬───────────┬─────────────────┐
│ START1 │ START2 │ Length MSB│ Length LSB│  Protobuf Data  │
├────────┼────────┼───────────┼───────────┼─────────────────┤
│  0x94  │  0xc3  │   High    │    Low    │  ToRadio msg    │
└────────┴────────┴───────────┴───────────┴─────────────────┘
```

- **START1**: `0x94` - Magic byte 1
- **START2**: `0xc3` - Magic byte 2
- **Length**: 16-bit big-endian packet length (max 512 bytes)
- **Data**: Protocol buffer encoded `ToRadio` message

### Message Types

**1. Position Messages (POSITION_APP)**
- Uses `meshtastic_Position` protobuf
- Latitude/longitude as fixed-point int32 (1e-7 degrees)
- Altitude in meters
- Satellite count
- Displays on Meshtastic map

**2. Text Messages (TEXT_MESSAGE_APP)**
- Uses raw bytes payload
- Appears in message list
- Used for alerts, state updates, telemetry

**3. Incoming Messages (FromRadio)**
- Decoded from same 4-byte header format
- State machine parser handles framing
- Can receive text commands from mesh

## Code Changes

### Files Modified

1. **[platformio.ini](platformio.ini)**
   - Added `nanopb/Nanopb@^0.4.8` - Protocol buffer library
   - Added `https://github.com/meshtastic/protobufs.git` - Meshtastic message definitions

2. **[src/modules/meshtastic_interface.cpp](src/modules/meshtastic_interface.cpp)**
   - Complete rewrite using nanopb encoder/decoder
   - `sendPosition()` now sends proper `Position` protobuf with POSITION_APP portnum
   - `sendText()` sends TEXT_MESSAGE_APP packets
   - State machine parser for incoming 4-byte header packets
   - Proper protobuf encoding/decoding

3. **[include/modules/meshtastic_interface.h](include/modules/meshtastic_interface.h)**
   - Updated documentation to reflect protobuf protocol

### New Dependencies

```ini
lib_deps =
    ...
    nanopb/Nanopb@^0.4.8                      # Protocol Buffers for embedded
    https://github.com/meshtastic/protobufs.git # Meshtastic message definitions
```

## API Usage

### Sending GPS Position

```cpp
GPSData gpsData;
gpsData.latitude = 37.422408;
gpsData.longitude = -122.084108;
gpsData.altitude = 15.2;
gpsData.satellites = 12;
gpsData.valid = true;

// Sends protobuf Position message with POSITION_APP portnum
MeshtasticInterface_sendPosition(&gpsData);
```

This creates:
1. `meshtastic_Position` with lat/lon as int32 (1e-7 degrees)
2. Wrapped in `meshtastic_MeshPacket` with POSITION_APP portnum
3. Wrapped in `meshtastic_ToRadio`
4. Encoded with nanopb
5. Sent with 4-byte header

### Sending Text Messages

```cpp
// Sends TEXT_MESSAGE_APP packet
MeshtasticInterface_sendText("Hello from AGT!");

// Telemetry (as text)
MeshtasticInterface_sendTelemetry(12.4, 1.23);
// Sends: "TELEM: V=12.40V I=1.23A"

// State update (as text)
MeshtasticInterface_sendState("MISSION", 3600);
// Sends: "STATE: MISSION (3600s)"

// Alert (as text)
MeshtasticInterface_sendAlert("DROP_WEIGHT_RELEASED");
// Sends: "ALERT: DROP_WEIGHT_RELEASED"
```

### Receiving Messages

```cpp
void loop() {
    // Automatically processes incoming FromRadio packets
    MeshtasticInterface_update();

    // Handles:
    // - 4-byte header parsing
    // - Protobuf decoding
    // - Text message extraction
    // - Prints to serial debug
}
```

## Protocol Buffer Messages Used

### ToRadio (AGT → RAK4603)

```protobuf
message ToRadio {
    oneof payload_variant {
        MeshPacket packet = 1;
    }
}

message MeshPacket {
    oneof payload_variant {
        Data decoded = 3;
    }
    bool want_ack = 5;
}

message Data {
    PortNum portnum = 1;
    bytes payload = 2;
}
```

### Position Message

```protobuf
message Position {
    int32 latitude_i = 1;   // Latitude * 1e7 (fixed-point)
    int32 longitude_i = 2;  // Longitude * 1e7
    int32 altitude = 3;     // Meters MSL
    uint32 time = 4;        // Unix timestamp
    uint32 sats_in_view = 8;// GPS satellite count
}
```

### FromRadio (RAK4603 → AGT)

```protobuf
message FromRadio {
    oneof payload_variant {
        MeshPacket packet = 2;
    }
}
```

## Implementation Details

### Encoding Flow (Sending Position)

```
GPSData
   ↓
Create Position protobuf
   ├─ latitude_i  = lat * 1e7
   ├─ longitude_i = lon * 1e7
   ├─ altitude    = alt (meters)
   └─ sats_in_view = satellite count
   ↓
pb_encode() → buffer
   ↓
Create MeshPacket
   ├─ portnum = POSITION_APP
   └─ payload = Position bytes
   ↓
Create ToRadio
   └─ packet = MeshPacket
   ↓
pb_encode() → txBuffer
   ↓
Send 4-byte header + txBuffer
```

### Decoding Flow (Receiving)

```
Serial bytes
   ↓
State machine parser
   ├─ Wait for 0x94
   ├─ Wait for 0xc3
   ├─ Read length (MSB, LSB)
   └─ Read 'length' bytes → rxBuffer
   ↓
pb_decode(rxBuffer) → FromRadio
   ↓
Check payload_variant
   └─ If packet:
       └─ If TEXT_MESSAGE_APP:
           └─ Extract text and print
```

## Testing

### 1. Build and Upload

```bash
pio run -t upload
```

### 2. Monitor Serial Output

```bash
pio device monitor
```

Expected output:
```
================================
Meshtastic: Interface initialized
  Mode: PROTOBUF (Client API)
  Pins: GPIO6/GPIO7 (SPI header)
  Baud: 115200
  Protocol: 4-byte header + protobuf
================================
```

### 3. Send Test Position

Once GPS has a fix:
```
Mesh TX: Position 37.422408,-122.084108 alt=15.2m sats=12
```

### 4. Verify on Meshtastic App

- Open Meshtastic iOS/Android app
- Go to **Map** tab
- AGT position should appear as a node on the map
- Position updates broadcast to all mesh nodes

### 5. Send Test Text

```
Mesh TX: Text: ALERT: DROP_WEIGHT_RELEASED
```

Should appear in message list on Meshtastic app.

## Comparison: TEXT vs PROTOBUF

| Feature | TEXT Mode | PROTOBUF Mode |
|---------|-----------|---------------|
| **Setup** | Requires `meshtastic --set serial.mode TEXTMSG` | Default, no config needed |
| **Position** | Text string (not on map) | Proper GPS coordinates on map |
| **Packet Size** | ~50 bytes (text) | ~30 bytes (binary) |
| **Map Display** | ❌ No | ✅ Yes |
| **Meshtastic Apps** | Text messages only | Full integration |
| **Efficiency** | Low | High |
| **Complexity** | Simple | Moderate (nanopb) |

## Advantages of Protobuf Implementation

1. **Native Map Integration**
   - GPS positions appear on Meshtastic map
   - Other users see drop camera location in real-time
   - Position history tracked

2. **Proper Message Types**
   - POSITION_APP for GPS coordinates
   - TEXT_MESSAGE_APP for alerts/status
   - Future: TELEMETRY_APP for battery data

3. **Efficient Binary Encoding**
   - Position: ~30 bytes vs ~50 bytes in text
   - Faster transmission over LoRa
   - Lower power consumption

4. **Standard Protocol**
   - Compatible with all Meshtastic clients
   - Works with iOS, Android, Web apps
   - No custom parsing needed

5. **No Special Configuration**
   - RAK4603 works with default settings
   - No `--set serial.mode` commands needed
   - Works out of box

## Memory Usage

```cpp
static uint8_t txBuffer[512];  // Transmission buffer
static uint8_t rxBuffer[512];  // Reception buffer
```

Total: ~1KB RAM for buffers

Protobuf messages are stack-allocated and small (~100-200 bytes).

## Known Limitations

1. **No ACK Support (Yet)**
   - Currently `want_ack = false`
   - Fire-and-forget transmission
   - Could be added for critical messages

2. **Limited Incoming Message Handling**
   - Receives and prints text messages
   - Could parse commands (e.g., "emergency") in future

3. **Nanopb Overhead**
   - Adds ~10-15KB flash for protobuf library
   - Acceptable for AGT's flash capacity

## Future Enhancements

1. **Telemetry App Support**
   - Send battery voltage via TELEMETRY_APP
   - Native display in Meshtastic apps

2. **Remote Commands**
   - Parse incoming TEXT_MESSAGE_APP
   - Trigger emergency via mesh command
   - Arm/disarm drop weight remotely

3. **Node Info**
   - Send device info (firmware version, hardware)
   - Node identification on mesh

4. **Traceroute**
   - Use TRACEROUTE_APP for mesh diagnostics
   - Verify connectivity before deployment

## References

- [Meshtastic Client API Documentation](https://meshtastic.org/docs/development/device/client-api/)
- [Meshtastic Protobufs Repository](https://github.com/meshtastic/protobufs)
- [Nanopb Documentation](https://jpa.kapsi.fi/nanopb/)
- [Protocol Buffers](https://developers.google.com/protocol-buffers)

## Migration from TEXT Mode

If you previously used TEXT mode:

**Before:**
```cpp
// RAK4603 configuration required:
meshtastic --set serial.mode TEXTMSG
meshtastic --set serial.enabled true
```

**After:**
- No configuration needed
- RAK4603 uses default PROTO mode
- Just upload new firmware

The new implementation is **backwards compatible** - position data now appears properly on maps while text messages still work for alerts and status.
