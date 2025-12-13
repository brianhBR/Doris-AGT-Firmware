#include "modules/meshtastic_interface.h"
#include "config.h"
#include <Arduino.h>

// Protobuf headers for Meshtastic protocol
#include <pb_encode.h>
#include <pb_decode.h>
#include "meshtastic/mesh.pb.h"
#include "meshtastic/portnums.pb.h"

// Create a dedicated UART instance for Meshtastic on UART0
// This is separate from Serial1 (UART1) used by Iridium
// Use pointer to avoid early construction crash
UART* MeshtasticSerial = nullptr;

/*
 * Meshtastic RAK4603 Serial Interface
 *
 * Communication Protocol: PROTOBUF (Client API)
 *
 * IMPORTANT: RAK4603 must be configured in PROTO mode (not default SIMPLE mode):
 *   meshtastic --set serial.mode PROTO
 *   meshtastic --set serial.enabled true
 *   meshtastic --set serial.baud BAUD_115200
 *   meshtastic --commit
 *
 * Uses the Meshtastic serial protocol with 4-byte header:
 *   Byte 0: START1 (0x94)
 *   Byte 1: START2 (0xc3)
 *   Byte 2: MSB of protobuf length
 *   Byte 3: LSB of protobuf length
 *   Followed by: Protobuf-encoded ToRadio message
 *
 * This allows sending proper GPS position packets using the Position portnum,
 * which are handled natively by Meshtastic and displayed on maps.
 *
 * Reference: https://meshtastic.org/docs/development/device/client-api/
 */

// Protocol constants
#define MESH_START1          0x94
#define MESH_START2          0xc3
#define MESH_MAX_PACKET_SIZE 512

static bool initialized = false;
static uint8_t txBuffer[MESH_MAX_PACKET_SIZE];
static uint8_t rxBuffer[MESH_MAX_PACKET_SIZE];

// Helper function to send a protobuf packet with 4-byte header
static bool sendProtobufPacket(const uint8_t* buffer, size_t length) {
    if (length > MESH_MAX_PACKET_SIZE || MeshtasticSerial == nullptr) {
        Serial.println(F("Mesh TX: Packet too large or UART not initialized"));
        return false;
    }

    Serial.print(F("Mesh TX: Sending packet, length="));
    Serial.println(length);

    // Send 4-byte header
    MeshtasticSerial->write(MESH_START1);
    MeshtasticSerial->write(MESH_START2);
    MeshtasticSerial->write((length >> 8) & 0xFF);  // MSB
    MeshtasticSerial->write(length & 0xFF);          // LSB

    // Send protobuf data
    MeshtasticSerial->write(buffer, length);
    MeshtasticSerial->flush();

    // Show hex dump of first 32 bytes for debugging
    Serial.print(F("Mesh TX: Header [94 c3 "));
    Serial.print((length >> 8) & 0xFF, HEX);
    Serial.print(F(" "));
    Serial.print(length & 0xFF, HEX);
    Serial.print(F("] Data ["));
    for (size_t i = 0; i < length && i < 32; i++) {
        if (i > 0) Serial.print(F(" "));
        if (buffer[i] < 0x10) Serial.print(F("0"));
        Serial.print(buffer[i], HEX);
    }
    if (length > 32) Serial.print(F(" ..."));
    Serial.println(F("]"));

    return true;
}

void MeshtasticInterface_init() {
    // Initialize MeshtasticSerial (UART0) on J10 connector (Qwiic/I2C port 4)
    // D39 (SCL4) = TX to RAK4603 RX
    // D40 (SDA4) = RX from RAK4603 TX
    //
    // UART Configuration:
    //   - Iridium 9603N: Serial1 (UART1) on default pins D24/D25
    //   - Meshtastic RAK4603: MeshtasticSerial (UART0) on pins D39/D40
    // Both UARTs operate independently without conflicts

    // Create UART object now (safe to do after hardware init)
    MeshtasticSerial = new UART(0, MESHTASTIC_TX_PIN, MESHTASTIC_RX_PIN);

    // Begin the UART with the specified baud rate
    MeshtasticSerial->begin(MESHTASTIC_BAUD);

    delay(500);

    Serial.println(F("================================"));
    Serial.println(F("Meshtastic: Interface initialized"));
    Serial.println(F("  Serial: MeshtasticSerial (UART0)"));
    Serial.println(F("  Mode: PROTO (Client API)"));
    Serial.print(F("  TX Pin: D"));
    Serial.print(MESHTASTIC_TX_PIN);
    Serial.print(F(" (to RAK4603 RX)"));
    Serial.println();
    Serial.print(F("  RX Pin: D"));
    Serial.print(MESHTASTIC_RX_PIN);
    Serial.print(F(" (from RAK4603 TX)"));
    Serial.println();
    Serial.print(F("  Baud: "));
    Serial.println(MESHTASTIC_BAUD);
    Serial.println(F("  Protocol: 4-byte header + protobuf"));
    Serial.println(F(""));
    Serial.println(F("IMPORTANT: RAK4603 must be configured:"));
    Serial.println(F("  meshtastic --set serial.mode PROTO"));
    Serial.println(F("  meshtastic --set serial.enabled true"));
    Serial.println(F("  meshtastic --set serial.baud BAUD_115200"));
    Serial.println(F("================================"));

    initialized = true;
}

