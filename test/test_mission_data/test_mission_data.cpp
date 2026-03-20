#include <unity.h>
#include "Arduino.h"
#include "modules/mission_data.h"

// Pull in the real implementation
#include "../../src/modules/mission_data.cpp"

// ---------------------------------------------------------------------------
// Initialization
// ---------------------------------------------------------------------------

void test_init_zeroes_all_fields(void) {
    MissionData_init();
    MissionData md;
    MissionData_get(&md);

    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, md.depth_m);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, md.max_depth_m);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, md.battery_voltage);
    TEST_ASSERT_FALSE(md.depth_valid);
    TEST_ASSERT_FALSE(md.voltage_from_autopilot);
    TEST_ASSERT_FALSE(md.leak_detected);
    TEST_ASSERT_FALSE(md.heartbeat_valid);
    TEST_ASSERT_EQUAL(0, md.last_heartbeat_ms);
}

// ---------------------------------------------------------------------------
// Depth tracking
// ---------------------------------------------------------------------------

void test_depth_update_sets_value_and_valid(void) {
    MissionData_init();
    MissionData_update_depth(25.5f);

    MissionData md;
    MissionData_get(&md);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 25.5f, md.depth_m);
    TEST_ASSERT_TRUE(md.depth_valid);
}

void test_depth_negative_clamped_to_zero(void) {
    MissionData_init();
    MissionData_update_depth(-5.0f);

    MissionData md;
    MissionData_get(&md);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, md.depth_m);
    TEST_ASSERT_TRUE(md.depth_valid);
}

void test_max_depth_tracks_highest(void) {
    MissionData_init();
    MissionData_update_depth(10.0f);
    MissionData_update_depth(50.0f);
    MissionData_update_depth(30.0f);

    MissionData md;
    MissionData_get(&md);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 30.0f, md.depth_m);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 50.0f, md.max_depth_m);
}

void test_max_depth_not_affected_by_negative(void) {
    MissionData_init();
    MissionData_update_depth(20.0f);
    MissionData_update_depth(-10.0f);

    MissionData md;
    MissionData_get(&md);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 20.0f, md.max_depth_m);
}

// ---------------------------------------------------------------------------
// Voltage
// ---------------------------------------------------------------------------

void test_voltage_update_from_psm(void) {
    MissionData_init();
    MissionData_update_voltage(12.6f);

    MissionData md;
    MissionData_get(&md);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 12.6f, md.battery_voltage);
    TEST_ASSERT_FALSE(md.voltage_from_autopilot);
}

void test_autopilot_voltage_sets_flag(void) {
    MissionData_init();
    MissionData_update_autopilot_voltage(14.2f);

    MissionData md;
    MissionData_get(&md);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 14.2f, md.battery_voltage);
    TEST_ASSERT_TRUE(md.voltage_from_autopilot);
}

void test_autopilot_voltage_overrides_psm(void) {
    MissionData_init();
    MissionData_update_voltage(10.0f);
    MissionData_update_autopilot_voltage(14.0f);

    MissionData md;
    MissionData_get(&md);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 14.0f, md.battery_voltage);
    TEST_ASSERT_TRUE(md.voltage_from_autopilot);
}

// ---------------------------------------------------------------------------
// Heartbeat
// ---------------------------------------------------------------------------

void test_heartbeat_records_time_and_sets_valid(void) {
    MissionData_init();
    stub_set_millis(5000);
    MissionData_update_heartbeat();

    MissionData md;
    MissionData_get(&md);
    TEST_ASSERT_EQUAL(5000, md.last_heartbeat_ms);
    TEST_ASSERT_TRUE(md.heartbeat_valid);
}

void test_heartbeat_updates_on_subsequent_calls(void) {
    MissionData_init();
    stub_set_millis(1000);
    MissionData_update_heartbeat();
    stub_set_millis(2000);
    MissionData_update_heartbeat();

    MissionData md;
    MissionData_get(&md);
    TEST_ASSERT_EQUAL(2000, md.last_heartbeat_ms);
}

// ---------------------------------------------------------------------------
// Leak
// ---------------------------------------------------------------------------

void test_leak_set_and_clear(void) {
    MissionData_init();
    MissionData_set_leak(true);

    MissionData md;
    MissionData_get(&md);
    TEST_ASSERT_TRUE(md.leak_detected);

    MissionData_set_leak(false);
    MissionData_get(&md);
    TEST_ASSERT_FALSE(md.leak_detected);
}

// ---------------------------------------------------------------------------
// Null safety
// ---------------------------------------------------------------------------

void test_get_with_null_does_not_crash(void) {
    MissionData_init();
    MissionData_get(NULL);  // should not crash
    TEST_PASS();
}

// ---------------------------------------------------------------------------
// Unity entry point
// ---------------------------------------------------------------------------

void setUp(void) {
    stub_set_millis(0);
    MissionData_init();
}

void tearDown(void) {}

int main(int argc, char** argv) {
    UNITY_BEGIN();

    RUN_TEST(test_init_zeroes_all_fields);
    RUN_TEST(test_depth_update_sets_value_and_valid);
    RUN_TEST(test_depth_negative_clamped_to_zero);
    RUN_TEST(test_max_depth_tracks_highest);
    RUN_TEST(test_max_depth_not_affected_by_negative);
    RUN_TEST(test_voltage_update_from_psm);
    RUN_TEST(test_autopilot_voltage_sets_flag);
    RUN_TEST(test_autopilot_voltage_overrides_psm);
    RUN_TEST(test_heartbeat_records_time_and_sets_valid);
    RUN_TEST(test_heartbeat_updates_on_subsequent_calls);
    RUN_TEST(test_leak_set_and_clear);
    RUN_TEST(test_get_with_null_does_not_crash);

    return UNITY_END();
}
