/*
 * Meshtastic RAK4603 - Position via protobuf (PROTO mode)
 * Sends Position on POSITION_APP port so nodes show on Meshtastic maps.
 * RAK4603 must be in PROTO mode: serial.mode PROTO, serial.enabled true.
 * Frame: 0x94 0xc3 len_MSB len_LSB + encoded ToRadio.
 */

#include "modules/meshtastic_interface.h"
#include "config.h"
#include "utils/SoftwareSerial.h"
#include <Arduino.h>
#include <pb_encode.h>
#include "meshtastic/mesh.pb.h"
#include "meshtastic/portnums.pb.h"

#define MESHTASTIC_HEADER_0  0x94
#define MESHTASTIC_HEADER_1  0xc3
#define MESH_BROADCAST       0xFFFFFFFF
#define POSITION_PAYLOAD_MAX 128

SoftwareSerial* MeshtasticSerial = nullptr;
static bool initialized = false;
static uint32_t positionSeq = 0;

bool MeshtasticInterface_sendPosition(GPSData* gpsData) {
    if (!initialized || MeshtasticSerial == nullptr) {
        return false;
    }
    if (!gpsData->valid) {
        return false;
    }

    meshtastic_Position pos = meshtastic_Position_init_zero;
    pos.has_latitude_i = true;
    pos.latitude_i = (int32_t)(gpsData->latitude * 1e7);
    pos.has_longitude_i = true;
    pos.longitude_i = (int32_t)(gpsData->longitude * 1e7);
    pos.has_altitude = true;
    pos.altitude = (int32_t)gpsData->altitude;
    pos.time = 0;  // Optional: set from RTC if available
    pos.timestamp = 0;
    pos.location_source = meshtastic_Position_LocSource_LOC_EXTERNAL;
    pos.altitude_source = meshtastic_Position_AltSource_ALT_EXTERNAL;
    pos.sats_in_view = gpsData->satellites;
    pos.fix_type = gpsData->fixType;
    pos.seq_number = positionSeq++;
    pos.next_update = 60;

    uint8_t posBuf[POSITION_PAYLOAD_MAX];
    pb_ostream_t posStream = pb_ostream_from_buffer(posBuf, sizeof(posBuf));
    if (!pb_encode(&posStream, meshtastic_Position_fields, &pos)) {
        Serial.println(F("Mesh: Position encode fail"));
        return false;
    }
    size_t posLen = posStream.bytes_written;

    meshtastic_Data data = meshtastic_Data_init_zero;
    data.portnum = meshtastic_PortNum_POSITION_APP;
    data.payload.size = posLen;
    if (posLen > sizeof(data.payload.bytes)) {
        return false;
    }
    memcpy(data.payload.bytes, posBuf, posLen);
    data.dest = MESH_BROADCAST;

    meshtastic_MeshPacket packet = meshtastic_MeshPacket_init_zero;
    packet.from = 0;
    packet.to = MESH_BROADCAST;
    packet.which_payload_variant = 4;  // decoded
    packet.payload_variant.decoded = data;
    packet.hop_limit = 3;
    packet.want_ack = false;
    packet.priority = meshtastic_MeshPacket_Priority_RELIABLE;

    meshtastic_ToRadio toRadio = meshtastic_ToRadio_init_zero;
    toRadio.which_payload_variant = 1;  // packet
    toRadio.payload_variant.packet = packet;

    uint8_t outBuf[320];
    pb_ostream_t outStream = pb_ostream_from_buffer(outBuf, sizeof(outBuf));
    if (!pb_encode(&outStream, meshtastic_ToRadio_fields, &toRadio)) {
        Serial.println(F("Mesh: ToRadio encode fail"));
        return false;
    }
    size_t outLen = outStream.bytes_written;

    MeshtasticSerial->write((uint8_t)MESHTASTIC_HEADER_0);
    MeshtasticSerial->write((uint8_t)MESHTASTIC_HEADER_1);
    MeshtasticSerial->write((uint8_t)(outLen >> 8));
    MeshtasticSerial->write((uint8_t)(outLen & 0xFF));
    MeshtasticSerial->write(outBuf, outLen);
    MeshtasticSerial->flush();

    return true;
}

bool MeshtasticInterface_sendText(const char* message) {
    if (!initialized || MeshtasticSerial == nullptr) return false;
    size_t len = strlen(message);
    if (len > 230) len = 230;

    meshtastic_Data data = meshtastic_Data_init_zero;
    data.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
    data.payload.size = len;
    memcpy(data.payload.bytes, message, len);
    data.dest = MESH_BROADCAST;

    meshtastic_MeshPacket packet = meshtastic_MeshPacket_init_zero;
    packet.from = 0;
    packet.to = MESH_BROADCAST;
    packet.which_payload_variant = 4;
    packet.payload_variant.decoded = data;
    packet.hop_limit = 3;
    packet.want_ack = false;

    meshtastic_ToRadio toRadio = meshtastic_ToRadio_init_zero;
    toRadio.which_payload_variant = 1;
    toRadio.payload_variant.packet = packet;

    uint8_t outBuf[320];
    pb_ostream_t outStream = pb_ostream_from_buffer(outBuf, sizeof(outBuf));
    if (!pb_encode(&outStream, meshtastic_ToRadio_fields, &toRadio)) return false;
    size_t outLen = outStream.bytes_written;

    MeshtasticSerial->write((uint8_t)MESHTASTIC_HEADER_0);
    MeshtasticSerial->write((uint8_t)MESHTASTIC_HEADER_1);
    MeshtasticSerial->write((uint8_t)(outLen >> 8));
    MeshtasticSerial->write((uint8_t)(outLen & 0xFF));
    MeshtasticSerial->write(outBuf, outLen);
    MeshtasticSerial->flush();
    return true;
}

bool MeshtasticInterface_sendTelemetry(float voltage, float current) {
    (void)voltage;
    (void)current;
    return true;
}

bool MeshtasticInterface_sendState(const char* stateName, uint32_t timeInState) {
    char buf[80];
    snprintf(buf, sizeof(buf), "STATE:%s %lus", stateName, timeInState);
    return MeshtasticInterface_sendText(buf);
}

bool MeshtasticInterface_sendAlert(const char* alertMessage) {
    return MeshtasticInterface_sendText(alertMessage);
}

bool MeshtasticInterface_checkMessages() {
    return false;
}

void MeshtasticInterface_update() {
    // Optional: poll RX for FromRadio (e.g. to get node id). TX-only is fine for position.
}

void MeshtasticInterface_init() {
    MeshtasticSerial = new SoftwareSerial(MESHTASTIC_RX_PIN, MESHTASTIC_TX_PIN);
    MeshtasticSerial->begin(MESHTASTIC_BAUD);
    delay(300);
    initialized = true;
    Serial.println(F("Meshtastic: PROTO mode, position via protobuf"));
}
