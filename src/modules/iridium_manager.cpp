#include "modules/iridium_manager.h"
#include "config.h"
#include <Arduino.h>

static IridiumSBD* modemPtr = nullptr;
static bool modemConfigured = false;
static bool modemReady = false;

#define SUPERCAP_CHARGE_TIMEOUT_MS  120000
#define SUPERCAP_TOPUP_MS           10000
#define INITIAL_CHARGE_DELAY_MS     2000
#define VBAT_LOW                    2.8

bool ISBDCallback() {
    if ((millis() / 250) % 2 == 1)
        digitalWrite(LED_WHITE, HIGH);
    else
        digitalWrite(LED_WHITE, LOW);
    return true;
}

static float getBusVoltage() {
    pinMode(BUS_VOLTAGE_MON_EN, OUTPUT);
    digitalWrite(BUS_VOLTAGE_MON_EN, HIGH);
    delay(10);
    int rawValue = analogRead(BUS_VOLTAGE_PIN);
    float voltage = (rawValue / 16384.0) * 2.0 * 3.0;
    digitalWrite(BUS_VOLTAGE_MON_EN, LOW);
    return voltage;
}

// ============================================================================
// AS179 RF ANTENNA SWITCH CONTROL
// GNSS_EN and IRIDIUM_PWR_EN must NEVER both be active simultaneously.
//   GPS mode:     GNSS_EN=LOW (on), IRIDIUM_PWR_EN=LOW (off)
//   Iridium mode: GNSS_EN=HIGH (off), IRIDIUM_PWR_EN=HIGH (on)
// ============================================================================
static void configureGnssEnPin() {
    am_hal_gpio_pincfg_t pinCfg = g_AM_HAL_GPIO_OUTPUT;
    pinCfg.eGPOutcfg = AM_HAL_GPIO_PIN_OUTCFG_OPENDRAIN;
    pin_config(PinName(GNSS_EN), pinCfg);
    delay(1);
}

static void switchToGPS() {
    Serial.println(F("[RF] Switch -> GPS (Iridium OFF first, then GPS ON)"));
    digitalWrite(IRIDIUM_PWR_EN, LOW);
    digitalWrite(IRIDIUM_SLEEP, LOW);
    delay(250);
    configureGnssEnPin();
    digitalWrite(GNSS_EN, LOW);
    delay(750);
}

static void switchToIridium() {
    Serial.println(F("[RF] Switch -> Iridium (GPS OFF first, then Iridium ON)"));
    configureGnssEnPin();
    digitalWrite(GNSS_EN, HIGH);
    delay(250);
    digitalWrite(IRIDIUM_PWR_EN, HIGH);
    delay(250);
}

// ============================================================================
// SUPERCAP CHARGE (shared by init and send)
// ============================================================================
static bool chargeSupercaps() {
    Serial.println(F("Iridium: Charging supercaps..."));
    digitalWrite(SUPERCAP_CHG_EN, HIGH);
    delay(INITIAL_CHARGE_DELAY_MS);

    unsigned long startTime = millis();
    bool pgoodReceived = false;
    while (!pgoodReceived && (millis() - startTime < SUPERCAP_CHARGE_TIMEOUT_MS)) {
        pgoodReceived = (digitalRead(SUPERCAP_PGOOD) == HIGH);
        float vbat = getBusVoltage();
        if (vbat < VBAT_LOW) {
            Serial.print(F("Iridium: Battery too low ("));
            Serial.print(vbat, 2);
            Serial.println(F("V) - aborting"));
            digitalWrite(SUPERCAP_CHG_EN, LOW);
            return false;
        }
        delay(100);
    }

    if (!pgoodReceived) {
        Serial.println(F("Iridium: Supercap charge timeout!"));
        digitalWrite(SUPERCAP_CHG_EN, LOW);
        return false;
    }

    Serial.println(F("Iridium: PGOOD, top-up..."));
    unsigned long topupStart = millis();
    while ((millis() - topupStart) < SUPERCAP_TOPUP_MS) {
        float vbat = getBusVoltage();
        if (vbat < VBAT_LOW) {
            Serial.println(F("Iridium: Battery low during top-up - aborting"));
            digitalWrite(SUPERCAP_CHG_EN, LOW);
            return false;
        }
        delay(100);
    }

    Serial.println(F("Iridium: Supercap charging complete"));
    return true;
}