bool MeshtasticInterface_sendPosition(GPSData* gpsData) {
    if (!initialized) {
        Serial.println(F("Mesh TX: Not initialized"));
        return false;
    }

    if (!gpsData->valid) {
        Serial.println(F("Mesh TX: GPS data not valid"));
        return false;
    }

    Serial.println(F("Mesh TX: Encoding position message..."));

    // Create Position protobuf message
    meshtastic_Position position = meshtastic_Position_init_zero;

    // Convert lat/lon to fixed-point integers (1e-7 degrees)
    position.latitude_i = (int32_t)(gpsData->latitude * 1e7);
    position.longitude_i = (int32_t)(gpsData->longitude * 1e7);
    position.altitude = (int32_t)gpsData->altitude;
    position.sats_in_view = gpsData->satellites;
    position.time = millis() / 1000;  // Unix timestamp (approximate)

    Serial.print(F("Mesh TX: lat_i="));
    Serial.print(position.latitude_i);
    Serial.print(F(" lon_i="));
    Serial.print(position.longitude_i);
    Serial.print(F(" alt="));
    Serial.print(position.altitude);
    Serial.print(F(" sats="));
    Serial.println(position.sats_in_view);

    // Encode Position to buffer first
    uint8_t posBuf[256];
    pb_ostream_t posStream = pb_ostream_from_buffer(posBuf, sizeof(posBuf));
    if (!pb_encode(&posStream, meshtastic_Position_fields, &position)) {
        Serial.print(F("Mesh TX: Failed to encode position. Error: "));
        Serial.println(PB_GET_ERROR(&posStream));
        return false;
    }
    size_t posLength = posStream.bytes_written;
    Serial.print(F("Mesh TX: Position encoded, "));
    Serial.print(posLength);
    Serial.println(F(" bytes"));

    // Create MeshPacket - use encrypted payload for now (contains raw bytes)
    meshtastic_MeshPacket packet = meshtastic_MeshPacket_init_zero;
    packet.which_payload_variant = meshtastic_MeshPacket_encrypted_tag;
    packet.payload_variant.encrypted.size = posLength;
    memcpy(packet.payload_variant.encrypted.bytes, posBuf, posLength);
    packet.want_ack = false;  // No ACK needed for position broadcasts

    // Create ToRadio wrapper
    meshtastic_ToRadio toRadio = meshtastic_ToRadio_init_zero;
    toRadio.which_payload_variant = meshtastic_ToRadio_packet_tag;
    toRadio.payload_variant.packet = packet;

    Serial.println(F("Mesh TX: Encoding ToRadio wrapper..."));

    // Encode ToRadio
    pb_ostream_t stream = pb_ostream_from_buffer(txBuffer, sizeof(txBuffer));
    if (!pb_encode(&stream, meshtastic_ToRadio_fields, &toRadio)) {
        Serial.print(F("Mesh TX: Failed to encode ToRadio. Error: "));
        Serial.println(PB_GET_ERROR(&stream));
        return false;
    }

    Serial.print(F("Mesh TX: ToRadio encoded, "));
    Serial.print(stream.bytes_written);
    Serial.println(F(" bytes"));

    // Send with 4-byte header
    bool success = sendProtobufPacket(txBuffer, stream.bytes_written);

    if (success) {
        Serial.print(F("Mesh TX: Position "));
        Serial.print(gpsData->latitude, 6);
        Serial.print(F(","));
        Serial.print(gpsData->longitude, 6);
        Serial.print(F(" alt="));
        Serial.print(gpsData->altitude);
        Serial.print(F("m sats="));
        Serial.println(gpsData->satellites);
    }

    return success;
}

bool MeshtasticInterface_sendText(const char* message) {
    if (!initialized) {
        return false;
    }

    size_t msgLen = strlen(message);
    if (msgLen == 0 || msgLen > 200) {
        return false;
    }

    // Create MeshPacket with text payload
    meshtastic_MeshPacket packet = meshtastic_MeshPacket_init_zero;
    packet.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    packet.payload_variant.decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
    packet.payload_variant.decoded.payload.size = msgLen;
    memcpy(packet.payload_variant.decoded.payload.bytes, message, msgLen);
    packet.want_ack = false;

    // Create ToRadio wrapper
    meshtastic_ToRadio toRadio = meshtastic_ToRadio_init_zero;
    toRadio.which_payload_variant = meshtastic_ToRadio_packet_tag;
    toRadio.payload_variant.packet = packet;

    // Encode ToRadio
    pb_ostream_t stream = pb_ostream_from_buffer(txBuffer, sizeof(txBuffer));
    if (!pb_encode(&stream, meshtastic_ToRadio_fields, &toRadio)) {
        Serial.println(F("Mesh TX: Failed to encode text"));
        return false;
    }

    // Send with 4-byte header
    bool success = sendProtobufPacket(txBuffer, stream.bytes_written);

    if (success) {
        Serial.print(F("Mesh TX: Text: "));
        Serial.println(message);
    }

    return success;
}

