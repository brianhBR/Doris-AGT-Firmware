#include "APOLLOrtc.h"

// Prefer Apollo3 HAL if available
#if defined(__has_include)
#  if __has_include(<am_hal_rtc.h>)
#    include <am_hal_rtc.h>
#    define APOLLO_HAS_HAL 1
#  endif
#endif

APOLLOrtc::APOLLOrtc() : epochOffset(0), setMillis(0) {}

int64_t APOLLOrtc::dateToEpoch(int year, int month, int day, int hour, int minute, int second) {
    if (month <= 2) {
        year -= 1;
        month += 12;
    }
    int64_t era = year / 400;
    int64_t yoe = year - era * 400;                        // [0, 399]
    int64_t doy = (153 * (month - 3) + 2) / 5 + day - 1;   // [0, 365]
    int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;  // [0, 146096]
    int64_t days = era * 146097 + doe - 719468; // days since 1970-01-01
    int64_t seconds = days * 86400 + hour * 3600 + minute * 60 + second;
    return seconds;
}

void APOLLOrtc::setTime(uint8_t hour, uint8_t minute, uint8_t second, uint8_t /*unused*/, uint8_t day, uint8_t month, uint16_t year) {
#if defined(APOLLO_HAS_HAL)
    am_hal_rtc_time_t rtc_time;
    rtc_time.ui32Year = year;
    rtc_time.ui32Month = month;
    rtc_time.ui32DayOfMonth = day;
    rtc_time.ui32Hour = hour;
    rtc_time.ui32Minute = minute;
    rtc_time.ui32Second = second;
    // Use HAL to set RTC; ignore return status for now
    am_hal_rtc_time_set(&rtc_time);
#else
    int y = (int)year;
    int m = (int)month;
    int d = (int)day;
    int h = (int)hour;
    int min = (int)minute;
    int s = (int)second;
    int64_t epoch = dateToEpoch(y, m, d, h, min, s);
    if (epoch < 0) epoch = 0;
    epochOffset = (uint32_t)epoch;
    setMillis = millis();
#endif
}

uint32_t APOLLOrtc::getEpoch() {
#if defined(APOLLO_HAS_HAL)
    am_hal_rtc_time_t rtc_time;
    am_hal_rtc_time_get(&rtc_time);
    int y = rtc_time.ui32Year;
    int m = rtc_time.ui32Month;
    int d = rtc_time.ui32DayOfMonth;
    int h = rtc_time.ui32Hour;
    int min = rtc_time.ui32Minute;
    int s = rtc_time.ui32Second;
    int64_t epoch = dateToEpoch(y, m, d, h, min, s);
    if (epoch < 0) epoch = 0;
    return (uint32_t)epoch;
#else
    unsigned long nowMs = millis();
    unsigned long diffMs = 0;
    if (nowMs >= setMillis) diffMs = nowMs - setMillis;
    else diffMs = (ULONG_MAX - setMillis) + nowMs; // handle millis wrap
    uint32_t added = (uint32_t)(diffMs / 1000UL);
    return epochOffset + added;
#endif
}
