#ifndef RTC_TIME_GUARD_H
#define RTC_TIME_GUARD_H

#include <stdint.h>

/**
 * Calendar fields supplied by a GNSS receiver.
 */
struct RtcCalendarTime {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
};

/**
 * Limits used to reject implausible GNSS clock updates.
 */
struct RtcTimeGuardLimits {
    uint16_t minimumYear;
    uint16_t maximumYear;
    uint32_t maximumStepSeconds;
};

/**
 * Current RTC state used when validating a GNSS clock update.
 */
struct RtcTimeGuardState {
    bool isSynced;
    uint64_t currentUnixSeconds;
};

/**
 * Result of validating a GNSS clock update.
 */
enum RtcTimeValidation {
    RTC_TIME_VALID,
    RTC_TIME_REJECT_INVALID_CALENDAR,
    RTC_TIME_REJECT_YEAR,
    RTC_TIME_REJECT_STEP
};

/**
 * Return whether a calendar year contains February 29.
 */
static inline bool rtcTimeIsLeapYear(uint16_t year) {
    return (year % 4U == 0U) && ((year % 100U != 0U) || (year % 400U == 0U));
}

/**
 * Return the number of days in a calendar month, or zero for an invalid month.
 */
static inline uint8_t rtcTimeDaysInMonth(uint16_t year, uint8_t month) {
    static const uint8_t daysByMonth[] = {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
    };
    if (month < 1U || month > 12U) {
        return 0U;
    }
    if (month == 2U && rtcTimeIsLeapYear(year)) {
        return 29U;
    }
    return daysByMonth[month - 1U];
}

/**
 * Convert a UTC calendar time to Unix seconds.
 */
static inline bool rtcTimeToUnixSeconds(
    const RtcCalendarTime& calendar,
    uint64_t* unixSeconds) {
    if (unixSeconds == 0 || calendar.year < 1970U) {
        return false;
    }

    const uint8_t daysInMonth = rtcTimeDaysInMonth(calendar.year, calendar.month);
    if (calendar.day < 1U || calendar.day > daysInMonth ||
        calendar.hour > 23U || calendar.minute > 59U || calendar.second > 59U) {
        return false;
    }

    uint32_t days = 0U;
    for (uint16_t year = 1970U; year < calendar.year; ++year) {
        days += rtcTimeIsLeapYear(year) ? 366U : 365U;
    }
    for (uint8_t month = 1U; month < calendar.month; ++month) {
        days += rtcTimeDaysInMonth(calendar.year, month);
    }
    days += calendar.day - 1U;

    *unixSeconds =
        ((static_cast<uint64_t>(days) * 24U + calendar.hour) * 60U +
         calendar.minute) *
            60U +
        calendar.second;
    return true;
}

/**
 * Extract the year from the compiler's "Mmm dd yyyy" __DATE__ string.
 */
static inline uint16_t rtcTimeBuildYear(const char* buildDate) {
    if (buildDate == 0) {
        return 0U;
    }
    for (uint8_t index = 7U; index <= 10U; ++index) {
        if (buildDate[index] < '0' || buildDate[index] > '9') {
            return 0U;
        }
    }
    return static_cast<uint16_t>(
        (buildDate[7] - '0') * 1000U +
        (buildDate[8] - '0') * 100U +
        (buildDate[9] - '0') * 10U +
        (buildDate[10] - '0'));
}

/**
 * Validate a GNSS time before it is allowed to discipline the hardware RTC.
 */
static inline RtcTimeValidation rtcTimeValidate(
    const RtcCalendarTime& candidate,
    const RtcTimeGuardState& state,
    const RtcTimeGuardLimits& limits,
    uint64_t* candidateUnixSeconds) {
    if (candidate.year < limits.minimumYear ||
        (!state.isSynced && candidate.year > limits.maximumYear)) {
        return RTC_TIME_REJECT_YEAR;
    }

    uint64_t convertedSeconds = 0U;
    if (!rtcTimeToUnixSeconds(candidate, &convertedSeconds)) {
        return RTC_TIME_REJECT_INVALID_CALENDAR;
    }
    if (candidateUnixSeconds != 0) {
        *candidateUnixSeconds = convertedSeconds;
    }

    if (!state.isSynced) {
        return RTC_TIME_VALID;
    }

    const uint64_t difference =
        convertedSeconds > state.currentUnixSeconds
            ? convertedSeconds - state.currentUnixSeconds
            : state.currentUnixSeconds - convertedSeconds;
    if (difference > limits.maximumStepSeconds) {
        return RTC_TIME_REJECT_STEP;
    }
    return RTC_TIME_VALID;
}

#endif // RTC_TIME_GUARD_H