// ============================================================================
// CONFIGURE (called at startup, does NOT power on the modem)
// ============================================================================
void IridiumManager_configure(IridiumSBD* modem) {
    modemPtr = modem;
    pinMode(SUPERCAP_PGOOD, INPUT);
    pinMode(IRIDIUM_RI, INPUT);
    pinMode(IRIDIUM_NA, INPUT);
    IRIDIUM_SERIAL.begin(IRIDIUM_BAUD);
    modemPtr->setPowerProfile(IridiumSBD::USB_POWER_PROFILE);
    modemPtr->adjustSendReceiveTimeout(180);
    modemConfigured = true;
}

// ============================================================================
// INIT (full power-on test, handles antenna switch)
// ============================================================================
bool IridiumManager_init(IridiumSBD* modem) {
    modemPtr = modem;
    modemConfigured = true;

    pinMode(SUPERCAP_PGOOD, INPUT);
    pinMode(IRIDIUM_RI, INPUT);
    pinMode(IRIDIUM_NA, INPUT);

    Serial.println(F("================================"));
    Serial.println(F("Iridium: Initialization starting"));
    Serial.println(F("================================"));

    switchToIridium();

    if (!chargeSupercaps()) {
        switchToGPS();
        return false;
    }

    IRIDIUM_SERIAL.begin(IRIDIUM_BAUD);
    modemPtr->setPowerProfile(IridiumSBD::USB_POWER_PROFILE);
    modemPtr->adjustSendReceiveTimeout(180);

    Serial.println(F("Iridium: Starting modem..."));
    int err = modemPtr->begin();

    if (err != ISBD_SUCCESS) {
        Serial.print(F("Iridium: Begin failed: error "));
        Serial.println(err);
        modemPtr->sleep();
        digitalWrite(SUPERCAP_CHG_EN, LOW);
        switchToGPS();
        modemReady = false;
        return false;
    }

    Serial.println(F("================================"));
    Serial.println(F("Iridium: Initialized successfully"));
    Serial.println(F("================================"));

    modemPtr->sleep();
    digitalWrite(SUPERCAP_CHG_EN, LOW);
    switchToGPS();

    modemReady = true;
    return true;
}

// ============================================================================
// Helper: format float as integer.fraction for Apollo3 (no %f in snprintf)
// ============================================================================
static int appendFloat(char* buf, int pos, int maxLen, double val, int decimals) {
    long intPart = (long)val;
    double fracVal = val - intPart;
    if (fracVal < 0) fracVal = -fracVal;

    long fracPart = 0;
    long multiplier = 1;
    for (int i = 0; i < decimals; i++) multiplier *= 10;
    fracPart = (long)(fracVal * multiplier + 0.5);
    if (fracPart >= multiplier) { fracPart = 0; intPart += (val >= 0) ? 1 : -1; }

    int written;
    if (decimals == 6)
        written = snprintf(buf + pos, maxLen - pos, "%ld.%06ld", intPart, fracPart);
    else if (decimals == 2)
        written = snprintf(buf + pos, maxLen - pos, "%ld.%02ld", intPart, fracPart);
    else
        written = snprintf(buf + pos, maxLen - pos, "%ld.%01ld", intPart, fracPart);
    return (written > 0) ? pos + written : pos;
}

