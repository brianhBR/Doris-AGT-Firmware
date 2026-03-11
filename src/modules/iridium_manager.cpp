#include "modules/iridium_manager.h"
#include "config.h"
#include <Arduino.h>

static IridiumSBD* modemPtr = nullptr;
static bool modemReady = false;

// Timing constants from SparkFun examples
#define SUPERCAP_CHARGE_TIMEOUT_MS  120000  // 2 minutes for 1F @ 150mA
#define SUPERCAP_TOPUP_MS           10000   // 10 seconds top-up charge
#define INITIAL_CHARGE_DELAY_MS     2000    // Initial settling time
#define VBAT_LOW                    2.8     // Minimum battery voltage

// Callback for Iridium diagnostics
bool ISBDCallback() {
    // Feed watchdog or update status LEDs here if needed
    return true;
}

// Helper function to read bus voltage
static float getBusVoltage() {
    // Enable bus voltage monitor
    pinMode(BUS_VOLTAGE_MON_EN, OUTPUT);
    digitalWrite(BUS_VOLTAGE_MON_EN, HIGH);
    delay(10);  // Allow time for reading to stabilize

    // Read voltage (divided by 3 on the AGT board)
    int rawValue = analogRead(BUS_VOLTAGE_PIN);
    float voltage = (rawValue / 16384.0) * 2.0 * 3.0;  // Apollo3 ADC is 14-bit, 2V ref, divide-by-3

    // Disable bus voltage monitor to save power
    digitalWrite(BUS_VOLTAGE_MON_EN, LOW);

    return voltage;
}

bool IridiumManager_init(IridiumSBD* modem) {
    modemPtr = modem;

    // Initialize pins - follow SparkFun example setup order
    pinMode(IRIDIUM_PWR_EN, OUTPUT);
    pinMode(IRIDIUM_SLEEP, OUTPUT);
    pinMode(SUPERCAP_CHG_EN, OUTPUT);
    pinMode(SUPERCAP_PGOOD, INPUT);
    pinMode(IRIDIUM_RI, INPUT);
    pinMode(IRIDIUM_NA, INPUT);

    // Start with everything OFF (safe state)
    digitalWrite(IRIDIUM_PWR_EN, LOW);
    digitalWrite(IRIDIUM_SLEEP, LOW);
    digitalWrite(SUPERCAP_CHG_EN, LOW);

    Serial.println(F("================================"));
    Serial.println(F("Iridium: Initialization starting"));
    Serial.println(F("================================"));

    // Step 1: Enable the supercapacitor charger
    Serial.println(F("Iridium: Enabling supercapacitor charger..."));
    digitalWrite(SUPERCAP_CHG_EN, HIGH);

    // Step 2: Initial delay for charger settling
    Serial.println(F("Iridium: Waiting for supercapacitors to charge..."));
    delay(INITIAL_CHARGE_DELAY_MS);

    // Step 3: Wait for PGOOD to go HIGH (with timeout and battery monitoring)
    bool pgoodReceived = false;
    unsigned long startTime = millis();

    while (!pgoodReceived && (millis() - startTime < SUPERCAP_CHARGE_TIMEOUT_MS)) {
        pgoodReceived = (digitalRead(SUPERCAP_PGOOD) == HIGH);

        // Check battery voltage - abort if too low
        float vbat = getBusVoltage();
        if (vbat < VBAT_LOW) {
            Serial.print(F("Iridium: Battery voltage too low ("));
            Serial.print(vbat, 2);
            Serial.println(F("V) - aborting"));
            digitalWrite(SUPERCAP_CHG_EN, LOW);
            return false;
        }

        delay(100);  // Don't pound the bus voltage monitor too hard
    }

    if (!pgoodReceived) {
        Serial.println(F("Iridium: Supercap charge timeout!"));
        digitalWrite(SUPERCAP_CHG_EN, LOW);
        return false;
    }

    Serial.println(F("Iridium: Supercap PGOOD received"));

    // Step 4: CRITICAL - Give supercaps extra time to top-up charge
    Serial.println(F("Iridium: Top-up charging supercapacitors..."));
    unsigned long topupStart = millis();

    while ((millis() - topupStart) < SUPERCAP_TOPUP_MS) {
        // Continue monitoring battery voltage during top-up
        float vbat = getBusVoltage();
        if (vbat < VBAT_LOW) {
            Serial.print(F("Iridium: Battery voltage too low during top-up ("));
            Serial.print(vbat, 2);
            Serial.println(F("V) - aborting"));
            digitalWrite(SUPERCAP_CHG_EN, LOW);
            return false;
        }

        delay(100);
    }

    Serial.println(F("Iridium: Supercap charging complete"));

    // Step 5: Enable power for the 9603N
    Serial.println(F("Iridium: Enabling 9603N power..."));
    digitalWrite(IRIDIUM_PWR_EN, HIGH);
    delay(1000);  // SparkFun uses 1 second delay here

    // Step 6: Initialize Serial1 for Iridium communication
    // Note: SparkFun examples handle this via IridiumSBD::beginSerialPort
    // but we're using the standard IridiumSBD library which doesn't expose that
    IRIDIUM_SERIAL.begin(IRIDIUM_BAUD);

    // Step 7: Set power profile - use USB profile to relax timing for supercap recharge
    Serial.println(F("Iridium: Configuring power profile..."));
    modemPtr->setPowerProfile(IridiumSBD::USB_POWER_PROFILE);
    modemPtr->adjustSendReceiveTimeout(180);

    // Step 8: Begin modem (this can take a while)
    Serial.println(F("Iridium: Starting modem..."));
    int err = modemPtr->begin();

    if (err != ISBD_SUCCESS) {
        Serial.print(F("Iridium: Begin failed: error "));
        Serial.println(err);

        // Clean up on failure
        digitalWrite(IRIDIUM_SLEEP, LOW);
        delay(1000);
        digitalWrite(IRIDIUM_PWR_EN, LOW);
        delay(1000);
        digitalWrite(SUPERCAP_CHG_EN, LOW);

        modemReady = false;
        return false;
    }

    Serial.println(F("================================"));
    Serial.println(F("Iridium: Initialized successfully"));
    Serial.println(F("================================"));

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

    // Check battery voltage before transmission
    float vbat = getBusVoltage();
    if (vbat < VBAT_LOW) {
        Serial.print(F("Iridium: Battery too low for transmission ("));
        Serial.print(vbat, 2);
        Serial.println(F("V)"));
        return false;
    }

    // Format message following SparkFun format
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

    Serial.print(F("Iridium: Transmitting: "));
    Serial.println(message);

    // Wake modem (already done during init, but ensure it's awake)
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
        return false;
    }

    Serial.println(F("Iridium: >>> Message sent! <<<"));

    // Clear the Mobile Originated message buffer (SparkFun recommendation)
    Serial.println(F("Iridium: Clearing MO buffer..."));
    err = modemPtr->clearBuffers(ISBD_CLEAR_MO);

    if (err != ISBD_SUCCESS) {
        Serial.print(F("Iridium: Clear buffer warning: error "));
        Serial.println(err);
        // Don't fail on buffer clear error
    }

    return true;
}

