#include "modules/gps_manager.h"
#include "modules/mavlink_interface.h"
#include "config.h"
#include <Wire.h>

// AGT GPS uses dedicated I2C bus on pins 8 (SCL) and 9 (SDA)
const byte PIN_AGTWIRE_SCL = 8;
const byte PIN_AGTWIRE_SDA = 9;

static TwoWire& getAGTWire() {
    static TwoWire wire(PIN_AGTWIRE_SDA, PIN_AGTWIRE_SCL);
    return wire;
}

static SFE_UBLOX_GNSS* gpsPtr = nullptr;
static GPSData currentGPSData;
static unsigned long lastFixAttempt = 0;
static unsigned long powerOnTime = 0;   // Track when GPS was powered on for TTFF
static bool ttffLogged = false;         // Only log TTFF once per power cycle
static bool configSavedToBBR = false;   // Track whether we've saved config to BBR

// Toggle SCL manually to free a stuck I2C bus (SDA held low by slave).
static void i2cBusRecovery() {
    DebugPrintln(F("GPS: I2C bus recovery..."));
    pinMode(PIN_AGTWIRE_SDA, INPUT);
    pinMode(PIN_AGTWIRE_SCL, OUTPUT);
    for (int i = 0; i < 16; i++) {
        digitalWrite(PIN_AGTWIRE_SCL, HIGH);
        delayMicroseconds(5);
        digitalWrite(PIN_AGTWIRE_SCL, LOW);
        delayMicroseconds(5);
    }
    digitalWrite(PIN_AGTWIRE_SCL, HIGH);
    delayMicroseconds(5);
    // Generate STOP condition
    pinMode(PIN_AGTWIRE_SDA, OUTPUT);
    digitalWrite(PIN_AGTWIRE_SDA, LOW);
    delayMicroseconds(5);
    digitalWrite(PIN_AGTWIRE_SCL, HIGH);
    delayMicroseconds(5);
    digitalWrite(PIN_AGTWIRE_SDA, HIGH);
    delayMicroseconds(5);
}

// fullConfig: true on first boot (configure everything + save to BBR),
//             false on reinit after power cycle (BBR retains config, just reconnect)
static bool initI2CAndGPS(bool fullConfig) {
    i2cBusRecovery();

    TwoWire& agtWire = getAGTWire();
    agtWire.begin();
    delay(100);
    agtWire.setClock(100000);

    am_hal_gpio_pincfg_t sclCfg = g_AM_BSP_GPIO_IOM1_SCL;
    sclCfg.ePullup = AM_HAL_GPIO_PIN_PULLUP_NONE;
    pin_config(PinName(PIN_AGTWIRE_SCL), sclCfg);
    am_hal_gpio_pincfg_t sdaCfg = g_AM_BSP_GPIO_IOM1_SDA;
    sdaCfg.ePullup = AM_HAL_GPIO_PIN_PULLUP_NONE;
    pin_config(PinName(PIN_AGTWIRE_SDA), sdaCfg);
    delay(100);

    if (!gpsPtr->begin(agtWire)) {
        DebugPrintln(F("GPS: ZOE-M8Q not detected on I2C"));
        return false;
    }

    if (fullConfig) {
        // Check if BBR already has our config by testing whether GLONASS
        // is enabled. If it is, we've configured before and BBR is intact —
        // skip reconfiguration to preserve ephemeris for a hot/warm start.
        // CRITICAL: calling enableGNSS() on u-blox M8 triggers a receiver
        // reset (even with unchanged values), wiping ephemeris from RAM.
        bool alreadyConfigured = gpsPtr->isGNSSenabled(SFE_UBLOX_GNSS_ID_GLONASS);

        if (!alreadyConfigured) {
            DebugPrintln(F("GPS: First boot — configuring GPS+GLONASS..."));
            gpsPtr->setI2COutput(COM_TYPE_UBX);
            gpsPtr->setNavigationFrequency(GPS_UPDATE_RATE_HZ);
            gpsPtr->setDynamicModel(DYN_MODEL_PORTABLE);
            gpsPtr->setI2CpollingWait(25);
            gpsPtr->enableGNSS(true, SFE_UBLOX_GNSS_ID_GPS);
            gpsPtr->enableGNSS(true, SFE_UBLOX_GNSS_ID_GLONASS);

            if (gpsPtr->saveConfiguration()) {
                configSavedToBBR = true;
                DebugPrintln(F("GPS: Config saved to BBR (warm starts enabled)"));
            } else {
                DebugPrintln(F("GPS: WARNING - failed to save config to BBR"));
            }
        } else {
            configSavedToBBR = true;
            DebugPrintln(F("GPS: BBR config valid, skipping reconfig (ephemeris preserved)"));
        }
    } else {
        DebugPrintln(F("GPS: Light reinit (BBR config retained)"));
    }

    // autoPVT is a library-side flag, must be set every time
    gpsPtr->setAutoPVT(true);

    powerOnTime = millis();
    ttffLogged = false;

    return true;
}

