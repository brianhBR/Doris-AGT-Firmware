#include "modules/gps_manager.h"
#include "config.h"
#include <Wire.h>

// AGT GPS uses dedicated I2C bus on pins 8 (SCL) and 9 (SDA)
const byte PIN_AGTWIRE_SCL = 8;
const byte PIN_AGTWIRE_SDA = 9;

// Helper function to get/init agtWire (lazy initialization to avoid early constructor)
static TwoWire& getAGTWire() {
    static TwoWire wire(PIN_AGTWIRE_SDA, PIN_AGTWIRE_SCL);
    return wire;
}

static SFE_UBLOX_GNSS* gpsPtr = nullptr;
static GPSData currentGPSData;
static unsigned long lastFixAttempt = 0;

bool GPSManager_init(SFE_UBLOX_GNSS* gps) {
    gpsPtr = gps;

    // Enable GNSS power - configure as open-drain output (per SparkFun AGT examples)
    am_hal_gpio_pincfg_t pinCfg = g_AM_HAL_GPIO_OUTPUT;
    pinCfg.eGPOutcfg = AM_HAL_GPIO_PIN_OUTCFG_OPENDRAIN;
    pin_config(PinName(GNSS_EN), pinCfg);
    delay(1);
    digitalWrite(GNSS_EN, LOW);  // Active low to enable
    delay(1000);

    // Get reference to agtWire (lazy init - constructor runs now, not at global scope)
    TwoWire& agtWire = getAGTWire();

    // Initialize custom I2C bus for GPS (pins 8 and 9)
    agtWire.begin();
    delay(10);
    agtWire.setClock(100000); // Use 100kHz for reliability (per SparkFun examples)

    // CRITICAL: Disable pull-ups AFTER agtWire.begin() (per SparkFun AGT examples)
    // The ZOE-M8Q has its own pull-ups
    // Order matters: must call pin_config AFTER agtWire.begin()
    am_hal_gpio_pincfg_t sclCfg = g_AM_BSP_GPIO_IOM1_SCL;
    sclCfg.ePullup = AM_HAL_GPIO_PIN_PULLUP_NONE;
    pin_config(PinName(PIN_AGTWIRE_SCL), sclCfg);

    am_hal_gpio_pincfg_t sdaCfg = g_AM_BSP_GPIO_IOM1_SDA;
    sdaCfg.ePullup = AM_HAL_GPIO_PIN_PULLUP_NONE;
    pin_config(PinName(PIN_AGTWIRE_SDA), sdaCfg);
    delay(10);

    // Connect to GPS on custom I2C bus
    delay(100);

    if (!gpsPtr->begin(agtWire)) {
        Serial.println(F("GPS: ZOE-M8Q not detected on I2C"));
        return false;
    }

    // Configure GPS
    gpsPtr->setI2COutput(COM_TYPE_UBX);  // UBX protocol
    gpsPtr->setNavigationFrequency(GPS_UPDATE_RATE_HZ);
    gpsPtr->setDynamicModel(DYN_MODEL_PORTABLE);

    // Enable automatic NAV PVT messages
    gpsPtr->setAutoPVT(true);

    // NMEA output already disabled by setI2COutput(COM_TYPE_UBX) above

    // Enable GNSS backup battery charging (per SparkFun AGT examples)
    pinMode(GNSS_BCKP_BAT_CHG_EN, OUTPUT);
    digitalWrite(GNSS_BCKP_BAT_CHG_EN, LOW);  // OUTPUT+LOW = charging enabled

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

    // Configure pin as open-drain and disable power (per SparkFun AGT examples)
    am_hal_gpio_pincfg_t pinCfg = g_AM_HAL_GPIO_OUTPUT;
    pinCfg.eGPOutcfg = AM_HAL_GPIO_PIN_OUTCFG_OPENDRAIN;
    pin_config(PinName(GNSS_EN), pinCfg);
    delay(1);
    digitalWrite(GNSS_EN, HIGH);  // Disable power (HIGH = disable)
}

void GPSManager_wake() {
    // Configure pin as open-drain and enable power (per SparkFun AGT examples)
    am_hal_gpio_pincfg_t pinCfg = g_AM_HAL_GPIO_OUTPUT;
    pinCfg.eGPOutcfg = AM_HAL_GPIO_PIN_OUTCFG_OPENDRAIN;
    pin_config(PinName(GNSS_EN), pinCfg);
    delay(1);
    digitalWrite(GNSS_EN, LOW);  // Enable power (LOW = enable)
    delay(500);
    if (gpsPtr != nullptr) {
        TwoWire& agtWire = getAGTWire();
        gpsPtr->begin(agtWire);  // Use custom I2C bus
    }
}
