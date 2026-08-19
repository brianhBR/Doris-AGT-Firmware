#include <unity.h>
#include <limits.h>
#include "Arduino.h"

#include "../../src/modules/iridium_schedule.cpp"

#define REPORT_INTERVAL_MS DEFAULT_IRIDIUM_INTERVAL

static IridiumSchedule sched;

// ---------------------------------------------------------------------------
// Unlocated reports
// ---------------------------------------------------------------------------

void test_first_unlocated_report_is_due_immediately(void) {
    IridiumSchedule_reset(&sched);
    TEST_ASSERT_TRUE(
        IridiumSchedule_unlocatedDue(&sched, 0UL, REPORT_INTERVAL_MS));
}

void test_unlocated_report_then_waits_the_configured_interval(void) {
    IridiumSchedule_reset(&sched);
    IridiumSchedule_noteSent(&sched, 1000UL, false);

    TEST_ASSERT_FALSE(IridiumSchedule_unlocatedDue(
        &sched, 1000UL + REPORT_INTERVAL_MS - 1, REPORT_INTERVAL_MS));
    TEST_ASSERT_TRUE(IridiumSchedule_unlocatedDue(
        &sched, 1000UL + REPORT_INTERVAL_MS, REPORT_INTERVAL_MS));
}

// ---------------------------------------------------------------------------
// Located reports
// ---------------------------------------------------------------------------

void test_located_report_is_due_immediately_on_entering_recovery(void) {
    IridiumSchedule_reset(&sched);
    TEST_ASSERT_TRUE(IridiumSchedule_locatedDue(&sched, 0UL,
                                                REPORT_INTERVAL_MS));
}

void test_located_report_then_waits_the_interval(void) {
    IridiumSchedule_reset(&sched);
    IridiumSchedule_noteSent(&sched, 1000UL, true);

    TEST_ASSERT_FALSE(IridiumSchedule_locatedDue(
        &sched, 1000UL + REPORT_INTERVAL_MS - 1, REPORT_INTERVAL_MS));
    TEST_ASSERT_TRUE(IridiumSchedule_locatedDue(
        &sched, 1000UL + REPORT_INTERVAL_MS, REPORT_INTERVAL_MS));
}

// The whole point of the report is the position, and so far the operator has
// only been told the vehicle is up.
void test_a_fix_upgrades_an_unlocated_report_at_once(void) {
    IridiumSchedule_reset(&sched);
    IridiumSchedule_noteSent(&sched, 120000UL, false);

    TEST_ASSERT_TRUE(IridiumSchedule_locatedDue(&sched, 120001UL,
                                                REPORT_INTERVAL_MS));
}

void test_the_upgrade_happens_only_once(void) {
    IridiumSchedule_reset(&sched);
    IridiumSchedule_noteSent(&sched, 120000UL, false);
    IridiumSchedule_noteSent(&sched, 121000UL, true);

    TEST_ASSERT_FALSE(IridiumSchedule_locatedDue(&sched, 122000UL,
                                                 REPORT_INTERVAL_MS));
}

// After a located report, losing the fix must not immediately produce an
// unlocated one; the operator has just heard from the vehicle.
void test_losing_the_fix_after_a_report_does_not_retransmit(void) {
    IridiumSchedule_reset(&sched);
    IridiumSchedule_noteSent(&sched, 120000UL, true);

    TEST_ASSERT_FALSE(IridiumSchedule_unlocatedDue(
        &sched, 130000UL, REPORT_INTERVAL_MS));
    TEST_ASSERT_TRUE(IridiumSchedule_unlocatedDue(
        &sched, 120000UL + REPORT_INTERVAL_MS, REPORT_INTERVAL_MS));
}

// ---------------------------------------------------------------------------
// Rollover
// ---------------------------------------------------------------------------

void test_scheduling_survives_millis_rollover(void) {
    IridiumSchedule_reset(&sched);
    unsigned long before = ULONG_MAX - 1000UL;
    IridiumSchedule_noteSent(&sched, before, false);

    unsigned long after = before + REPORT_INTERVAL_MS;  // wraps
    TEST_ASSERT_TRUE(IridiumSchedule_unlocatedDue(
        &sched, after, REPORT_INTERVAL_MS));
    TEST_ASSERT_FALSE(IridiumSchedule_unlocatedDue(
        &sched, after - 1, REPORT_INTERVAL_MS));
}

// ---------------------------------------------------------------------------
// Unity entry point
// ---------------------------------------------------------------------------

void setUp(void) { IridiumSchedule_reset(&sched); }

void tearDown(void) {}

int main(int argc, char** argv) {
    UNITY_BEGIN();

    RUN_TEST(test_first_unlocated_report_is_due_immediately);
    RUN_TEST(test_unlocated_report_then_waits_the_configured_interval);

    RUN_TEST(test_located_report_is_due_immediately_on_entering_recovery);
    RUN_TEST(test_located_report_then_waits_the_interval);
    RUN_TEST(test_a_fix_upgrades_an_unlocated_report_at_once);
    RUN_TEST(test_the_upgrade_happens_only_once);
    RUN_TEST(test_losing_the_fix_after_a_report_does_not_retransmit);

    RUN_TEST(test_scheduling_survives_millis_rollover);

    return UNITY_END();
}