bool GPSManager_init(SFE_UBLOX_GNSS* gps) {
    gpsPtr = gps;

    // Enable backup battery charging FIRST so BBR is powered before main power-on.
    // This keeps ephemeris + RTC alive across power cycles for hot/warm starts.
    pinMode(GNSS_BCKP_BAT_CHG_EN, OUTPUT);
    digitalWrite(GNSS_BCKP_BAT_CHG_EN, LOW);
    DebugPrintln(F("GPS: Backup battery charging enabled"));

    // Enable GNSS power
    am_hal_gpio_pincfg_t pinCfg = g_AM_HAL_GPIO_OUTPUT;
    pinCfg.eGPOutcfg = AM_HAL_GPIO_PIN_OUTCFG_OPENDRAIN;
    pin_config(PinName(GNSS_EN), pinCfg);
    delay(1);
    digitalWrite(GNSS_EN, LOW);
    delay(2000);

    if (!initI2CAndGPS(true)) {
        DebugPrintln(F("GPS: ZOE-M8Q init FAILED"));
        MAVLinkInterface_sendStatusText(3, "GPS: ZOE-M8Q init FAILED");
        return false;
    }

    currentGPSData.valid = false;
    currentGPSData.satellites = 0;

    const char* initMsg = configSavedToBBR ?
        "GPS: Init OK (BBR valid, warm start)" :
        "GPS: Init OK (fresh config, cold start)";
    DebugPrintln(initMsg);
    MAVLinkInterface_sendStatusText(6, initMsg);
    return true;
}

bool GPSManager_reinit() {
    if (gpsPtr == nullptr) return false;
    DebugPrintln(F("GPS: Re-initializing after power cycle (warm start)..."));

    am_hal_gpio_pincfg_t pinCfg = g_AM_HAL_GPIO_OUTPUT;
    pinCfg.eGPOutcfg = AM_HAL_GPIO_PIN_OUTCFG_OPENDRAIN;
    pin_config(PinName(GNSS_EN), pinCfg);
    delay(1);
    digitalWrite(GNSS_EN, LOW);
    delay(1000);  // Shorter delay — GPS doesn't need full cold-start ramp

    // BBR retains config from initial setup; skip full reconfiguration
    bool useLightInit = configSavedToBBR;
    if (!initI2CAndGPS(!useLightInit)) {
        DebugPrintln(F("GPS: Re-init FAILED, retrying with full config..."));
        delay(1000);
        if (!initI2CAndGPS(true)) {
            DebugPrintln(F("GPS: Re-init FAILED"));
            return false;
        }
    }

    DebugPrintln(F("GPS: Re-init OK"));
    return true;
}

// ============================================================================
// NON-BLOCKING UBX PARSER
// Reads GPS I2C in small chunks (~16 bytes per loop) to avoid blocking
// the main loop. The SparkFun library's getPVT() reads ALL buffered data
// in one call which causes a visible stutter in LED animations.
// ============================================================================
#define UBX_I2C_ADDR       0x42
#define UBX_MAX_PER_LOOP   16     // bytes to read per loop iteration
#define UBX_SYNC1          0xB5
#define UBX_SYNC2          0x62
#define UBX_NAV_CLASS      0x01
#define UBX_NAV_PVT_ID     0x07
#define UBX_NAV_PVT_LEN    92

static uint8_t  ubxBuf[UBX_NAV_PVT_LEN];
static uint16_t ubxPayloadIdx = 0;
static uint8_t  ubxState = 0;       // parser state machine
static uint16_t ubxPayloadLen = 0;
static uint8_t  ubxClass = 0;
static uint8_t  ubxId = 0;
static uint8_t  ubxCkA = 0, ubxCkB = 0;

