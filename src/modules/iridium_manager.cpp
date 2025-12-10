#include "modules/iridium_manager.h"
#include "config.h"
#include <Arduino.h>

static IridiumSBD* modemPtr = nullptr;
static bool modemReady = false;

// Callback for Iridium diagnostics
bool ISBDCallback() {
    // Feed watchdog or update status LEDs here if needed
    return true;
}

bool IridiumManager_init(IridiumSBD* modem) {
    modemPtr = modem;

    // Enable Iridium power
    pinMode(IRIDIUM_PWR_EN, OUTPUT);
    pinMode(IRIDIUM_SLEEP, OUTPUT);
    pinMode(SUPERCAP_CHG_EN, OUTPUT);
    pinMode(SUPERCAP_PGOOD, INPUT);

    digitalWrite(IRIDIUM_PWR_EN, HIGH);  // Power on
    digitalWrite(IRIDIUM_SLEEP, HIGH);   // Wake up
    digitalWrite(SUPERCAP_CHG_EN, HIGH); // Enable supercap charger

    Serial.println(F("Iridium: Waiting for supercap to charge..."));

    // Wait for supercap to charge (PGOOD signal)
    unsigned long startTime = millis();
    while (digitalRead(SUPERCAP_PGOOD) == LOW) {
        delay(100);
        if (millis() - startTime > 60000) {  // 60 second timeout
            Serial.println(F("Iridium: Supercap charge timeout!"));
            return false;
        }
    }

    Serial.println(F("Iridium: Supercap charged"));

    // Initialize serial port
    IRIDIUM_SERIAL.begin(IRIDIUM_BAUD);
    delay(1000);

    // Configure modem
    // The IridiumSBD library used here doesn't provide attachConsole/attachDiagnostics
    // (those were present in other variants). Configure timeouts and power profile directly.
    modemPtr->setPowerProfile(IridiumSBD::DEFAULT_POWER_PROFILE);
    modemPtr->adjustSendReceiveTimeout(180);

    // Begin modem
    Serial.println(F("Iridium: Starting modem..."));
    int err = modemPtr->begin();

    if (err != ISBD_SUCCESS) {
        Serial.print(F("Iridium: Begin failed: error "));
        Serial.println(err);
        modemReady = false;
        return false;
    }

    Serial.println(F("Iridium: Initialized successfully"));
    modemReady = true;
    return true;
}

bool IridiumManager_sendPosition(GPSData* gpsData, BatteryData* battData) {
    if (modemPtr == nullptr || !modemReady) {
        Serial.println(F("Iridium: Modem not ready"));
        return false;
    }

    if (!gpsData->valid) {
        Serial.println(F("Iridium: GPS data not valid"));
        return false;
    }

    // Format message
    char message[340];  // Iridium max message size
    snprintf(message, sizeof(message),
             "LAT:%.6f,LON:%.6f,ALT:%.1f,SPD:%.1f,SAT:%d,BATT:%.2fV,%.2fA",
             gpsData->latitude,
             gpsData->longitude,
             gpsData->altitude,
             gpsData->speed,
             gpsData->satellites,
             battData->voltage,
             battData->current);

    Serial.print(F("Iridium: Sending: "));
    Serial.println(message);

    // Wake modem
    digitalWrite(IRIDIUM_SLEEP, HIGH);
    delay(100);

    // Get signal quality first
    int signalQuality;
    int err = modemPtr->getSignalQuality(signalQuality);

    if (err != ISBD_SUCCESS) {
        Serial.println(F("Iridium: Signal quality check failed"));
        return false;
    }

    Serial.print(F("Iridium: Signal quality: "));
    Serial.println(signalQuality);

    if (signalQuality < 2) {
        Serial.println(F("Iridium: Signal too weak"));
        return false;
    }

    // Send message
    err = modemPtr->sendSBDText(message);

    if (err != ISBD_SUCCESS) {
        Serial.print(F("Iridium: Send failed: error "));
        Serial.println(err);

        if (IRIDIUM_SLEEP_ENABLED) {
            digitalWrite(IRIDIUM_SLEEP, LOW);
        }
        return false;
    }

    Serial.println(F("Iridium: Message sent successfully"));

    // Sleep modem to save power
    if (IRIDIUM_SLEEP_ENABLED) {
        digitalWrite(IRIDIUM_SLEEP, LOW);
    }

    return true;
}

bool IridiumManager_sendMessage(const char* message) {
    if (modemPtr == nullptr || !modemReady) {
        return false;
    }

    digitalWrite(IRIDIUM_SLEEP, HIGH);
    delay(100);

    int err = modemPtr->sendSBDText(message);

    if (IRIDIUM_SLEEP_ENABLED) {
        digitalWrite(IRIDIUM_SLEEP, LOW);
    }

    return (err == ISBD_SUCCESS);
}

bool IridiumManager_sendBinary(uint8_t* data, size_t length) {
    if (modemPtr == nullptr || !modemReady) {
        return false;
    }

    if (length > 340) {  // Iridium max message size
        return false;
    }

    digitalWrite(IRIDIUM_SLEEP, HIGH);
    delay(100);

    int err = modemPtr->sendSBDBinary(data, length);

    if (IRIDIUM_SLEEP_ENABLED) {
        digitalWrite(IRIDIUM_SLEEP, LOW);
    }

    return (err == ISBD_SUCCESS);
}

bool IridiumManager_checkMessages(char* buffer, size_t* bufferSize) {
    if (modemPtr == nullptr || !modemReady) {
        return false;
    }

    digitalWrite(IRIDIUM_SLEEP, HIGH);
    delay(100);

    size_t rxBufferSize = *bufferSize;
    // library expects a uint8_t* rx buffer
    int err = modemPtr->sendReceiveSBDText(nullptr, (uint8_t*)buffer, rxBufferSize);

    if (IRIDIUM_SLEEP_ENABLED) {
        digitalWrite(IRIDIUM_SLEEP, LOW);
    }

    if (err == ISBD_SUCCESS && rxBufferSize > 0) {
        *bufferSize = rxBufferSize;
        return true;
    }

    return false;
}

int IridiumManager_getSignalQuality() {
    if (modemPtr == nullptr || !modemReady) {
        return -1;
    }

    int signalQuality;
    digitalWrite(IRIDIUM_SLEEP, HIGH);
    delay(100);

    int err = modemPtr->getSignalQuality(signalQuality);

    if (IRIDIUM_SLEEP_ENABLED) {
        digitalWrite(IRIDIUM_SLEEP, LOW);
    }

    if (err == ISBD_SUCCESS) {
        return signalQuality;
    }

    return -1;
}

void IridiumManager_sleep() {
    digitalWrite(IRIDIUM_SLEEP, LOW);
    digitalWrite(SUPERCAP_CHG_EN, LOW);
    digitalWrite(IRIDIUM_PWR_EN, LOW);
}

void IridiumManager_wake() {
    digitalWrite(IRIDIUM_PWR_EN, HIGH);
    digitalWrite(SUPERCAP_CHG_EN, HIGH);
    delay(100);
    digitalWrite(IRIDIUM_SLEEP, HIGH);
}

bool IridiumManager_isReady() {
    return modemReady;
}
