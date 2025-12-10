#include "modules/meshtastic_interface.h"
#include "config.h"
#include <Arduino.h>

/*
 * Meshtastic RAK4603 Serial Interface
 *
 * Communication Protocol: TEXT MODE (TEXTMSG)
 *
 * The RAK4603 must be pre-configured via Meshtastic CLI in TEXTMSG mode:
 *   meshtastic --set serial.enabled true
 *   meshtastic --set serial.mode TEXTMSG
 *   meshtastic --set serial.baud BAUD_115200
 *   meshtastic --set serial.rxd 21
 *   meshtastic --set serial.txd 22
 *   meshtastic --set serial.timeout 0
 *   meshtastic --commit
 *
 * In TEXTMSG mode, the RAK4603 accepts plain text messages over serial
 * and automatically wraps them in Meshtastic packets for mesh broadcast.
 * No AT commands or Protocol Buffer encoding needed on AGT side.
 *
 * Message Format:
 *   - Position: POS:<lat>,<lon>,<alt>m,<sats>sat
 *   - State: STATE:<state>,<time>s
 *   - Telemetry: TELEM:<key>=<value>,<key>=<value>
 *   - Alert: ALERT:<message>
 *
 * Reference: https://meshtastic.org/docs/configuration/module/serial/
 */

static bool initialized = false;

void MeshtasticInterface_init() {
    // Initialize serial port for Meshtastic on SPI header pins
    // GPIO6 (MISO header) = TX2 to RAK4603 RX (pin 21)
    // GPIO7 (MOSI header) = RX2 from RAK4603 TX (pin 22)

    // Configure Serial2 for Artemis/Apollo3
    MESHTASTIC_SERIAL.begin(MESHTASTIC_BAUD);
    // Note: Apollo3 Serial2 pin configuration may need to be set via variant files
    // or using setPins() if available in the core

    delay(500);

    Serial.println(F("================================"));
    Serial.println(F("Meshtastic: Interface initialized"));
    Serial.println(F("  Mode: TEXT (TEXTMSG)"));
    Serial.println(F("  Pins: GPIO6/GPIO7 (SPI header)"));
    Serial.println(F("  Baud: 115200"));
    Serial.println(F(""));
    Serial.println(F("IMPORTANT: RAK4603 must be pre-configured"));
    Serial.println(F("in TEXTMSG mode via Meshtastic CLI"));
    Serial.println(F("================================"));

    // No initialization commands needed in TEXT mode
    // RAK4603 automatically handles mesh protocol

    initialized = true;
}

bool MeshtasticInterface_sendPosition(GPSData* gpsData) {
    if (!initialized || !gpsData->valid) {
        return false;
    }

    // Format position message as simple text
    // RAK4603 in TEXTMSG mode automatically wraps this in Meshtastic packet
    char positionMsg[100];
    snprintf(positionMsg, sizeof(positionMsg),
             "POS:%.6f,%.6f,%.1fm,%dsat",
             gpsData->latitude,
             gpsData->longitude,
             gpsData->altitude,
             gpsData->satellites);

    // Send directly - RAK4603 handles Meshtastic protocol
    MESHTASTIC_SERIAL.println(positionMsg);

    Serial.print(F("Mesh TX: "));
    Serial.println(positionMsg);

    // No ACK in TEXT mode - fire and forget
    // Messages appear on other Meshtastic devices as text
    return true;
}

bool MeshtasticInterface_sendText(const char* message) {
    if (!initialized) {
        return false;
    }

    // Send text directly - RAK4603 handles mesh protocol
    MESHTASTIC_SERIAL.println(message);

    Serial.print(F("Mesh TX: "));
    Serial.println(message);

    return true;
}

bool MeshtasticInterface_sendTelemetry(float voltage, float current) {
    if (!initialized) {
        return false;
    }

    char telemetryMsg[100];
    snprintf(telemetryMsg, sizeof(telemetryMsg),
             "TELEM:V=%.2fV,I=%.2fA",
             voltage, current);

    return MeshtasticInterface_sendText(telemetryMsg);
}

bool MeshtasticInterface_sendState(const char* stateName, uint32_t timeInState) {
    if (!initialized) {
        return false;
    }

    char stateMsg[100];
    snprintf(stateMsg, sizeof(stateMsg),
             "STATE:%s,%lus",
             stateName, timeInState);

    return MeshtasticInterface_sendText(stateMsg);
}

bool MeshtasticInterface_sendAlert(const char* alertMessage) {
    if (!initialized) {
        return false;
    }

    char alertMsg[100];
    snprintf(alertMsg, sizeof(alertMsg),
             "ALERT:%s",
             alertMessage);

    return MeshtasticInterface_sendText(alertMsg);
}

bool MeshtasticInterface_checkMessages() {
    if (!initialized) {
        return false;
    }

    // In TEXTMSG mode, incoming mesh messages are forwarded to serial
    // Check if we have received any messages
    if (MESHTASTIC_SERIAL.available()) {
        String message = MESHTASTIC_SERIAL.readStringUntil('\n');
        message.trim();

        if (message.length() > 0) {
            Serial.print(F("Mesh RX: "));
            Serial.println(message);
            return true;
        }
    }

    return false;
}

void MeshtasticInterface_update() {
    if (!initialized) {
        return;
    }

    // In TEXTMSG mode, incoming mesh messages appear as simple text lines
    // Forward any received messages to debug serial
    while (MESHTASTIC_SERIAL.available()) {
        String message = MESHTASTIC_SERIAL.readStringUntil('\n');
        message.trim();

        if (message.length() > 0) {
            Serial.print(F("Mesh RX: "));
            Serial.println(message);

            // TODO: Parse and act on received commands if needed
            // Example: "CMD:emergency" could trigger emergency state
        }
    }
}
