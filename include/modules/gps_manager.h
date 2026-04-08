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
    uint32_t h_acc_mm;      // Horizontal accuracy estimate (mm) from u-blox hAcc
    uint32_t v_acc_mm;      // Vertical accuracy estimate (mm) from u-blox vAcc
    uint32_t s_acc_mm;      // Speed accuracy estimate (mm/s) from u-blox sAcc
    int32_t alt_ellipsoid;  // Height above ellipsoid (mm) from u-blox height
    int32_t vel_n_mm;       // North velocity (mm/s) from u-blox velN
    int32_t vel_e_mm;       // East velocity (mm/s) from u-blox velE
    int32_t vel_d_mm;       // Down velocity (mm/s) from u-blox velD
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

// Re-initialize GPS after power cycle (e.g. after Iridium send)
bool GPSManager_reinit();

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

// Diagnostics: print BBR/backup battery status to Serial
void GPSManager_printDiagnostics();

#endif // GPS_MANAGER_H
