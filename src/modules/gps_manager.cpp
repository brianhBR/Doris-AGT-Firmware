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
static bool pvtReceived = false;        // True after first UBX-NAV-PVT parsed

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

            // Enable NAV-PVT output BEFORE saving so it's included in BBR.
            // enableGNSS() triggers a delayed receiver restart on the M8;
            // if setAutoPVT runs after saveConfiguration, the NAV-PVT
            // output rate is RAM-only and lost when the restart occurs.
            gpsPtr->setAutoPVT(true);

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

    // Re-enable in RAM for BBR-retained and light-init paths (BBR
    // already has it from the first-boot save above, but the library's
    // internal flag still needs to be set each session).
    gpsPtr->setAutoPVT(true);

    // NOTE: powerOnTime / ttffLogged are anchored at the GNSS_EN->LOW
    // transition in the callers (GPSManager_init / GPSManager_reinit),
    // NOT here. If we latched the anchor at this point we'd skip the
    // first ~2-5s of u-blox-on time (boot + I2C setup + config save),
    // which would bias reported TTFF artificially low and could mask a
    // cold start as a warm one.

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
    // Anchor TTFF measurement at the actual V_MAIN-to-module edge so hot
    // vs warm vs cold classification reflects u-blox wall time, not
    // post-I2C-config time.
    powerOnTime = millis();
    ttffLogged = false;
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
    // Anchor TTFF at the V_MAIN edge (see GPSManager_init for rationale).
    powerOnTime = millis();
    ttffLogged = false;
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

// Forward-declared for processPVT() — defined near GPSManager_update()
static unsigned long lastPVTTime = 0;

