#ifndef APOLLO_RTC_TIME_H
#define APOLLO_RTC_TIME_H

#include <stdint.h>

// Arguments in the order Apollo3RTC::setTime expects them.
//
// That API does not accept a normal calendar tuple. Its signature is:
//   hundredths, seconds, minutes, hours, day, month, year-since-2000
//
// Passing hour, minute, second, 0, ..., full-year shipped in the AGT and
// transformed 18:06:45 in 2026 into 00:45:06 in 2074. The resulting
// SYSTEM_TIME moved the vehicle clock, split logs, and defeated post-dive
// file matching. Keep this awkward conversion in one tested place.
struct ApolloRtcTime {
    uint8_t hundredths;
    uint8_t seconds;
    uint8_t minutes;
    uint8_t hours;
    uint8_t day;
    uint8_t month;
    uint16_t yearSince2000;
};

static inline bool apolloRtcTimeFromCalendar(
    uint16_t year, uint8_t month, uint8_t day,
    uint8_t hour, uint8_t minute, uint8_t second,
    ApolloRtcTime* out) {
    if (out == 0) return false;
    if (year < 2000 || year > 2099) return false;
    if (month < 1 || month > 12 || day < 1 || day > 31) return false;
    if (hour > 23 || minute > 59 || second > 59) return false;

    out->hundredths = 0;
    out->seconds = second;
    out->minutes = minute;
    out->hours = hour;
    out->day = day;
    out->month = month;
    out->yearSince2000 = year - 2000;
    return true;
}

#endif // APOLLO_RTC_TIME_H