// ============================================================================
// SEND — full antenna switch cycle:
//   GPS off -> supercap charge -> modem wake -> send -> modem sleep -> GPS on
// Returns true on success. Caller must re-init GPS after (power was cycled).
// ============================================================================
static bool iridiumSendText(const char* message) {
    if (modemPtr == nullptr || !modemConfigured) {
        Serial.println(F("Iridium: Not configured"));
        return false;
    }

    float vbat = getBusVoltage();
    if (vbat < VBAT_LOW) {
        Serial.print(F("Iridium: Battery too low ("));
        Serial.print(vbat, 2);
        Serial.println(F("V)"));
        return false;
    }

    switchToIridium();

    if (!chargeSupercaps()) {
        switchToGPS();
        return false;
    }

    Serial.println(F("Iridium: Waking modem..."));
    int err = modemPtr->begin();
    if (err != ISBD_SUCCESS) {
        Serial.print(F("Iridium: Wake failed, error="));
        Serial.println(err);
        modemPtr->sleep();
        digitalWrite(SUPERCAP_CHG_EN, LOW);
        switchToGPS();
        return false;
    }
    modemReady = true;

    int csq = -1;
    err = modemPtr->getSignalQuality(csq);
    if (err == ISBD_SUCCESS) {
        Serial.print(F("Iridium: Signal quality (CSQ): "));
        Serial.print(csq);
        Serial.println(F("/5"));
    }

    Serial.print(F("Iridium: Sending: "));
    Serial.println(message);
    err = modemPtr->sendSBDText(message);

    bool success = (err == ISBD_SUCCESS);
    if (success) {
        Serial.println(F("Iridium: >>> Message sent! <<<"));
        modemPtr->clearBuffers(ISBD_CLEAR_MO);
    } else {
        Serial.print(F("Iridium: Send failed: error "));
        Serial.println(err);
    }

    modemPtr->sleep();
    digitalWrite(SUPERCAP_CHG_EN, LOW);
    switchToGPS();

    return success;
}

bool IridiumManager_sendPosition(GPSData* gpsData, BatteryData* battData) {
    if (!gpsData->valid) {
        Serial.println(F("Iridium: GPS data not valid"));
        return false;
    }

    char message[340];
    int pos = 0;
    pos += snprintf(message + pos, sizeof(message) - pos, "LAT:");
    pos = appendFloat(message, pos, sizeof(message), gpsData->latitude, 6);
    pos += snprintf(message + pos, sizeof(message) - pos, ",LON:");
    pos = appendFloat(message, pos, sizeof(message), gpsData->longitude, 6);
    pos += snprintf(message + pos, sizeof(message) - pos, ",ALT:");
    pos = appendFloat(message, pos, sizeof(message), gpsData->altitude, 1);
    pos += snprintf(message + pos, sizeof(message) - pos, ",SPD:");
    pos = appendFloat(message, pos, sizeof(message), gpsData->speed, 1);
    pos += snprintf(message + pos, sizeof(message) - pos, ",SAT:%d,BATT:", gpsData->satellites);
    pos = appendFloat(message, pos, sizeof(message), battData->voltage, 2);
    pos += snprintf(message + pos, sizeof(message) - pos, "V,");
    pos = appendFloat(message, pos, sizeof(message), battData->current, 2);
    snprintf(message + pos, sizeof(message) - pos, "A");

    return iridiumSendText(message);
}

bool IridiumManager_sendMissionReport(GPSData* gpsData, MissionData* mission) {
    if (!gpsData->valid) return false;
    float vbat = getBusVoltage();

    char message[340];
    int pos = 0;
    pos += snprintf(message + pos, sizeof(message) - pos, "LAT:");
    pos = appendFloat(message, pos, sizeof(message), gpsData->latitude, 6);
    pos += snprintf(message + pos, sizeof(message) - pos, ",LON:");
    pos = appendFloat(message, pos, sizeof(message), gpsData->longitude, 6);
    pos += snprintf(message + pos, sizeof(message) - pos, ",ALT:");
    pos = appendFloat(message, pos, sizeof(message), gpsData->altitude, 1);
    pos += snprintf(message + pos, sizeof(message) - pos, ",SAT:%d", gpsData->satellites);

    if (mission) {
        float v = mission->battery_voltage > 0 ? mission->battery_voltage : vbat;
        pos += snprintf(message + pos, sizeof(message) - pos, ",V:");
        pos = appendFloat(message, pos, sizeof(message), v, 2);
        pos += snprintf(message + pos, sizeof(message) - pos, ",LEAK:%d,MAXD:",
                        mission->leak_detected ? 1 : 0);
        pos = appendFloat(message, pos, sizeof(message), mission->max_depth_m, 1);
        snprintf(message + pos, sizeof(message) - pos, "m");
    } else {
        pos += snprintf(message + pos, sizeof(message) - pos, ",V:");
        pos = appendFloat(message, pos, sizeof(message), vbat, 2);
    }

    return iridiumSendText(message);
}

