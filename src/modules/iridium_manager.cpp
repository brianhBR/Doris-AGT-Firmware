#include "modules/iridium_manager.h"
#include "modules/doris_protocol.h"
#include "config.h"
#include <Arduino.h>

static IridiumSBD* modemPtr = nullptr;
static bool modemConfigured = false;
static bool modemReady = false;

#define SUPERCAP_CHARGE_TIMEOUT_MS  120000
#define SUPERCAP_TOPUP_MS           10000
#define INITIAL_CHARGE_DELAY_MS     2000
#define VBAT_LOW                    2.8

// ============================================================================
// Apollo3 Serial1 workarounds (SparkFun Arduino_Apollo3 issue #423)
// The RX pin must be disabled when the modem is off to prevent a
// power-on glitch that can hang the core.  beginSerialPort re-enables
// the pins with a weak pull-up on RX before each modem session.
// ============================================================================
void IridiumSBD::beginSerialPort()
{
    am_hal_gpio_pincfg_t pinConfigTx = g_AM_BSP_GPIO_COM_UART_TX;
    pinConfigTx.uFuncSel = AM_HAL_PIN_24_UART1TX;
    pin_config(D24, pinConfigTx);

    am_hal_gpio_pincfg_t pinConfigRx = g_AM_BSP_GPIO_COM_UART_RX;
    pinConfigRx.uFuncSel = AM_HAL_PIN_25_UART1RX;
    pinConfigRx.ePullup = AM_HAL_GPIO_PIN_PULLUP_WEAK;
    pin_config(D25, pinConfigRx);

    Serial1.begin(19200);
}

void IridiumSBD::endSerialPort()
{
    am_hal_gpio_pinconfig(PinName(D25), g_AM_HAL_GPIO_DISABLE);
}

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

static void antennaToGPS() {
    DebugPrintln(F("[RF] Antenna -> GPS"));
    configureGnssEnPin();
    digitalWrite(GNSS_EN, LOW);
    delay(750);
}

static void antennaToIridium() {
    DebugPrintln(F("[RF] Antenna -> Iridium"));
    configureGnssEnPin();
    digitalWrite(GNSS_EN, HIGH);
    delay(250);
}

// ============================================================================
// MODEM POWER CONTROL
// SparkFun reference sequence:
//   Power-on:  charge supercaps -> iridiumPwrEN HIGH -> delay(1s) -> modem.begin()
//   Power-off: modem.sleep() -> endSerialPort -> iridiumSleep LOW ->
//              iridiumPwrEN LOW -> superCapChgEN LOW -> Serial1.end()
// ============================================================================
static void modemPowerDown() {
    DebugPrintln(F("Iridium: Powering down modem..."));
    modemPtr->sleep();
    modemPtr->endSerialPort();
    digitalWrite(IRIDIUM_SLEEP, LOW);
    delay(1);
    digitalWrite(IRIDIUM_PWR_EN, LOW);
    delay(1);
    digitalWrite(SUPERCAP_CHG_EN, LOW);
    IRIDIUM_SERIAL.end();
}

