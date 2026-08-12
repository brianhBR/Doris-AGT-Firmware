#include <unity.h>
#include <limits.h>
#include "Arduino.h"

#include "../../src/modules/iridium_schedule.cpp"

#define LOCATED_INTERVAL_MS IRIDIUM_SEND_INTERVAL_MS
#define FIRST_SECONDS       (IRIDIUM_NOFIX_FIRST_MS / 1000UL)

static IridiumSchedule sched;

// ---------------------------------------------------------------------------
// Unlocated reports
// ---------------------------------------------------------------------------

void test_nothing_is_due_before_the_first_interval(void) {
    IridiumSchedule_reset(&sched);
    TEST_ASSERT_FALSE(
        IridiumSchedule_unlocatedDue(&sched, 60000UL, FIRST_SECONDS - 1));
}

void test_first_unlocated_report_at_two_minutes(void) {
    IridiumSchedule_reset(&sched);
    TEST_ASSERT_TRUE(
        IridiumSchedule_unlocatedDue(&sched, 120000UL, FIRST_SECONDS));
}

void test_unlocated_reports_repeat_every_thirty_minutes(void) {
    IridiumSchedule_reset(&sched);
    IridiumSchedule_noteSent(&sched, 120000UL, false);

    // Iridium and GPS share an antenna, so a chatty schedule here would keep
    // interrupting the acquisition we are waiting on.
    TEST_ASSERT_FALSE(IridiumSchedule_unlocatedDue(
        &sched, 120000UL + IRIDIUM_NOFIX_REPEAT_MS - 1, 600));
    TEST_ASSERT_TRUE(IridiumSchedule_unlocatedDue(
        &sched, 120000UL + IRIDIUM_NOFIX_REPEAT_MS, 600));
}

// A failsafe release enters RECOVERY without going through the normal
// surfacing path, and must report on the same schedule.
void test_first_report_is_timed_from_recovery_not_from_boot(void) {
    IridiumSchedule_reset(&sched);
    TEST_ASSERT_FALSE(
        IridiumSchedule_unlocatedDue(&sched, 9999999UL, FIRST_SECONDS - 1));
    TEST_ASSERT_TRUE(
        IridiumSchedule_unlocatedDue(&sched, 9999999UL, FIRST_SECONDS));
}

// ---------------------------------------------------------------------------
// Located reports
// ---------------------------------------------------------------------------

void test_located_report_is_due_immediately_on_entering_recovery(void) {
    IridiumSchedule_reset(&sched);
    TEST_ASSERT_TRUE(IridiumSchedule_locatedDue(&sched, 0UL,
                                                LOCATED_INTERVAL_MS));
}

void test_located_report_then_waits_the_interval(void) {
    IridiumSchedule_reset(&sched);
    IridiumSchedule_noteSent(&sched, 1000UL, true);

    TEST_ASSERT_FALSE(IridiumSchedule_locatedDue(
        &sched, 1000UL + LOCATED_INTERVAL_MS - 1, LOCATED_INTERVAL_MS));
    TEST_ASSERT_TRUE(IridiumSchedule_locatedDue(
        &sched, 1000UL + LOCATED_INTERVAL_MS, LOCATED_INTERVAL_MS));
}

// The whole point of the report is the position, and so far the operator has
// only been told the vehicle is up.
void test_a_fix_upgrades_an_unlocated_report_at_once(void) {
    IridiumSchedule_reset(&sched);
    IridiumSchedule_noteSent(&sched, 120000UL, false);

    TEST_ASSERT_TRUE(IridiumSchedule_locatedDue(&sched, 120001UL,
                                                LOCATED_INTERVAL_MS));
}

void test_the_upgrade_happens_only_once(void) {
    IridiumSchedule_reset(&sched);
    IridiumSchedule_noteSent(&sched, 120000UL, false);
    IridiumSchedule_noteSent(&sched, 121000UL, true);

    TEST_ASSERT_FALSE(IridiumSchedule_locatedDue(&sched, 122000UL,
                                                 LOCATED_INTERVAL_MS));
}

// After a located report, losing the fix must not immediately produce an
// unlocated one; the operator has just heard from the vehicle.
void test_losing_the_fix_after_a_report_does_not_retransmit(void) {
    IridiumSchedule_reset(&sched);
    IridiumSchedule_noteSent(&sched, 120000UL, true);

    TEST_ASSERT_FALSE(IridiumSchedule_unlocatedDue(&sched, 130000UL, 600));
    TEST_ASSERT_TRUE(IridiumSchedule_unlocatedDue(
        &sched, 120000UL + IRIDIUM_NOFIX_REPEAT_MS, 600));
}

// ---------------------------------------------------------------------------
// Rollover
// ---------------------------------------------------------------------------

void test_scheduling_survives_millis_rollover(void) {
    IridiumSchedule_reset(&sched);
    unsigned long before = ULONG_MAX - 1000UL;
    IridiumSchedule_noteSent(&sched, before, false);

    unsigned long after = before + IRIDIUM_NOFIX_REPEAT_MS;  // wraps
    TEST_ASSERT_TRUE(IridiumSchedule_unlocatedDue(&sched, after, 600));
    TEST_ASSERT_FALSE(IridiumSchedule_unlocatedDue(&sched, after - 1, 600));
}

// ---------------------------------------------------------------------------
// Unity entry point
// ---------------------------------------------------------------------------

void setUp(void) { IridiumSchedule_reset(&sched); }

void tearDown(void) {}

int main(int argc, char** argv) {
    UNITY_BEGIN();

    RUN_TEST(test_nothing_is_due_before_the_first_interval);
    RUN_TEST(test_first_unlocated_report_at_two_minutes);
    RUN_TEST(test_unlocated_reports_repeat_every_thirty_minutes);
    RUN_TEST(test_first_report_is_timed_from_recovery_not_from_boot);

    RUN_TEST(test_located_report_is_due_immediately_on_entering_recovery);
    RUN_TEST(test_located_report_then_waits_the_interval);
    RUN_TEST(test_a_fix_upgrades_an_unlocated_report_at_once);
    RUN_TEST(test_the_upgrade_happens_only_once);
    RUN_TEST(test_losing_the_fix_after_a_report_does_not_retransmit);

    RUN_TEST(test_scheduling_survives_millis_rollover);

    return UNITY_END();
}