static void ubxReset() {
    ubxState = 0;
    ubxPayloadIdx = 0;
    ubxCkA = 0;
    ubxCkB = 0;
}

static int32_t ubxI32(uint16_t off) {
    return (int32_t)((uint32_t)ubxBuf[off] | ((uint32_t)ubxBuf[off+1] << 8) |
           ((uint32_t)ubxBuf[off+2] << 16) | ((uint32_t)ubxBuf[off+3] << 24));
}
static uint32_t ubxU32(uint16_t off) {
    return (uint32_t)ubxBuf[off] | ((uint32_t)ubxBuf[off+1] << 8) |
           ((uint32_t)ubxBuf[off+2] << 16) | ((uint32_t)ubxBuf[off+3] << 24);
}
static uint16_t ubxU16(uint16_t off) {
    return (uint16_t)ubxBuf[off] | ((uint16_t)ubxBuf[off+1] << 8);
}

static void processPVT() {
    // UBX-NAV-PVT payload offsets (u-blox M8 protocol spec)
    currentGPSData.year      = ubxU16(4);
    currentGPSData.month     = ubxBuf[6];
    currentGPSData.day       = ubxBuf[7];
    currentGPSData.hour      = ubxBuf[8];
    currentGPSData.minute    = ubxBuf[9];
    currentGPSData.second    = ubxBuf[10];
    currentGPSData.fixType   = ubxBuf[20];
    currentGPSData.satellites = ubxBuf[23];
    currentGPSData.longitude = ubxI32(24) / 10000000.0;
    currentGPSData.latitude  = ubxI32(28) / 10000000.0;
    currentGPSData.altitude      = ubxI32(36) / 1000.0;   // hMSL
    currentGPSData.alt_ellipsoid = ubxI32(32);             // height above ellipsoid (mm)
    currentGPSData.h_acc_mm      = ubxU32(40);             // hAcc (mm)
    currentGPSData.v_acc_mm      = ubxU32(44);             // vAcc (mm)
    currentGPSData.vel_n_mm      = ubxI32(48);             // velN (mm/s)
    currentGPSData.vel_e_mm      = ubxI32(52);             // velE (mm/s)
    currentGPSData.vel_d_mm      = ubxI32(56);             // velD (mm/s)
    currentGPSData.speed         = ubxI32(60) / 1000.0;   // gSpeed mm/s → m/s
    currentGPSData.course        = ubxI32(64) / 100000.0;  // headMot
    currentGPSData.s_acc_mm      = ubxU32(68);             // sAcc (mm/s)
    currentGPSData.hdop          = ubxU16(76) / 100.0;    // pDOP as proxy

    unsigned long now = millis();

    if (currentGPSData.fixType >= 2 && currentGPSData.satellites >= GPS_MIN_SATS) {
        currentGPSData.valid = true;
        lastFixAttempt = now;
        digitalWrite(LED_WHITE, HIGH);

        if (!ttffLogged && powerOnTime > 0) {
            unsigned long ttff = now - powerOnTime;
            const char* startType = (ttff < 5000) ? "hot" :
                                    (ttff < 35000) ? "warm" : "cold";
            char ttffMsg[50];
            snprintf(ttffMsg, sizeof(ttffMsg), "GPS: TTFF=%lus (%s start) sats=%u",
                     ttff / 1000, startType, currentGPSData.satellites);
            MAVLinkInterface_sendStatusText(6, ttffMsg);
            DebugPrintln(ttffMsg);
            ttffLogged = true;
        }

        static unsigned long lastPrint = 0;
        if (now - lastPrint > 10000) {
            DebugPrint(F("GPS: Fix - Lat: "));
            DebugPrint(currentGPSData.latitude, 6);
            DebugPrint(F(" Lon: "));
            DebugPrint(currentGPSData.longitude, 6);
            DebugPrint(F(" Sats: "));
            DebugPrint(currentGPSData.satellites);
            DebugPrint(F(" HDOP: "));
            DebugPrintln(currentGPSData.hdop, 1);
            lastPrint = now;
        }
    } else {
        currentGPSData.valid = false;
        digitalWrite(LED_WHITE, LOW);

        static unsigned long lastSearchPrint = 0;
        if (now - lastSearchPrint > 15000) {
            unsigned long elapsed = (powerOnTime > 0) ? (now - powerOnTime) / 1000 : 0;
            char searchMsg[50];
            snprintf(searchMsg, sizeof(searchMsg), "GPS: Searching sats=%u fix=%u %lus",
                     currentGPSData.satellites, currentGPSData.fixType, elapsed);
            MAVLinkInterface_sendStatusText(6, searchMsg);
            DebugPrintln(searchMsg);
            lastSearchPrint = now;
        }
    }
}

