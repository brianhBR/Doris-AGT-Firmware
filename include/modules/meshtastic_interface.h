#ifndef MESHTASTIC_INTERFACE_H
#define MESHTASTIC_INTERFACE_H

#include "gps_manager.h"

// Initialize Meshtastic interface
void MeshtasticInterface_init();

// Send position update to Meshtastic
bool MeshtasticInterface_sendPosition(GPSData* gpsData);

// Send custom text message
bool MeshtasticInterface_sendText(const char* message);

// Send telemetry data
bool MeshtasticInterface_sendTelemetry(float voltage, float current);

// Check for incoming messages
bool MeshtasticInterface_checkMessages();

// Process incoming serial data
void MeshtasticInterface_update();

#endif // MESHTASTIC_INTERFACE_H
