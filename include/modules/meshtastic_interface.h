#ifndef MESHTASTIC_INTERFACE_H
#define MESHTASTIC_INTERFACE_H

#include "gps_manager.h"

/*
 * Meshtastic RAK4603 Interface - NMEA GPS output to J10
 *
 * The AGT outputs standard NMEA 0183 sentences (GPGGA, GPRMC) on the serial
 * connected to Meshtastic's J10. J10 is UART1 on the RAK board (same port
 * as the RAK GPS module). Configure Meshtastic to use external GPS on that
 * port; it will read NMEA and use the position for the node.
 *
 * Wiring: AGT TX (config MESHTASTIC_TX_PIN) -> Meshtastic J10 RX
 * Baud: 4800 (MESHTASTIC_BAUD) - standard NMEA
 */

// Initialize Meshtastic interface
void MeshtasticInterface_init();

// Send position update to Meshtastic (NMEA GGA + RMC)
bool MeshtasticInterface_sendPosition(GPSData* gpsData);

// Send "no fix" NMEA so pin 39 has activity for debugging (UART monitor)
void MeshtasticInterface_sendNoFixNMEA();

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