// Coin-cell (V_BCKP / ML414H) health captured at first-ever PVT after
// GPSManager_init. If validDate && validTime are set on the very first
// PVT, the u-blox internal RTC rode through the last power-off on coin
// cell alone — the cell is healthy. If they're clear, the cell was
// depleted (or absent) and the module is doing a cold start.
static bool     bootDiagDone        = false; // one-shot flag
static bool     bootBBRTimeRetained = false; // latched at first PVT
static bool     bootBBREphemFast    = false; // latched true if TTFF < 5s (hot)
static uint32_t bootFirstFixTTFF_ms = 0;     // latched at first valid fix

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
    pvtReceived = true;
    lastPVTTime = millis();
    // UBX-NAV-PVT payload offsets (u-blox M8 protocol spec)
    currentGPSData.year      = ubxU16(4);
    currentGPSData.month     = ubxBuf[6];
    currentGPSData.day       = ubxBuf[7];
    currentGPSData.hour      = ubxBuf[8];
    currentGPSData.minute    = ubxBuf[9];
    currentGPSData.second    = ubxBuf[10];
    // Byte 11: "valid" bitfield.
    //   bit 0 = validDate, bit 1 = validTime, bit 2 = fullyResolved.
    // On warm start (BBR retained via V_BCKP coin cell) the module asserts
    // validDate/validTime from its internal RTC well before a position fix.
    const uint8_t validFlags = ubxBuf[11];
    currentGPSData.date_valid          = (validFlags & 0x01) != 0;
    currentGPSData.time_valid          = (validFlags & 0x02) != 0;
    currentGPSData.time_fully_resolved = (validFlags & 0x04) != 0;
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

    // One-shot coin-cell verdict on the very first PVT after GPSManager_init.
    // `validDate && validTime` on the first PVT means the u-blox's internal
    // RTC rode through the last power-off on V_BCKP alone — proof that the
    // ML414H coin cell is retaining charge. If they're clear, either the
    // cell has never been charged long enough, or it's failed.
    if (!bootDiagDone) {
        bootDiagDone        = true;
        bootBBRTimeRetained = currentGPSData.date_valid && currentGPSData.time_valid;
        char bootMsg[50];
        if (bootBBRTimeRetained) {
            snprintf(bootMsg, sizeof(bootMsg),
                     "GPS: Coin cell OK, BBR time %04u-%02u-%02u",
                     currentGPSData.year, currentGPSData.month, currentGPSData.day);
            MAVLinkInterface_sendStatusText(6, bootMsg);
        } else {
            snprintf(bootMsg, sizeof(bootMsg),
                     "GPS: Coin cell DEPLETED, BBR time lost (cold)");
            MAVLinkInterface_sendStatusText(4, bootMsg);
        }
        DebugPrintln(bootMsg);
    }

    if (currentGPSData.fixType >= 2 && currentGPSData.satellites >= GPS_MIN_SATS) {
        currentGPSData.valid = true;
        lastFixAttempt = now;
        digitalWrite(LED_WHITE, HIGH);

        if (!ttffLogged && powerOnTime > 0) {
            unsigned long ttff = now - powerOnTime;
            const char* startType = (ttff < 5000) ? "hot" :
                                    (ttff < 35000) ? "warm" : "cold";
            bootFirstFixTTFF_ms = (uint32_t)ttff;
            bootBBREphemFast    = (ttff < 5000); // hot start => BBR had ephemeris
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

static uint8_t i2cFailCount = 0;
static uint8_t dataReadFailCount = 0;
static bool    watchdogFired = false;

#define PVT_WATCHDOG_MS      5000
#define I2C_FAIL_RECOVERY    10
#define UBX_READ_CHUNK       32     // bytes per loop (up from 16)

static void gpsFullReinit() {
    MAVLinkInterface_sendStatusText(4, "GPS: PVT watchdog reinit");
    ubxReset();
    i2cBusRecovery();

    TwoWire& wire = getAGTWire();
    wire.begin();
    delay(50);
    wire.setClock(100000);

    if (gpsPtr->begin(wire)) {
        gpsPtr->setAutoPVT(true);
    }
    lastPVTTime = millis();
    watchdogFired = true;
}

void GPSManager_update() {
    if (gpsPtr == nullptr) return;

    unsigned long now = millis();

    // PVT watchdog: runs UNCONDITIONALLY regardless of I2C state.
    // If no PVT has been parsed in PVT_WATCHDOG_MS, the GPS I2C
    // pipeline is broken — do a full re-init (bus recovery + begin +
    // setAutoPVT) to restart it.
    if (lastPVTTime > 0 && (now - lastPVTTime) > PVT_WATCHDOG_MS) {
        gpsFullReinit();
        return;
    }

    TwoWire& wire = getAGTWire();

    // Read available byte count (2 bytes from register 0xFD)
    wire.beginTransmission(UBX_I2C_ADDR);
    wire.write(0xFD);
    if (wire.endTransmission(false) != 0) {
        if (++i2cFailCount >= I2C_FAIL_RECOVERY) {
            i2cFailCount = 0;
            i2cBusRecovery();
            wire.begin();
            delay(10);
            wire.setClock(100000);
        }
        return;
    }
    i2cFailCount = 0;

    uint8_t got = wire.requestFrom((uint8_t)UBX_I2C_ADDR, (uint8_t)2);
    if (got < 2) return;
    uint16_t avail = ((uint16_t)wire.read() << 8) | wire.read();
    if (avail == 0 || avail == 0xFFFF) return;

    uint8_t toRead = (avail > UBX_READ_CHUNK) ? UBX_READ_CHUNK : (uint8_t)avail;
    wire.beginTransmission(UBX_I2C_ADDR);
    wire.write(0xFF);
    if (wire.endTransmission(false) != 0) {
        dataReadFailCount++;
        return;
    }
    dataReadFailCount = 0;

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

bool GPSManager_hasPVT() {
    return pvtReceived;
}

bool GPSManager_hasValidTime() {
    return pvtReceived &&
           currentGPSData.date_valid &&
           currentGPSData.time_valid;
}

void GPSManager_getDataString(char* buffer, size_t bufferSize) {
    if (!currentGPSData.valid) {
        snprintf(buffer, bufferSize, "NO FIX");
        return;
    }

    // Apollo3 snprintf lacks %f and doesn't zero-pad %0Nld; format manually
    bool latNeg = (currentGPSData.latitude < 0.0);
    double absLat = latNeg ? -currentGPSData.latitude : currentGPSData.latitude;
    long latInt = (long)absLat;
    long latFrac = (long)((absLat - latInt) * 1000000 + 0.5);
    if (latFrac >= 1000000) { latFrac = 0; latInt++; }

    bool lonNeg = (currentGPSData.longitude < 0.0);
    double absLon = lonNeg ? -currentGPSData.longitude : currentGPSData.longitude;
    long lonInt = (long)absLon;
    long lonFrac = (long)((absLon - lonInt) * 1000000 + 0.5);
    if (lonFrac >= 1000000) { lonFrac = 0; lonInt++; }

    long altInt = (long)currentGPSData.altitude;
    long spdInt = (long)(currentGPSData.speed * 10);

    char latFracStr[8], lonFracStr[8];
    long lf = latFrac;
    for (int j = 5; j >= 0; j--) { latFracStr[j] = '0' + (int)(lf % 10); lf /= 10; }
    latFracStr[6] = '\0';
    long nf = lonFrac;
    for (int j = 5; j >= 0; j--) { lonFracStr[j] = '0' + (int)(nf % 10); nf /= 10; }
    lonFracStr[6] = '\0';

    snprintf(buffer, bufferSize,
             "%s%ld.%s,%s%ld.%s,%ld,%ld.%01ld,%d",
             latNeg ? "-" : "", latInt, latFracStr,
             lonNeg ? "-" : "", lonInt, lonFracStr,
             altInt, spdInt / 10, abs(spdInt % 10),
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
    // Anchor TTFF at the V_MAIN edge, BEFORE module boot + I2C setup, so
    // hot/warm/cold classification reflects wall time at the module.
    powerOnTime = millis();
    ttffLogged = false;
    delay(500);

    if (gpsPtr != nullptr) {
        TwoWire& agtWire = getAGTWire();
        if (!gpsPtr->begin(agtWire)) {
            DebugPrintln(F("GPS: Wake failed, retrying..."));
            delay(500);
            gpsPtr->begin(agtWire);
        }
        gpsPtr->setAutoPVT(true);

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

    // Coin-cell / V_BCKP health summary — derived from boot-time state:
    //  * boot BBR time retained  -> coin cell held the u-blox RTC alive
    //  * first fix TTFF < 5s     -> coin cell held the ephemeris too
    //  * BBR config saved        -> we wrote CFG to BBR (survives w/ cell)
    // With all three true, the cell is in good shape. If BBR time was
    // lost, the cell is depleted or absent — expect a cold start every
    // power-up until it's been charged for many hours on main power.
    if (bootDiagDone) {
        const char* verdict;
        if (bootBBRTimeRetained && bootBBREphemFast) {
            verdict = "HEALTHY";
        } else if (bootBBRTimeRetained) {
            verdict = "weak";   // time survived, ephemeris did not
        } else {
            verdict = "DEPLETED";
        }
        snprintf(msg, sizeof(msg), "GPS: Coin cell %s (time=%s ttff=%lus)",
                 verdict,
                 bootBBRTimeRetained ? "kept" : "lost",
                 (unsigned long)(bootFirstFixTTFF_ms / 1000));
        MAVLinkInterface_sendStatusText(6, msg);
    } else {
        MAVLinkInterface_sendStatusText(4, "GPS: Coin cell unknown (no PVT yet)");
    }

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
