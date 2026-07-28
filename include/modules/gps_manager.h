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
    bool valid;               // true: fixType>=2 AND sats>=GPS_MIN_SATS (usable position)
    // u-blox UBX-NAV-PVT "valid" flags (byte 11). These are asserted by the
    // module from its BBR-backed internal RTC as soon as date/time are
    // known, which can happen seconds before a position fix on warm start.
    // Use these (NOT `valid` above) to gate wall-clock operations like
    // seeding the MCU RTC.
    bool date_valid;          // bit 0: validDate
    bool time_valid;          // bit 1: validTime
    bool time_fully_resolved; // bit 2: fullyResolved (UTC leap-sec resolved)
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

// True once the GPS module has delivered at least one PVT solution
bool GPSManager_hasPVT();

// True iff the most recent UBX-NAV-PVT has both validDate and validTime
// flags set. This can be true BEFORE GPSManager_hasFix() on warm start
// because u-blox seeds date/time from its coin-cell-backed internal RTC
// before satellites are (re)acquired.
bool GPSManager_hasValidTime();

// Get GPS data as formatted string
void GPSManager_getDataString(char* buffer, size_t bufferSize);

// Power management
void GPSManager_sleep();
void GPSManager_wake();

// Diagnostics: print BBR/backup battery status to Serial
void GPSManager_printDiagnostics();

#endif // GPS_MANAGER_H