bool IridiumManager_sendMessage(const char* message) {
    return iridiumSendText(message);
}

bool IridiumManager_sendBinary(uint8_t* data, size_t length) {
    if (modemPtr == nullptr || !modemConfigured) return false;
    if (length > 340) return false;

    float vbat = getBusVoltage();
    if (vbat < VBAT_LOW) return false;

    switchToIridium();
    if (!chargeSupercaps()) {
        switchToGPS();
        return false;
    }

    int err = modemPtr->begin();
    if (err != ISBD_SUCCESS) {
        modemPtr->sleep();
        digitalWrite(SUPERCAP_CHG_EN, LOW);
        switchToGPS();
        return false;
    }

    err = modemPtr->sendSBDBinary(data, length);
    bool success = (err == ISBD_SUCCESS);
    if (success) modemPtr->clearBuffers(ISBD_CLEAR_MO);

    modemPtr->sleep();
    digitalWrite(SUPERCAP_CHG_EN, LOW);
    switchToGPS();

    return success;
}

bool IridiumManager_checkMessages(char* buffer, size_t* bufferSize) {
    if (modemPtr == nullptr || !modemConfigured) return false;

    switchToIridium();
    if (!chargeSupercaps()) {
        switchToGPS();
        return false;
    }

    int err = modemPtr->begin();
    if (err != ISBD_SUCCESS) {
        modemPtr->sleep();
        digitalWrite(SUPERCAP_CHG_EN, LOW);
        switchToGPS();
        return false;
    }

    size_t rxBufferSize = *bufferSize;
    err = modemPtr->sendReceiveSBDText(nullptr, (uint8_t*)buffer, rxBufferSize);

    bool success = (err == ISBD_SUCCESS && rxBufferSize > 0);
    if (success) *bufferSize = rxBufferSize;

    modemPtr->sleep();
    digitalWrite(SUPERCAP_CHG_EN, LOW);
    switchToGPS();

    return success;
}

int IridiumManager_getSignalQuality() {
    if (modemPtr == nullptr || !modemConfigured) return -1;

    switchToIridium();
    if (!chargeSupercaps()) {
        switchToGPS();
        return -1;
    }

    int err = modemPtr->begin();
    if (err != ISBD_SUCCESS) {
        modemPtr->sleep();
        digitalWrite(SUPERCAP_CHG_EN, LOW);
        switchToGPS();
        return -1;
    }

    int signalQuality;
    err = modemPtr->getSignalQuality(signalQuality);

    modemPtr->sleep();
    digitalWrite(SUPERCAP_CHG_EN, LOW);
    switchToGPS();

    return (err == ISBD_SUCCESS) ? signalQuality : -1;
}

void IridiumManager_sleep() {
    if (modemPtr == nullptr) return;
    Serial.println(F("Iridium: Powering down..."));
    modemPtr->sleep();
    digitalWrite(IRIDIUM_SLEEP, LOW);
    delay(100);
    digitalWrite(IRIDIUM_PWR_EN, LOW);
    delay(100);
    digitalWrite(SUPERCAP_CHG_EN, LOW);
    Serial.println(F("Iridium: Power down complete"));
}

void IridiumManager_wake() {
    switchToIridium();
    digitalWrite(SUPERCAP_CHG_EN, HIGH);
    delay(100);
    digitalWrite(IRIDIUM_SLEEP, HIGH);
}

bool IridiumManager_isReady() {
    return modemConfigured;
}
