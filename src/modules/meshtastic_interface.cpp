#include "modules/meshtastic_interface.h"
#include "config.h"
#include <Arduino.h>

/*
 * Meshtastic RAK4603 Serial Interface
 *
 * The RAK4603 can be communicated with using serial AT commands.
 * This module sends GPS position and telemetry data to the Meshtastic network.
 *
 * Reference: https://docs.meshtastic.org/docs/configuration/radio/lora/
 */

static bool initialized = false;

void MeshtasticInterface_init() {
    // Initialize serial port for Meshtastic on SPI header pins
    // GPIO6 (MISO header) = TX2 to RAK4603 RX
    // GPIO7 (MOSI header) = RX2 from RAK4603 TX

    // Configure Serial2 pins for Artemis/Apollo3
    MESHTASTIC_SERIAL.begin(MESHTASTIC_BAUD);
    // Note: Apollo3 Serial2 pin configuration may need to be set via variant files
    // or using setPins() if available in the core

    delay(500);

    Serial.println(F("Meshtastic: Interface initialized on GPIO6/GPIO7 (SPI header)"));

    // Send initialization/wake command
    MESHTASTIC_SERIAL.println("AT");
    delay(100);

    // Clear any startup messages
    while (MESHTASTIC_SERIAL.available()) {
        MESHTASTIC_SERIAL.read();
    }

    initialized = true;
}

bool MeshtasticInterface_sendPosition(GPSData* gpsData) {
    if (!initialized || !gpsData->valid) {
        return false;
    }

    // Format position string
    // Using a simple text format that Meshtastic can relay
    char positionMsg[200];
    snprintf(positionMsg, sizeof(positionMsg),
             "POS:%.6f,%.6f,%.1fm,%dsat",
             gpsData->latitude,
             gpsData->longitude,
             gpsData->altitude,
             gpsData->satellites);

    // Send via Meshtastic serial interface
    // Format depends on RAK4603 firmware - adjust as needed
    MESHTASTIC_SERIAL.print("AT+SEND=");
    MESHTASTIC_SERIAL.println(positionMsg);

    Serial.print(F("Meshtastic: Sent position - "));
    Serial.println(positionMsg);

    // Wait for response
    unsigned long startTime = millis();
    while (millis() - startTime < 1000) {
        if (MESHTASTIC_SERIAL.available()) {
            String response = MESHTASTIC_SERIAL.readStringUntil('\n');
            if (response.indexOf("OK") >= 0) {
                return true;
            }
        }
    }

    return false;
}

bool MeshtasticInterface_sendText(const char* message) {
    if (!initialized) {
        return false;
    }

    MESHTASTIC_SERIAL.print("AT+SEND=");
    MESHTASTIC_SERIAL.println(message);

    Serial.print(F("Meshtastic: Sent text - "));
    Serial.println(message);

    // Wait for response
    unsigned long startTime = millis();
    while (millis() - startTime < 1000) {
        if (MESHTASTIC_SERIAL.available()) {
            String response = MESHTASTIC_SERIAL.readStringUntil('\n');
            if (response.indexOf("OK") >= 0) {
                return true;
            }
        }
    }

    return false;
}

bool MeshtasticInterface_sendTelemetry(float voltage, float current) {
    if (!initialized) {
        return false;
    }

    char telemetryMsg[100];
    snprintf(telemetryMsg, sizeof(telemetryMsg),
             "TELEM:V=%.2f,I=%.2f",
             voltage, current);

    return MeshtasticInterface_sendText(telemetryMsg);
}

bool MeshtasticInterface_checkMessages() {
    if (!initialized) {
        return false;
    }

    // Check for incoming messages
    MESHTASTIC_SERIAL.println("AT+RECV");
    delay(100);

    if (MESHTASTIC_SERIAL.available()) {
        String message = MESHTASTIC_SERIAL.readStringUntil('\n');
        if (message.length() > 0) {
            Serial.print(F("Meshtastic: Received - "));
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

    // Process any incoming serial data from Meshtastic
    while (MESHTASTIC_SERIAL.available()) {
        char c = MESHTASTIC_SERIAL.read();
        Serial.write(c);  // Echo to debug serial
    }
}