// ============================================================================
// SUPERCAP CHARGE (shared by init and send)
// ============================================================================
static bool chargeSupercaps() {
    DebugPrintln(F("Iridium: Charging supercaps..."));
    digitalWrite(SUPERCAP_CHG_EN, HIGH);
    delay(INITIAL_CHARGE_DELAY_MS);

    unsigned long startTime = millis();
    bool pgoodReceived = false;
    while (!pgoodReceived && (millis() - startTime < SUPERCAP_CHARGE_TIMEOUT_MS)) {
        pgoodReceived = (digitalRead(SUPERCAP_PGOOD) == HIGH);
        float vbat = getBusVoltage();
        if (vbat < VBAT_LOW) {
            DebugPrint(F("Iridium: Battery too low ("));
            DebugPrint(vbat, 2);
            DebugPrintln(F("V) - aborting"));
            digitalWrite(SUPERCAP_CHG_EN, LOW);
            return false;
        }
        delay(100);
    }

    if (!pgoodReceived) {
        DebugPrintln(F("Iridium: Supercap charge timeout!"));
        digitalWrite(SUPERCAP_CHG_EN, LOW);
        return false;
    }

    DebugPrintln(F("Iridium: PGOOD, top-up..."));
    unsigned long topupStart = millis();
    while ((millis() - topupStart) < SUPERCAP_TOPUP_MS) {
        float vbat = getBusVoltage();
        if (vbat < VBAT_LOW) {
            DebugPrintln(F("Iridium: Battery low during top-up - aborting"));
            digitalWrite(SUPERCAP_CHG_EN, LOW);
            return false;
        }
        delay(100);
    }

    DebugPrintln(F("Iridium: Supercap charging complete"));
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
    modemPtr->setPowerProfile(IridiumSBD::USB_POWER_PROFILE);
    modemPtr->adjustSendReceiveTimeout(180);
    modemPtr->endSerialPort();
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

    DebugPrintln(F("================================"));
    DebugPrintln(F("Iridium: Initialization starting"));
    DebugPrintln(F("================================"));

    antennaToIridium();

    if (!chargeSupercaps()) {
        antennaToGPS();
        return false;
    }

    DebugPrintln(F("Iridium: Enabling 9603N power..."));
    digitalWrite(IRIDIUM_PWR_EN, HIGH);
    delay(1000);

    modemPtr->setPowerProfile(IridiumSBD::USB_POWER_PROFILE);
    modemPtr->adjustSendReceiveTimeout(180);

    DebugPrintln(F("Iridium: Starting modem..."));
    int err = modemPtr->begin();

    if (err != ISBD_SUCCESS) {
        DebugPrint(F("Iridium: Begin failed: error "));
        DebugPrintln(err);
        modemPowerDown();
        antennaToGPS();
        modemReady = false;
        return false;
    }

    DebugPrintln(F("================================"));
    DebugPrintln(F("Iridium: Initialized successfully"));
    DebugPrintln(F("================================"));

    modemPowerDown();
    antennaToGPS();

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
        DebugPrintln(F("Iridium: Not configured"));
        return false;
    }

    float vbat = getBusVoltage();
    if (vbat < VBAT_LOW) {
        DebugPrint(F("Iridium: Battery too low ("));
        DebugPrint(vbat, 2);
        DebugPrintln(F("V)"));
        return false;
    }

    antennaToIridium();

    if (!chargeSupercaps()) {
        antennaToGPS();
        return false;
    }

    DebugPrintln(F("Iridium: Enabling 9603N power..."));
    digitalWrite(IRIDIUM_PWR_EN, HIGH);
    delay(1000);

    DebugPrintln(F("Iridium: Starting modem..."));
    int err = modemPtr->begin();
    if (err != ISBD_SUCCESS) {
        DebugPrint(F("Iridium: Begin failed, error="));
        DebugPrintln(err);
        modemPowerDown();
        antennaToGPS();
        return false;
    }
    modemReady = true;

    int csq = -1;
    err = modemPtr->getSignalQuality(csq);
    if (err == ISBD_SUCCESS) {
        DebugPrint(F("Iridium: Signal quality (CSQ): "));
        DebugPrint(csq);
        DebugPrintln(F("/5"));
    }

    bool success = false;
    for (int attempt = 1; attempt <= MAX_IRIDIUM_RETRY; attempt++) {
        DebugPrint(F("Iridium: Send attempt "));
        DebugPrint(attempt);
        DebugPrint(F("/"));
        DebugPrintln(MAX_IRIDIUM_RETRY);

        err = modemPtr->sendSBDText(message);
        if (err == ISBD_SUCCESS) {
            DebugPrintln(F("Iridium: >>> Message sent! <<<"));
            modemPtr->clearBuffers(ISBD_CLEAR_MO);
            success = true;
            break;
        }

        DebugPrint(F("Iridium: Attempt failed, error "));
        DebugPrintln(err);
        if (attempt < MAX_IRIDIUM_RETRY) delay(3000);
    }

    modemPowerDown();
    antennaToGPS();

    return success;
}

