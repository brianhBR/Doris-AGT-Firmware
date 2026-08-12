// Apollo3RTC::setTime uses a non-calendar argument order and stores a
// year-since-2000. The AGT once passed hour/minute/second first and the full
// year, turning 18:06:45 in 2026 into 00:45:06 in 2074 on the wire.

#include <unity.h>

#include "apollo_rtc_time.h"

void test_calendar_time_maps_to_apollo_argument_order(void) {
    ApolloRtcTime t;

    TEST_ASSERT_TRUE(apolloRtcTimeFromCalendar(
        2026, 8, 12, 18, 6, 45, &t));
    TEST_ASSERT_EQUAL_UINT8(0, t.hundredths);
    TEST_ASSERT_EQUAL_UINT8(45, t.seconds);
    TEST_ASSERT_EQUAL_UINT8(6, t.minutes);
    TEST_ASSERT_EQUAL_UINT8(18, t.hours);
    TEST_ASSERT_EQUAL_UINT8(12, t.day);
    TEST_ASSERT_EQUAL_UINT8(8, t.month);
    TEST_ASSERT_EQUAL_UINT16(26, t.yearSince2000);
}

void test_last_supported_year_maps_to_two_digits(void) {
    ApolloRtcTime t;

    TEST_ASSERT_TRUE(apolloRtcTimeFromCalendar(
        2099, 12, 31, 23, 59, 59, &t));
    TEST_ASSERT_EQUAL_UINT16(99, t.yearSince2000);
}

void test_full_year_outside_rtc_century_is_rejected(void) {
    ApolloRtcTime t;

    TEST_ASSERT_FALSE(apolloRtcTimeFromCalendar(
        1999, 12, 31, 23, 59, 59, &t));
    TEST_ASSERT_FALSE(apolloRtcTimeFromCalendar(
        2100, 1, 1, 0, 0, 0, &t));
}

void test_invalid_calendar_fields_are_rejected(void) {
    ApolloRtcTime t;

    TEST_ASSERT_FALSE(apolloRtcTimeFromCalendar(
        2026, 0, 12, 18, 6, 45, &t));
    TEST_ASSERT_FALSE(apolloRtcTimeFromCalendar(
        2026, 8, 0, 18, 6, 45, &t));
    TEST_ASSERT_FALSE(apolloRtcTimeFromCalendar(
        2026, 8, 12, 24, 6, 45, &t));
    TEST_ASSERT_FALSE(apolloRtcTimeFromCalendar(
        2026, 8, 12, 18, 60, 45, &t));
    TEST_ASSERT_FALSE(apolloRtcTimeFromCalendar(
        2026, 8, 12, 18, 6, 60, &t));
}

void test_null_output_is_rejected(void) {
    TEST_ASSERT_FALSE(apolloRtcTimeFromCalendar(
        2026, 8, 12, 18, 6, 45, 0));
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_calendar_time_maps_to_apollo_argument_order);
    RUN_TEST(test_last_supported_year_maps_to_two_digits);
    RUN_TEST(test_full_year_outside_rtc_century_is_rejected);
    RUN_TEST(test_invalid_calendar_fields_are_rejected);
    RUN_TEST(test_null_output_is_rejected);
    return UNITY_END();
}

void setUp(void) {}
void tearDown(void) {}