bool MeshtasticInterface_sendTelemetry(float voltage, float current) {
    if (!initialized) {
        return false;
    }

    // Format as text message for telemetry
    char telemetryMsg[100];
    snprintf(telemetryMsg, sizeof(telemetryMsg),
             "TELEM: V=%.2fV I=%.2fA",
             voltage, current);

    return MeshtasticInterface_sendText(telemetryMsg);
}

bool MeshtasticInterface_sendState(const char* stateName, uint32_t timeInState) {
    if (!initialized) {
        return false;
    }

    // Format as text message for state updates
    char stateMsg[100];
    snprintf(stateMsg, sizeof(stateMsg),
             "STATE: %s (%lus)",
             stateName, timeInState);

    return MeshtasticInterface_sendText(stateMsg);
}

bool MeshtasticInterface_sendAlert(const char* alertMessage) {
    if (!initialized) {
        return false;
    }

    // Format as text message for alerts
    char alertMsg[100];
    snprintf(alertMsg, sizeof(alertMsg),
             "ALERT: %s",
             alertMessage);

    return MeshtasticInterface_sendText(alertMsg);
}

bool MeshtasticInterface_checkMessages() {
    if (!initialized) {
        return false;
    }

    // Check for incoming protobuf packets with 4-byte header
    static uint8_t state = 0;  // 0=looking for START1, 1=START2, 2=MSB, 3=LSB, 4=data
    static uint16_t packetLength = 0;
    static uint16_t bytesRead = 0;
    static uint32_t lastRxActivity = 0;

    if (MeshtasticSerial == nullptr) return false;

    while (MeshtasticSerial->available()) {
        uint8_t b = MeshtasticSerial->read();

        // Debug: Show raw bytes received (throttle output to prevent spam)
        if (millis() - lastRxActivity > 1000 || state == 0) {
            Serial.print(F("Mesh RX: byte=0x"));
            if (b < 0x10) Serial.print(F("0"));
            Serial.print(b, HEX);
            Serial.print(F(" state="));
            Serial.println(state);
            lastRxActivity = millis();
        }

        switch (state) {
            case 0:  // Looking for START1
                if (b == MESH_START1) {
                    state = 1;
                }
                break;

            case 1:  // Looking for START2
                if (b == MESH_START2) {
                    state = 2;
                } else {
                    state = 0;  // Reset
                }
                break;

            case 2:  // MSB of length
                packetLength = (b << 8);
                state = 3;
                break;

            case 3:  // LSB of length
                packetLength |= b;
                if (packetLength > MESH_MAX_PACKET_SIZE) {
                    // Invalid packet length, reset
                    state = 0;
                    Serial.println(F("Mesh RX: Invalid packet length"));
                } else {
                    bytesRead = 0;
                    state = 4;
                }
                break;

            case 4:  // Reading data
                rxBuffer[bytesRead++] = b;
                if (bytesRead >= packetLength) {
                    // Full packet received, decode it
                    meshtastic_FromRadio fromRadio = meshtastic_FromRadio_init_zero;
                    pb_istream_t stream = pb_istream_from_buffer(rxBuffer, packetLength);

                    if (pb_decode(&stream, meshtastic_FromRadio_fields, &fromRadio)) {
                        // Successfully decoded
                        if (fromRadio.which_payload_variant == meshtastic_FromRadio_packet_tag) {
                            meshtastic_MeshPacket* pkt = &fromRadio.payload_variant.packet;

                            if (pkt->which_payload_variant == meshtastic_MeshPacket_decoded_tag) {
                                Serial.print(F("Mesh RX: Packet received, portnum="));
                                Serial.println(pkt->payload_variant.decoded.portnum);

                                // Handle text messages
                                if (pkt->payload_variant.decoded.portnum == meshtastic_PortNum_TEXT_MESSAGE_APP) {
                                    char textMsg[201];
                                    size_t msgLen = pkt->payload_variant.decoded.payload.size;
                                    if (msgLen > 200) msgLen = 200;
                                    memcpy(textMsg, pkt->payload_variant.decoded.payload.bytes, msgLen);
                                    textMsg[msgLen] = '\0';

                                    Serial.print(F("Mesh RX: Text: "));
                                    Serial.println(textMsg);
                                }
                            }
                        }
                    } else {
                        Serial.println(F("Mesh RX: Failed to decode packet"));
                    }

                    // Reset for next packet
                    state = 0;
                    return true;
                }
                break;
        }
    }

    return false;
}

void MeshtasticInterface_update() {
    if (!initialized) {
        return;
    }

    // Process any incoming messages
    MeshtasticInterface_checkMessages();
}
