#ifndef MESHTASTIC_INTERFACE_H
#define MESHTASTIC_INTERFACE_H

#include "gps_manager.h"

/*
 * Meshtastic RAK4603 Interface (TEXT MODE)
 *
 * RAK4603 must be pre-configured in TEXTMSG mode via Meshtastic CLI.
 * AGT sends simple text messages, RAK4603 handles mesh protocol.
 *
 * Message formats:
 *   - Position: POS:<lat>,<lon>,<alt>m,<sats>sat
 *   - State: STATE:<state>,<time>s
 *   - Telemetry: TELEM:V=<volts>V,I=<amps>A
 *   - Alert: ALERT:<message>
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