// Feed one byte into the UBX frame parser
static void ubxParseByte(uint8_t b) {
    switch (ubxState) {
        case 0: // waiting for sync1
            if (b == UBX_SYNC1) ubxState = 1;
            break;
        case 1: // waiting for sync2
            ubxState = (b == UBX_SYNC2) ? 2 : 0;
            break;
        case 2: // class
            ubxClass = b;
            ubxCkA = b; ubxCkB = ubxCkA;
            ubxState = 3;
            break;
        case 3: // id
            ubxId = b;
            ubxCkA += b; ubxCkB += ubxCkA;
            ubxState = 4;
            break;
        case 4: // length low byte
            ubxPayloadLen = b;
            ubxCkA += b; ubxCkB += ubxCkA;
            ubxState = 5;
            break;
        case 5: // length high byte
            ubxPayloadLen |= ((uint16_t)b << 8);
            ubxCkA += b; ubxCkB += ubxCkA;
            ubxPayloadIdx = 0;
            ubxState = (ubxPayloadLen > 0) ? 6 : 7;
            break;
        case 6: // payload
            ubxCkA += b; ubxCkB += ubxCkA;
            if (ubxClass == UBX_NAV_CLASS && ubxId == UBX_NAV_PVT_ID &&
                ubxPayloadIdx < UBX_NAV_PVT_LEN) {
                ubxBuf[ubxPayloadIdx] = b;
            }
            ubxPayloadIdx++;
            if (ubxPayloadIdx >= ubxPayloadLen) ubxState = 7;
            break;
        case 7: // checksum A
            if (b == ubxCkA) {
                ubxState = 8;
            } else {
                ubxReset();
            }
            break;
        case 8: // checksum B
            if (b == ubxCkB && ubxClass == UBX_NAV_CLASS &&
                ubxId == UBX_NAV_PVT_ID && ubxPayloadLen == UBX_NAV_PVT_LEN) {
                processPVT();
            }
            ubxReset();
            break;
        default:
            ubxReset();
            break;
    }
}

void GPSManager_update() {
    if (gpsPtr == nullptr) return;

    TwoWire& wire = getAGTWire();

    // Read available byte count (2 bytes from register 0xFD)
    wire.beginTransmission(UBX_I2C_ADDR);
    wire.write(0xFD);
    if (wire.endTransmission(false) != 0) return;
    uint8_t got = wire.requestFrom((uint8_t)UBX_I2C_ADDR, (uint8_t)2);
    if (got < 2) return;
    uint16_t avail = ((uint16_t)wire.read() << 8) | wire.read();

    if (avail == 0 || avail == 0xFFFF) return;

    // Read a small chunk — keeps I2C blocking under ~1ms at 400kHz
    uint8_t toRead = (avail > UBX_MAX_PER_LOOP) ? UBX_MAX_PER_LOOP : (uint8_t)avail;
    wire.beginTransmission(UBX_I2C_ADDR);
    wire.write(0xFF);
    if (wire.endTransmission(false) != 0) return;
    got = wire.requestFrom((uint8_t)UBX_I2C_ADDR, toRead);
    for (uint8_t i = 0; i < got; i++) {
        ubxParseByte(wire.read());
    }
}

bool GPSManager_hasFix() {
    return currentGPSData.valid;
}

GPSData GPSManager_getData() {
    return currentGPSData;
}

void GPSManager_getDataString(char* buffer, size_t bufferSize) {
    if (!currentGPSData.valid) {
        snprintf(buffer, bufferSize, "NO FIX");
        return;
    }

    // Apollo3 snprintf lacks %f support; format manually
    long latInt = (long)currentGPSData.latitude;
    long latFrac = abs((long)((currentGPSData.latitude - latInt) * 1000000));
    long lonInt = (long)currentGPSData.longitude;
    long lonFrac = abs((long)((currentGPSData.longitude - lonInt) * 1000000));
    long altInt = (long)currentGPSData.altitude;
    long spdInt = (long)(currentGPSData.speed * 10);

    snprintf(buffer, bufferSize,
             "%ld.%06ld,%ld.%06ld,%ld,%ld.%01ld,%d",
             latInt, latFrac, lonInt, lonFrac, altInt,
             spdInt / 10, abs(spdInt % 10),
             currentGPSData.satellites);
}

