#include <unity.h>

#include "rtc_time_guard.h"

static const RtcTimeGuardLimits limits = {
    2025,
    2027,
    300
};

void test_build_year_is_extracted_from_compiler_date() {
    TEST_ASSERT_EQUAL_UINT16(2026, rtcTimeBuildYear("Aug 15 2026"));
    TEST_ASSERT_EQUAL_UINT16(0, rtcTimeBuildYear("Aug 15 xxxx"));
    TEST_ASSERT_EQUAL_UINT16(0, rtcTimeBuildYear(0));
}

void test_calendar_conversion_handles_leap_days() {
    const RtcCalendarTime leapDay = {2028, 2, 29, 12, 34, 56};
    const RtcCalendarTime nextDay = {2028, 3, 1, 12, 34, 56};
    uint64_t leapDaySeconds = 0;
    uint64_t nextDaySeconds = 0;

    TEST_ASSERT_TRUE(rtcTimeToUnixSeconds(leapDay, &leapDaySeconds));
    TEST_ASSERT_TRUE(rtcTimeToUnixSeconds(nextDay, &nextDaySeconds));
    TEST_ASSERT_EQUAL_UINT64(86400, nextDaySeconds - leapDaySeconds);
}

void test_calendar_conversion_rejects_impossible_dates() {
    const RtcCalendarTime nonLeapDay = {2026, 2, 29, 0, 0, 0};
    const RtcCalendarTime april31 = {2026, 4, 31, 0, 0, 0};
    uint64_t unixSeconds = 0;

    TEST_ASSERT_FALSE(rtcTimeToUnixSeconds(nonLeapDay, &unixSeconds));
    TEST_ASSERT_FALSE(rtcTimeToUnixSeconds(april31, &unixSeconds));
}

void test_initial_sync_rejects_far_future_year() {
    const RtcCalendarTime candidate = {2074, 8, 5, 0, 45, 6};
    const RtcTimeGuardState unsynced = {false, 0};
    uint64_t unixSeconds = 0;

    TEST_ASSERT_EQUAL(
        RTC_TIME_REJECT_YEAR,
        rtcTimeValidate(candidate, unsynced, limits, &unixSeconds));
}

void test_initial_sync_accepts_plausible_year() {
    const RtcCalendarTime candidate = {2026, 8, 15, 15, 0, 0};
    const RtcTimeGuardState unsynced = {false, 0};
    uint64_t unixSeconds = 0;

    TEST_ASSERT_EQUAL(
        RTC_TIME_VALID,
        rtcTimeValidate(candidate, unsynced, limits, &unixSeconds));
    TEST_ASSERT_GREATER_THAN_UINT64(0, unixSeconds);
}

void test_synced_clock_accepts_small_gps_correction() {
    const RtcCalendarTime current = {2026, 8, 15, 15, 0, 0};
    const RtcCalendarTime candidate = {2026, 8, 15, 15, 4, 59};
    uint64_t currentUnixSeconds = 0;
    uint64_t candidateUnixSeconds = 0;
    TEST_ASSERT_TRUE(rtcTimeToUnixSeconds(current, &currentUnixSeconds));
    const RtcTimeGuardState synced = {true, currentUnixSeconds};

    TEST_ASSERT_EQUAL(
        RTC_TIME_VALID,
        rtcTimeValidate(candidate, synced, limits, &candidateUnixSeconds));
}

void test_synced_clock_can_cross_year_bound() {
    const RtcCalendarTime current = {2027, 12, 31, 23, 59, 59};
    const RtcCalendarTime candidate = {2028, 1, 1, 0, 0, 0};
    uint64_t currentUnixSeconds = 0;
    uint64_t candidateUnixSeconds = 0;
    TEST_ASSERT_TRUE(rtcTimeToUnixSeconds(current, &currentUnixSeconds));
    const RtcTimeGuardState synced = {true, currentUnixSeconds};

    TEST_ASSERT_EQUAL(
        RTC_TIME_VALID,
        rtcTimeValidate(candidate, synced, limits, &candidateUnixSeconds));
}

void test_synced_clock_rejects_large_forward_and_backward_steps() {
    const RtcCalendarTime current = {2026, 8, 15, 15, 0, 0};
    const RtcCalendarTime forward = {2026, 8, 15, 15, 5, 1};
    const RtcCalendarTime backward = {2026, 8, 15, 14, 54, 59};
    uint64_t currentUnixSeconds = 0;
    uint64_t candidateUnixSeconds = 0;
    TEST_ASSERT_TRUE(rtcTimeToUnixSeconds(current, &currentUnixSeconds));
    const RtcTimeGuardState synced = {true, currentUnixSeconds};

    TEST_ASSERT_EQUAL(
        RTC_TIME_REJECT_STEP,
        rtcTimeValidate(forward, synced, limits, &candidateUnixSeconds));
    TEST_ASSERT_EQUAL(
        RTC_TIME_REJECT_STEP,
        rtcTimeValidate(backward, synced, limits, &candidateUnixSeconds));
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_build_year_is_extracted_from_compiler_date);
    RUN_TEST(test_calendar_conversion_handles_leap_days);
    RUN_TEST(test_calendar_conversion_rejects_impossible_dates);
    RUN_TEST(test_initial_sync_rejects_far_future_year);
    RUN_TEST(test_initial_sync_accepts_plausible_year);
    RUN_TEST(test_synced_clock_accepts_small_gps_correction);
    RUN_TEST(test_synced_clock_can_cross_year_bound);
    RUN_TEST(test_synced_clock_rejects_large_forward_and_backward_steps);
    return UNITY_END();
}

void setUp() {}
void tearDown() {}
