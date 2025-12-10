#ifndef GPS_MANAGER_H
#define GPS_MANAGER_H

#include <SparkFun_u-blox_GNSS_Arduino_Library.h>

// GPS data structure
struct GPSData {
    double latitude;
    double longitude;
    float altitude;
    float speed;
    float course;
    uint8_t satellites;
    uint8_t fixType;
    float hdop;
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    bool valid;
};

// Initialize GPS module
bool GPSManager_init(SFE_UBLOX_GNSS* gps);

// Update GPS data (call in loop)
void GPSManager_update();

// Check if GPS has valid fix
bool GPSManager_hasFix();

// Get current GPS data
GPSData GPSManager_getData();

// Get GPS data as formatted string
void GPSManager_getDataString(char* buffer, size_t bufferSize);

// Power management
void GPSManager_sleep();
void GPSManager_wake();

#endif // GPS_MANAGER_H