void GPSManager_sleep() {
    if (gpsPtr != nullptr) {
        // Use powerOff instead of cutting GNSS_EN — the backup battery keeps
        // RTC + ephemeris alive in BBR for a hot start on wake (~1 s TTFF).
        gpsPtr->powerOff(0);
    }

    am_hal_gpio_pincfg_t pinCfg = g_AM_HAL_GPIO_OUTPUT;
    pinCfg.eGPOutcfg = AM_HAL_GPIO_PIN_OUTCFG_OPENDRAIN;
    pin_config(PinName(GNSS_EN), pinCfg);
    delay(1);
    digitalWrite(GNSS_EN, HIGH);  // Disable main power (backup battery keeps BBR alive)
    DebugPrintln(F("GPS: Sleep (BBR retained via backup battery)"));
}

void GPSManager_wake() {
    am_hal_gpio_pincfg_t pinCfg = g_AM_HAL_GPIO_OUTPUT;
    pinCfg.eGPOutcfg = AM_HAL_GPIO_PIN_OUTCFG_OPENDRAIN;
    pin_config(PinName(GNSS_EN), pinCfg);
    delay(1);
    digitalWrite(GNSS_EN, LOW);
    delay(500);

    if (gpsPtr != nullptr) {
        TwoWire& agtWire = getAGTWire();
        if (!gpsPtr->begin(agtWire)) {
            DebugPrintln(F("GPS: Wake failed, retrying..."));
            delay(500);
            gpsPtr->begin(agtWire);
        }
        gpsPtr->setAutoPVT(true);

        powerOnTime = millis();
        ttffLogged = false;
        DebugPrintln(F("GPS: Wake (expecting hot/warm start from BBR)"));
    }
}

void GPSManager_printDiagnostics() {
    if (gpsPtr == nullptr) {
        MAVLinkInterface_sendStatusText(4, "GPS: Not initialized");
        return;
    }

    static unsigned long lastDiagTime = 0;
    unsigned long now = millis();
    if (lastDiagTime != 0 && (now - lastDiagTime) < 10000) return;
    lastDiagTime = now;

    char msg[50];

    if (gpsPtr->getNAVSTATUS()) {
        auto &s = gpsPtr->packetUBXNAVSTATUS->data;
        snprintf(msg, sizeof(msg), "GPS: TTFF=%lums up=%lums fix=%u",
                 s.ttff, s.msss, s.gpsFix);
        MAVLinkInterface_sendStatusText(6, msg);

        snprintf(msg, sizeof(msg), "GPS: BBR wkn=%s tow=%s",
                 s.flags.bits.wknSet ? "YES" : "NO",
                 s.flags.bits.towSet ? "YES" : "NO");
        MAVLinkInterface_sendStatusText(6, msg);
    } else {
        MAVLinkInterface_sendStatusText(4, "GPS: NAV-STATUS query failed");
    }

    UBX_MON_HW_data_t hw;
    if (gpsPtr->getHWstatus(&hw)) {
        const char* antNames[] = {"INIT","UNK","OK","SHORT","OPEN"};
        const char* ant = (hw.aStatus <= 4) ? antNames[hw.aStatus] : "?";
        snprintf(msg, sizeof(msg), "GPS: ant=%s noise=%u agc=%u jam=%u",
                 ant, hw.noisePerMS, hw.agcCnt, hw.jamInd);
        MAVLinkInterface_sendStatusText(6, msg);
    } else {
        MAVLinkInterface_sendStatusText(4, "GPS: MON-HW query failed");
    }

    bool glonassOn = gpsPtr->isGNSSenabled(SFE_UBLOX_GNSS_ID_GLONASS);
    snprintf(msg, sizeof(msg), "GPS: GLONASS=%s BBR=%s sats=%u",
             glonassOn ? "ON" : "OFF",
             configSavedToBBR ? "YES" : "NO",
             currentGPSData.satellites);
    MAVLinkInterface_sendStatusText(6, msg);
}
