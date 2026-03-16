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
    Serial.println(F("GPS: I2C bus recovery..."));
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
        Serial.println(F("GPS: ZOE-M8Q not detected on I2C"));
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
            Serial.println(F("GPS: First boot — configuring GPS+GLONASS..."));
            gpsPtr->setI2COutput(COM_TYPE_UBX);
            gpsPtr->setNavigationFrequency(GPS_UPDATE_RATE_HZ);
            gpsPtr->setDynamicModel(DYN_MODEL_PORTABLE);
            gpsPtr->setI2CpollingWait(25);
            gpsPtr->enableGNSS(true, SFE_UBLOX_GNSS_ID_GPS);
            gpsPtr->enableGNSS(true, SFE_UBLOX_GNSS_ID_GLONASS);

            if (gpsPtr->saveConfiguration()) {
                configSavedToBBR = true;
                Serial.println(F("GPS: Config saved to BBR (warm starts enabled)"));
            } else {
                Serial.println(F("GPS: WARNING - failed to save config to BBR"));
            }
        } else {
            configSavedToBBR = true;
            Serial.println(F("GPS: BBR config valid, skipping reconfig (ephemeris preserved)"));
        }
    } else {
        Serial.println(F("GPS: Light reinit (BBR config retained)"));
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
    Serial.println(F("GPS: Backup battery charging enabled"));

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

    Serial.println(F("GPS: ZOE-M8Q initialized (GPS+GLONASS, config saved to BBR)"));
    return true;
}

bool GPSManager_reinit() {
    if (gpsPtr == nullptr) return false;
    Serial.println(F("GPS: Re-initializing after power cycle (warm start)..."));

    am_hal_gpio_pincfg_t pinCfg = g_AM_HAL_GPIO_OUTPUT;
    pinCfg.eGPOutcfg = AM_HAL_GPIO_PIN_OUTCFG_OPENDRAIN;
    pin_config(PinName(GNSS_EN), pinCfg);
    delay(1);
    digitalWrite(GNSS_EN, LOW);
    delay(1000);  // Shorter delay — GPS doesn't need full cold-start ramp

    // BBR retains config from initial setup; skip full reconfiguration
    bool useLightInit = configSavedToBBR;
    if (!initI2CAndGPS(!useLightInit)) {
        Serial.println(F("GPS: Re-init FAILED, retrying with full config..."));
        delay(1000);
        if (!initI2CAndGPS(true)) {
            Serial.println(F("GPS: Re-init FAILED"));
            return false;
        }
    }

    Serial.println(F("GPS: Re-init OK"));
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

            if (!ttffLogged && powerOnTime > 0) {
                unsigned long ttff = currentMillis - powerOnTime;
                Serial.print(F("GPS: TTFF = "));
                Serial.print(ttff / 1000);
                Serial.print(F("s ("));
                if (ttff < 5000)       Serial.print(F("hot start"));
                else if (ttff < 35000) Serial.print(F("warm start"));
                else                   Serial.print(F("cold start"));
                Serial.println(F(")"));
                ttffLogged = true;
            }

            static unsigned long lastPrint = 0;
            if (currentMillis - lastPrint > 10000) {
                Serial.print(F("GPS: Fix - Lat: "));
                Serial.print(currentGPSData.latitude, 6);
                Serial.print(F(" Lon: "));
                Serial.print(currentGPSData.longitude, 6);
                Serial.print(F(" Sats: "));
                Serial.print(currentGPSData.satellites);
                Serial.print(F(" HDOP: "));
                Serial.println(currentGPSData.hdop, 1);
                lastPrint = currentMillis;
            }
        } else {
            currentGPSData.valid = false;

            static unsigned long lastSearchPrint = 0;
            if (currentMillis - lastSearchPrint > 15000) {
                Serial.print(F("GPS: Searching... Sats: "));
                Serial.print(currentGPSData.satellites);
                Serial.print(F(" Fix: "));
                Serial.print(currentGPSData.fixType);
                unsigned long elapsed = (powerOnTime > 0) ? (currentMillis - powerOnTime) / 1000 : 0;
                Serial.print(F(" Elapsed: "));
                Serial.print(elapsed);
                Serial.println(F("s"));
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
    Serial.println(F("GPS: Sleep (BBR retained via backup battery)"));
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
            Serial.println(F("GPS: Wake failed, retrying..."));
            delay(500);
            gpsPtr->begin(agtWire);
        }
        gpsPtr->setAutoPVT(true);

        powerOnTime = millis();
        ttffLogged = false;
        Serial.println(F("GPS: Wake (expecting hot/warm start from BBR)"));
    }
}
