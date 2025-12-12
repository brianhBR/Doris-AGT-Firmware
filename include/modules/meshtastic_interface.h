#ifndef MESHTASTIC_INTERFACE_H
#define MESHTASTIC_INTERFACE_H

#include "gps_manager.h"

/*
 * Meshtastic RAK4603 Interface (PROTO MODE - Client API)
 *
 * IMPORTANT: Configure RAK4603 in PROTO mode:
 *   meshtastic --set serial.mode PROTO
 *   meshtastic --commit
 *
 * Uses the Meshtastic serial protocol with Protocol Buffers.
 *
 * Protocol:
 *   - 4-byte header: [0x94, 0xc3, length_MSB, length_LSB]
 *   - Followed by protobuf-encoded ToRadio/FromRadio message
 *
 * Position messages use POSITION_APP portnum and proper GPS coordinates.
 * Text messages use TEXT_MESSAGE_APP portnum.
 *
 * Reference: https://meshtastic.org/docs/development/device/client-api/
 */

// Initialize Meshtastic interface
void MeshtasticInterface_init();

// Send position update to Meshtastic
bool MeshtasticInterface_sendPosition(GPSData* gpsData);

// Send custom text message
bool MeshtasticInterface_sendText(const char* message);

// Send telemetry data (voltage and current)
bool MeshtasticInterface_sendTelemetry(float voltage, float current);

// Send state update (state name and time in state)
bool MeshtasticInterface_sendState(const char* stateName, uint32_t timeInState);

// Send alert message
bool MeshtasticInterface_sendAlert(const char* alertMessage);

// Check for incoming messages
bool MeshtasticInterface_checkMessages();

// Process incoming serial data
void MeshtasticInterface_update();

#endif // MESHTASTIC_INTERFACE_H
