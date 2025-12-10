#include "modules/gps_manager.h"
#include "config.h"
#include <Wire.h>

static SFE_UBLOX_GNSS* gpsPtr = nullptr;
static GPSData currentGPSData;
static unsigned long lastFixAttempt = 0;

bool GPSManager_init(SFE_UBLOX_GNSS* gps) {
    gpsPtr = gps;

    // Enable GNSS power
    pinMode(GNSS_EN, OUTPUT);
    digitalWrite(GNSS_EN, LOW);  // Active low to enable
    delay(1000);

    // Connect to GPS on I2C
    if (!gpsPtr->begin(Wire)) {
        Serial.println(F("GPS: u-blox GNSS not detected at default I2C address"));
        return false;
    }

    // Configure GPS
    gpsPtr->setI2COutput(COM_TYPE_UBX);  // UBX protocol
    gpsPtr->setNavigationFrequency(GPS_UPDATE_RATE_HZ);
    gpsPtr->setDynamicModel(DYN_MODEL_PORTABLE);

    // Enable automatic NAV PVT messages
    gpsPtr->setAutoPVT(true);

    // Disable unnecessary NMEA sentences to reduce I2C traffic
    gpsPtr->newCfgValset();
    gpsPtr->addCfgValset(UBLOX_CFG_MSGOUT_NMEA_ID_GLL_I2C, 0);
    gpsPtr->addCfgValset(UBLOX_CFG_MSGOUT_NMEA_ID_GSA_I2C, 0);
    gpsPtr->addCfgValset(UBLOX_CFG_MSGOUT_NMEA_ID_GSV_I2C, 0);
    gpsPtr->addCfgValset(UBLOX_CFG_MSGOUT_NMEA_ID_VTG_I2C, 0);
    gpsPtr->sendCfgValset();

    Serial.println(F("GPS: Initialized successfully"));

    // Initialize GPS data structure
    currentGPSData.valid = false;
    currentGPSData.satellites = 0;

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

            // Print fix info periodically
            static unsigned long lastPrint = 0;
            if (currentMillis - lastPrint > 10000) {  // Every 10 seconds
                Serial.print(F("GPS: Fix - Lat: "));
                Serial.print(currentGPSData.latitude, 6);
                Serial.print(F(" Lon: "));
                Serial.print(currentGPSData.longitude, 6);
                Serial.print(F(" Sats: "));
                Serial.println(currentGPSData.satellites);
                lastPrint = currentMillis;
            }
        } else {
            currentGPSData.valid = false;

            // Print search status periodically
            static unsigned long lastSearchPrint = 0;
            if (currentMillis - lastSearchPrint > 30000) {  // Every 30 seconds
                Serial.print(F("GPS: Searching... Sats: "));
                Serial.print(currentGPSData.satellites);
                Serial.print(F(" Fix: "));
                Serial.println(currentGPSData.fixType);
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

    snprintf(buffer, bufferSize,
             "%.6f,%.6f,%.1f,%.1f,%d",
             currentGPSData.latitude,
             currentGPSData.longitude,
             currentGPSData.altitude,
             currentGPSData.speed,
             currentGPSData.satellites);
}

void GPSManager_sleep() {
    if (gpsPtr != nullptr) {
        gpsPtr->powerOff(0);  // Sleep indefinitely
    }
    digitalWrite(GNSS_EN, HIGH);  // Disable power (active low)
}

void GPSManager_wake() {
    digitalWrite(GNSS_EN, LOW);  // Enable power (active low)
    delay(500);
    if (gpsPtr != nullptr) {
        gpsPtr->begin(Wire);
    }
}