bool IridiumManager_sendPosition(GPSData* gpsData, BatteryData* battData) {
    if (!gpsData->valid) {
        DebugPrintln(F("Iridium: GPS data not valid"));
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

    antennaToIridium();
    if (!chargeSupercaps()) {
        antennaToGPS();
        return false;
    }

    digitalWrite(IRIDIUM_PWR_EN, HIGH);
    delay(1000);

    int err = modemPtr->begin();
    if (err != ISBD_SUCCESS) {
        modemPowerDown();
        antennaToGPS();
        return false;
    }

    err = modemPtr->sendSBDBinary(data, length);
    bool success = (err == ISBD_SUCCESS);
    if (success) modemPtr->clearBuffers(ISBD_CLEAR_MO);

    modemPowerDown();
    antennaToGPS();

    return success;
}

bool IridiumManager_checkMessages(char* buffer, size_t* bufferSize) {
    if (modemPtr == nullptr || !modemConfigured) return false;

    antennaToIridium();
    if (!chargeSupercaps()) {
        antennaToGPS();
        return false;
    }

    digitalWrite(IRIDIUM_PWR_EN, HIGH);
    delay(1000);

    int err = modemPtr->begin();
    if (err != ISBD_SUCCESS) {
        modemPowerDown();
        antennaToGPS();
        return false;
    }

    size_t rxBufferSize = *bufferSize;
    err = modemPtr->sendReceiveSBDText(nullptr, (uint8_t*)buffer, rxBufferSize);

    bool success = (err == ISBD_SUCCESS && rxBufferSize > 0);
    if (success) *bufferSize = rxBufferSize;

    modemPowerDown();
    antennaToGPS();

    return success;
}

// ============================================================================
// DORIS BINARY PROTOCOL: send report + receive MT in one SBD session
// ============================================================================
static bool parseMTResponse(const uint8_t* rxBuf, size_t rxLen,
                             uint8_t* mtMsgId, DorisConfig* mtConfig, DorisCommand* mtCommand) {
    if (rxLen < DORIS_MSG_HEADER_SIZE) return false;
    uint8_t id = DorisProtocol_parseMT(rxBuf, rxLen, mtConfig, mtCommand);
    if (id == 0) return false;
    if (mtMsgId) *mtMsgId = id;
    return true;
}

bool IridiumManager_sendDorisReport(const DorisReport* report,
                                     uint8_t* mtMsgId,
                                     DorisConfig* mtConfig,
                                     DorisCommand* mtCommand) {
    if (modemPtr == nullptr || !modemConfigured || report == nullptr) {
        DebugPrintln(F("Iridium: Not configured"));
        return false;
    }

    float vbat = getBusVoltage();
    if (vbat < VBAT_LOW) {
        DebugPrint(F("Iridium: Battery too low ("));
        DebugPrint(vbat, 2);
        DebugPrintln(F("V)"));
        return false;
    }

    uint8_t txBuf[DORIS_REPORT_WIRE_SIZE];
    size_t txLen = DorisProtocol_serializeReport(report, txBuf, sizeof(txBuf));
    if (txLen == 0) return false;

    antennaToIridium();
    if (!chargeSupercaps()) {
        antennaToGPS();
        return false;
    }

    DebugPrintln(F("Iridium: Enabling 9603N power..."));
    digitalWrite(IRIDIUM_PWR_EN, HIGH);
    delay(1000);

    DebugPrintln(F("Iridium: Starting modem..."));
    int err = modemPtr->begin();
    if (err != ISBD_SUCCESS) {
        DebugPrint(F("Iridium: Begin failed, error="));
        DebugPrintln(err);
        modemPowerDown();
        antennaToGPS();
        return false;
    }
    modemReady = true;

    int csq = -1;
    err = modemPtr->getSignalQuality(csq);
    if (err == ISBD_SUCCESS) {
        DebugPrint(F("Iridium: Signal (CSQ): "));
        DebugPrint(csq);
        DebugPrintln(F("/5"));
    }

    DebugPrint(F("Iridium: Sending Doris report ("));
    DebugPrint(txLen);
    DebugPrintln(F(" bytes)"));

    uint8_t rxBuf[270];
    size_t rxLen = 0;
    bool sendOk = false;

    for (int attempt = 1; attempt <= MAX_IRIDIUM_RETRY; attempt++) {
        DebugPrint(F("Iridium: Send attempt "));
        DebugPrint(attempt);
        DebugPrint(F("/"));
        DebugPrintln(MAX_IRIDIUM_RETRY);

        rxLen = sizeof(rxBuf);
        err = modemPtr->sendReceiveSBDBinary(txBuf, txLen, rxBuf, rxLen);
        if (err == ISBD_SUCCESS) {
            sendOk = true;
            break;
        }

        DebugPrint(F("Iridium: Attempt failed, error "));
        DebugPrintln(err);
        if (attempt < MAX_IRIDIUM_RETRY) delay(3000);
    }

    if (sendOk) {
        DebugPrintln(F("Iridium: >>> Report sent! <<<"));
        modemPtr->clearBuffers(ISBD_CLEAR_MO);

        if (rxLen > 0) {
            DebugPrint(F("Iridium: MT message received ("));
            DebugPrint(rxLen);
            DebugPrintln(F(" bytes)"));
            parseMTResponse(rxBuf, rxLen, mtMsgId, mtConfig, mtCommand);
        }
    } else {
        DebugPrint(F("Iridium: All "));
        DebugPrint(MAX_IRIDIUM_RETRY);
        DebugPrintln(F(" attempts failed"));
    }

    modemPowerDown();
    antennaToGPS();

    return sendOk;
}

bool IridiumManager_checkMT(uint8_t* mtMsgId,
                              DorisConfig* mtConfig,
                              DorisCommand* mtCommand) {
    if (modemPtr == nullptr || !modemConfigured) return false;

    antennaToIridium();
    if (!chargeSupercaps()) {
        antennaToGPS();
        return false;
    }

    digitalWrite(IRIDIUM_PWR_EN, HIGH);
    delay(1000);

    int err = modemPtr->begin();
    if (err != ISBD_SUCCESS) {
        modemPowerDown();
        antennaToGPS();
        return false;
    }

    uint8_t rxBuf[270];
    size_t rxLen = sizeof(rxBuf);
    err = modemPtr->sendReceiveSBDBinary(nullptr, 0, rxBuf, rxLen);

    bool gotMessage = false;
    if (err == ISBD_SUCCESS && rxLen > 0) {
        gotMessage = parseMTResponse(rxBuf, rxLen, mtMsgId, mtConfig, mtCommand);
    }

    modemPowerDown();
    antennaToGPS();

    return gotMessage;
}

int IridiumManager_getSignalQuality() {
    if (modemPtr == nullptr || !modemConfigured) return -1;

    antennaToIridium();
    if (!chargeSupercaps()) {
        antennaToGPS();
        return -1;
    }

    digitalWrite(IRIDIUM_PWR_EN, HIGH);
    delay(1000);

    int err = modemPtr->begin();
    if (err != ISBD_SUCCESS) {
        modemPowerDown();
        antennaToGPS();
        return -1;
    }

    int signalQuality;
    err = modemPtr->getSignalQuality(signalQuality);

    modemPowerDown();
    antennaToGPS();

    return (err == ISBD_SUCCESS) ? signalQuality : -1;
}

void IridiumManager_sleep() {
    if (modemPtr == nullptr) return;
    DebugPrintln(F("Iridium: Powering down..."));
    modemPowerDown();
    DebugPrintln(F("Iridium: Power down complete"));
}

void IridiumManager_wake() {
    antennaToIridium();
    digitalWrite(SUPERCAP_CHG_EN, HIGH);
    delay(100);
    digitalWrite(IRIDIUM_PWR_EN, HIGH);
    delay(1000);
}

bool IridiumManager_isReady() {
    return modemConfigured;
}