bool IridiumManager_sendMissionReport(GPSData* gpsData, MissionData* mission) {
    if (modemPtr == nullptr || !modemReady || !gpsData->valid) {
        return false;
    }
    float vbat = getBusVoltage();
    if (vbat < VBAT_LOW) return false;

    char message[340];
    if (mission) {
        snprintf(message, sizeof(message),
                 "LAT:%.6f,LON:%.6f,ALT:%.1f,SAT:%d,V:%.2f,LEAK:%d,MAXD:%.1fm",
                 gpsData->latitude, gpsData->longitude, gpsData->altitude,
                 gpsData->satellites, mission->battery_voltage > 0 ? mission->battery_voltage : vbat,
                 mission->leak_detected ? 1 : 0, mission->max_depth_m);
    } else {
        snprintf(message, sizeof(message),
                 "LAT:%.6f,LON:%.6f,ALT:%.1f,SAT:%d,V:%.2f",
                 gpsData->latitude, gpsData->longitude, gpsData->altitude,
                 gpsData->satellites, vbat);
    }

    digitalWrite(IRIDIUM_SLEEP, HIGH);
    delay(100);
    int err = modemPtr->sendSBDText(message);
    if (err == ISBD_SUCCESS) {
        modemPtr->clearBuffers(ISBD_CLEAR_MO);
    }
    return (err == ISBD_SUCCESS);
}

bool IridiumManager_sendMessage(const char* message) {
    if (modemPtr == nullptr || !modemReady) {
        return false;
    }

    // Check battery voltage
    float vbat = getBusVoltage();
    if (vbat < VBAT_LOW) {
        Serial.print(F("Iridium: Battery too low ("));
        Serial.print(vbat, 2);
        Serial.println(F("V)"));
        return false;
    }

    digitalWrite(IRIDIUM_SLEEP, HIGH);
    delay(100);

    int err = modemPtr->sendSBDText(message);

    if (err == ISBD_SUCCESS) {
        // Clear MO buffer after successful send
        modemPtr->clearBuffers(ISBD_CLEAR_MO);
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

    // Check battery voltage
    float vbat = getBusVoltage();
    if (vbat < VBAT_LOW) {
        return false;
    }

    digitalWrite(IRIDIUM_SLEEP, HIGH);
    delay(100);

    int err = modemPtr->sendSBDBinary(data, length);

    if (err == ISBD_SUCCESS) {
        // Clear MO buffer after successful send
        modemPtr->clearBuffers(ISBD_CLEAR_MO);
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
    int err = modemPtr->sendReceiveSBDText(nullptr, (uint8_t*)buffer, rxBufferSize);

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

    if (err == ISBD_SUCCESS) {
        return signalQuality;
    }

    return -1;
}

void IridiumManager_sleep() {
    if (modemPtr == nullptr) {
        return;
    }

    Serial.println(F("Iridium: Powering down..."));

    // Follow SparkFun power-down sequence
    // Step 1: Put modem to sleep
    Serial.println(F("Iridium: Putting 9603N to sleep..."));
    int err = modemPtr->sleep();
    if (err != ISBD_SUCCESS) {
        Serial.print(F("Iridium: Sleep warning: error "));
        Serial.println(err);
    }

    // Step 2: Disable 9603N via ON/OFF pin
    Serial.println(F("Iridium: Disabling 9603N power..."));
    digitalWrite(IRIDIUM_SLEEP, LOW);
    delay(1000);

    // Step 3: Disable Iridium power
    digitalWrite(IRIDIUM_PWR_EN, LOW);
    delay(1000);

    // Step 4: Disable supercapacitor charger
    Serial.println(F("Iridium: Disabling supercapacitor charger..."));
    digitalWrite(SUPERCAP_CHG_EN, LOW);

    Serial.println(F("Iridium: Power down complete"));
}

void IridiumManager_wake() {
    // This would require re-running the full init procedure
    // Just enable the hardware for now
    digitalWrite(SUPERCAP_CHG_EN, HIGH);
    delay(100);
    digitalWrite(IRIDIUM_PWR_EN, HIGH);
    delay(100);
    digitalWrite(IRIDIUM_SLEEP, HIGH);
}

bool IridiumManager_isReady() {
    return modemReady;
}
