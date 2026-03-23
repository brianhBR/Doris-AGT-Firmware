#include "modules/gps_manager.h"
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

    if (!initI2CAndGPS(true)) return false;

    currentGPSData.valid = false;
    currentGPSData.satellites = 0;

    DebugPrintln(F("GPS: ZOE-M8Q initialized (GPS+GLONASS, config saved to BBR)"));
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

void GPSManager_update() {
    if (gpsPtr == nullptr) {
        return;
    }

    unsigned long currentMillis = millis();

    // Check if new PVT data is available
    if (gpsPtr->getPVT()) {
        currentGPSData.latitude = gpsPtr->getLatitude() / 10000000.0;
        currentGPSData.longitude = gpsPtr->getLongitude() / 10000000.0;
        currentGPSData.altitude = gpsPtr->getAltitudeMSL() / 1000.0;
        currentGPSData.speed = gpsPtr->getGroundSpeed() / 1000.0;  // m/s
        currentGPSData.course = gpsPtr->getHeading() / 100000.0;
        currentGPSData.satellites = gpsPtr->getSIV();
        currentGPSData.fixType = gpsPtr->getFixType();
        currentGPSData.hdop = gpsPtr->getHorizontalDOP() / 100.0;

        // Get time
        currentGPSData.year = gpsPtr->getYear();
        currentGPSData.month = gpsPtr->getMonth();
        currentGPSData.day = gpsPtr->getDay();
        currentGPSData.hour = gpsPtr->getHour();
        currentGPSData.minute = gpsPtr->getMinute();
        currentGPSData.second = gpsPtr->getSecond();

        // Check if fix is valid
        // Fix type: 0=no fix, 1=dead reckoning, 2=2D, 3=3D, 4=GNSS+dead reckoning, 5=time only
        if (currentGPSData.fixType >= 2 && currentGPSData.satellites >= GPS_MIN_SATS) {
            currentGPSData.valid = true;
            lastFixAttempt = currentMillis;
            digitalWrite(LED_WHITE, HIGH);

            if (!ttffLogged && powerOnTime > 0) {
                unsigned long ttff = currentMillis - powerOnTime;
                DebugPrint(F("GPS: TTFF = "));
                DebugPrint(ttff / 1000);
                DebugPrint(F("s ("));
                if (ttff < 5000)       DebugPrint(F("hot start"));
                else if (ttff < 35000) DebugPrint(F("warm start"));
                else                   DebugPrint(F("cold start"));
                DebugPrintln(F(")"));
                ttffLogged = true;
            }

            static unsigned long lastPrint = 0;
            if (currentMillis - lastPrint > 10000) {
                DebugPrint(F("GPS: Fix - Lat: "));
                DebugPrint(currentGPSData.latitude, 6);
                DebugPrint(F(" Lon: "));
                DebugPrint(currentGPSData.longitude, 6);
                DebugPrint(F(" Sats: "));
                DebugPrint(currentGPSData.satellites);
                DebugPrint(F(" HDOP: "));
                DebugPrintln(currentGPSData.hdop, 1);
                lastPrint = currentMillis;
            }
        } else {
            currentGPSData.valid = false;
            digitalWrite(LED_WHITE, LOW);

            static unsigned long lastSearchPrint = 0;
            if (currentMillis - lastSearchPrint > 15000) {
                DebugPrint(F("GPS: Searching... Sats: "));
                DebugPrint(currentGPSData.satellites);
                DebugPrint(F(" Fix: "));
                DebugPrint(currentGPSData.fixType);
                unsigned long elapsed = (powerOnTime > 0) ? (currentMillis - powerOnTime) / 1000 : 0;
                DebugPrint(F(" Elapsed: "));
                DebugPrint(elapsed);
                DebugPrintln(F("s"));
                lastSearchPrint = currentMillis;
            }
        }
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
        DebugPrintln(F("GPS: Not initialized"));
        return;
    }

    DebugPrintln(F("=== GPS Diagnostics ==="));

    // NAV-STATUS: receiver's own TTFF, time validity, BBR state
    if (gpsPtr->getNAVSTATUS()) {
        auto &s = gpsPtr->packetUBXNAVSTATUS->data;
        DebugPrint(F("  Receiver TTFF: "));
        DebugPrint(s.ttff);
        DebugPrintln(F(" ms"));
        DebugPrint(F("  Uptime (msss): "));
        DebugPrint(s.msss);
        DebugPrintln(F(" ms"));
        DebugPrint(F("  Fix type: "));
        DebugPrintln(s.gpsFix);
        DebugPrint(F("  Week # valid (wknSet): "));
        DebugPrintln(s.flags.bits.wknSet ? F("YES (BBR RTC intact)") : F("NO (BBR lost)"));
        DebugPrint(F("  TOW valid (towSet): "));
        DebugPrintln(s.flags.bits.towSet ? F("YES") : F("NO"));
    } else {
        DebugPrintln(F("  NAV-STATUS: query failed"));
    }

    // MON-HW: antenna status, noise, jamming
    UBX_MON_HW_data_t hw;
    if (gpsPtr->getHWstatus(&hw)) {
        DebugPrint(F("  Antenna status: "));
        switch (hw.aStatus) {
            case 0: DebugPrintln(F("INIT")); break;
            case 1: DebugPrintln(F("UNKNOWN")); break;
            case 2: DebugPrintln(F("OK")); break;
            case 3: DebugPrintln(F("SHORT")); break;
            case 4: DebugPrintln(F("OPEN")); break;
            default: DebugPrintln(hw.aStatus); break;
        }
        DebugPrint(F("  Antenna power: "));
        DebugPrintln(hw.aPower == 1 ? F("ON") : (hw.aPower == 0 ? F("OFF") : F("UNKNOWN")));
        DebugPrint(F("  Noise/ms: "));
        DebugPrintln(hw.noisePerMS);
        DebugPrint(F("  AGC count: "));
        DebugPrintln(hw.agcCnt);
        DebugPrint(F("  Jamming: "));
        DebugPrint(hw.jamInd);
        DebugPrintln(F("/255"));
    } else {
        DebugPrintln(F("  MON-HW: query failed"));
    }

    // GLONASS enabled check (BBR config test)
    bool glonassOn = gpsPtr->isGNSSenabled(SFE_UBLOX_GNSS_ID_GLONASS);
    DebugPrint(F("  GLONASS enabled: "));
    DebugPrintln(glonassOn ? F("YES (BBR config intact)") : F("NO (BBR config lost)"));

    // Current fix info
    DebugPrint(F("  Satellites: "));
    DebugPrintln(currentGPSData.satellites);
    DebugPrint(F("  HDOP: "));
    DebugPrintln(currentGPSData.hdop, 1);
    DebugPrint(F("  Config saved to BBR: "));
    DebugPrintln(configSavedToBBR ? F("YES") : F("NO"));

    DebugPrintln(F("======================="));
}
