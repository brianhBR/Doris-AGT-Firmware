#include <unity.h>
#include "Arduino.h"

// Use stub relay controller instead of real hardware
#include "modules/relay_controller.h"

#include "../../src/modules/state_machine.cpp"

// ---------------------------------------------------------------------------
// Initialization
// ---------------------------------------------------------------------------

void test_init_starts_in_pre_mission(void) {
    StateMachine_init();
    TEST_ASSERT_EQUAL(STATE_PRE_MISSION, StateMachine_getState());
}

void test_init_clears_failsafe(void) {
    StateMachine_init();
    StateMachineStatus s = StateMachine_getStatus();
    TEST_ASSERT_EQUAL(FAILSAFE_NONE, s.lastFailsafeSource);
    TEST_ASSERT_FALSE(s.releaseTriggered);
}

void test_init_powers_nonessentials(void) {
    StateMachine_init();
    StateMachineStatus s = StateMachine_getStatus();
    TEST_ASSERT_TRUE(s.nonessentialsPowered);
    TEST_ASSERT_TRUE(RelayController_getPowerManagement());
}

// ---------------------------------------------------------------------------
// Valid transitions
// ---------------------------------------------------------------------------

void test_pre_mission_to_self_test(void) {
    StateMachine_init();
    StateMachine_startSelfTest();
    TEST_ASSERT_EQUAL(STATE_SELF_TEST, StateMachine_getState());
}

void test_self_test_to_mission(void) {
    StateMachine_init();
    StateMachine_startSelfTest();
    StateMachine_enterMission();
    TEST_ASSERT_EQUAL(STATE_MISSION, StateMachine_getState());
}

void test_mission_to_recovery(void) {
    StateMachine_init();
    StateMachine_startSelfTest();
    StateMachine_enterMission();
    StateMachine_enterRecovery();
    TEST_ASSERT_EQUAL(STATE_RECOVERY, StateMachine_getState());
}

void test_full_happy_path(void) {
    StateMachine_init();
    TEST_ASSERT_EQUAL(STATE_PRE_MISSION, StateMachine_getState());

    StateMachine_startSelfTest();
    TEST_ASSERT_EQUAL(STATE_SELF_TEST, StateMachine_getState());

    StateMachine_enterMission();
    TEST_ASSERT_EQUAL(STATE_MISSION, StateMachine_getState());

    StateMachine_enterRecovery();
    TEST_ASSERT_EQUAL(STATE_RECOVERY, StateMachine_getState());
}

// ---------------------------------------------------------------------------
// Invalid transitions (guard conditions)
// ---------------------------------------------------------------------------

void test_cannot_start_self_test_from_mission(void) {
    StateMachine_init();
    StateMachine_startSelfTest();
    StateMachine_enterMission();

    StateMachine_startSelfTest();  // should be rejected
    TEST_ASSERT_EQUAL(STATE_MISSION, StateMachine_getState());
}

void test_cannot_enter_mission_from_pre_mission(void) {
    StateMachine_init();
    StateMachine_enterMission();  // should be rejected
    TEST_ASSERT_EQUAL(STATE_PRE_MISSION, StateMachine_getState());
}

void test_cannot_enter_mission_from_recovery(void) {
    StateMachine_init();
    StateMachine_startSelfTest();
    StateMachine_enterMission();
    StateMachine_enterRecovery();

    StateMachine_enterMission();  // should be rejected
    TEST_ASSERT_EQUAL(STATE_RECOVERY, StateMachine_getState());
}

// ---------------------------------------------------------------------------
// Reset
// ---------------------------------------------------------------------------

void test_reset_returns_to_pre_mission(void) {
    StateMachine_init();
    StateMachine_startSelfTest();
    StateMachine_enterMission();

    StateMachine_reset();
    TEST_ASSERT_EQUAL(STATE_PRE_MISSION, StateMachine_getState());
}

void test_reset_restores_nonessentials(void) {
    StateMachine_init();
    StateMachine_startSelfTest();
    StateMachine_enterMission();
    StateMachine_enterRecovery();

    StateMachineStatus s = StateMachine_getStatus();
    TEST_ASSERT_FALSE(s.nonessentialsPowered);

    StateMachine_reset();
    s = StateMachine_getStatus();
    TEST_ASSERT_TRUE(s.nonessentialsPowered);
}

// ---------------------------------------------------------------------------
// Failsafe
// ---------------------------------------------------------------------------

void test_failsafe_enters_recovery(void) {
    StateMachine_init();
    StateMachine_startSelfTest();
    StateMachine_enterMission();

    StateMachine_triggerFailsafe(FAILSAFE_LOW_VOLTAGE);
    TEST_ASSERT_EQUAL(STATE_RECOVERY, StateMachine_getState());
}

void test_failsafe_sets_source(void) {
    StateMachine_init();
    StateMachine_startSelfTest();
    StateMachine_enterMission();

    StateMachine_triggerFailsafe(FAILSAFE_LEAK);
    StateMachineStatus s = StateMachine_getStatus();
    TEST_ASSERT_EQUAL(FAILSAFE_LEAK, s.lastFailsafeSource);
}

void test_failsafe_triggers_release_relay(void) {
    StateMachine_init();
    stub_relay_reset();
    StateMachine_startSelfTest();
    StateMachine_enterMission();

    StateMachine_triggerFailsafe(FAILSAFE_MAX_DEPTH);

    StateMachineStatus s = StateMachine_getStatus();
    TEST_ASSERT_TRUE(s.releaseTriggered);
    TEST_ASSERT_TRUE(_stub_timed_event_active);
    TEST_ASSERT_EQUAL(RELEASE_RELAY_DURATION_SEC, _stub_timed_event_duration);
}

void test_failsafe_does_not_double_trigger_relay(void) {
    StateMachine_init();
    stub_relay_reset();
    StateMachine_startSelfTest();
    StateMachine_enterMission();

    StateMachine_triggerFailsafe(FAILSAFE_LOW_VOLTAGE);
    int count_after_first = _stub_timed_event_trigger_count;

    // Second failsafe should not fire relay again
    StateMachine_triggerFailsafe(FAILSAFE_LEAK);
    TEST_ASSERT_EQUAL(count_after_first, _stub_timed_event_trigger_count);
}

void test_failsafe_all_sources(void) {
    FailsafeSource sources[] = {
        FAILSAFE_LOW_VOLTAGE,
        FAILSAFE_LEAK,
        FAILSAFE_MAX_DEPTH,
        FAILSAFE_NO_HEARTBEAT,
        FAILSAFE_MANUAL
    };

    for (int i = 0; i < 5; i++) {
        StateMachine_init();
        StateMachine_startSelfTest();
        StateMachine_enterMission();
        StateMachine_triggerFailsafe(sources[i]);

        TEST_ASSERT_EQUAL(STATE_RECOVERY, StateMachine_getState());
        StateMachineStatus s = StateMachine_getStatus();
        TEST_ASSERT_EQUAL(sources[i], s.lastFailsafeSource);
    }
}

// ---------------------------------------------------------------------------
// Recovery state behavior
// ---------------------------------------------------------------------------

void test_recovery_shuts_down_nonessentials(void) {
    StateMachine_init();
    StateMachine_startSelfTest();
    StateMachine_enterMission();
    StateMachine_enterRecovery();

    StateMachineStatus s = StateMachine_getStatus();
    TEST_ASSERT_FALSE(s.nonessentialsPowered);
    TEST_ASSERT_FALSE(RelayController_getPowerManagement());
}

void test_recovery_enables_strobe(void) {
    StateMachine_init();
    TEST_ASSERT_FALSE(StateMachine_isRecoveryStrobe());

    StateMachine_startSelfTest();
    TEST_ASSERT_FALSE(StateMachine_isRecoveryStrobe());

    StateMachine_enterMission();
    TEST_ASSERT_FALSE(StateMachine_isRecoveryStrobe());

    StateMachine_enterRecovery();
    TEST_ASSERT_TRUE(StateMachine_isRecoveryStrobe());
}

// ---------------------------------------------------------------------------
// Iridium / Meshtastic transmission rules
// ---------------------------------------------------------------------------

void test_iridium_allowed_in_self_test_and_recovery(void) {
    StateMachine_init();
    TEST_ASSERT_FALSE(StateMachine_canTransmitIridium());

    StateMachine_startSelfTest();
    TEST_ASSERT_TRUE(StateMachine_canTransmitIridium());

    StateMachine_enterMission();
    TEST_ASSERT_FALSE(StateMachine_canTransmitIridium());

    StateMachine_enterRecovery();
    TEST_ASSERT_TRUE(StateMachine_canTransmitIridium());
}

void test_meshtastic_always_allowed(void) {
    StateMachine_init();
    TEST_ASSERT_TRUE(StateMachine_canTransmitMeshtastic());

    StateMachine_startSelfTest();
    TEST_ASSERT_TRUE(StateMachine_canTransmitMeshtastic());

    StateMachine_enterMission();
    TEST_ASSERT_TRUE(StateMachine_canTransmitMeshtastic());

    StateMachine_enterRecovery();
    TEST_ASSERT_TRUE(StateMachine_canTransmitMeshtastic());
}

// ---------------------------------------------------------------------------
// Previous state tracking
// ---------------------------------------------------------------------------

void test_previous_state_tracked(void) {
    StateMachine_init();
    StateMachine_startSelfTest();

    StateMachineStatus s = StateMachine_getStatus();
    TEST_ASSERT_EQUAL(STATE_PRE_MISSION, s.previousState);
    TEST_ASSERT_EQUAL(STATE_SELF_TEST, s.currentState);
}

// ---------------------------------------------------------------------------
// Time in state
// ---------------------------------------------------------------------------

void test_time_in_state_advances(void) {
    stub_set_millis(1000);
    StateMachine_init();

    stub_set_millis(6000);
    StateMachine_update();

    TEST_ASSERT_EQUAL(5, StateMachine_getTimeInState());
}

// ---------------------------------------------------------------------------
// Nonessential shutdown query
// ---------------------------------------------------------------------------

void test_should_shutdown_nonessentials_only_in_recovery(void) {
    StateMachine_init();
    TEST_ASSERT_FALSE(StateMachine_shouldShutdownNonessentials());

    StateMachine_startSelfTest();
    TEST_ASSERT_FALSE(StateMachine_shouldShutdownNonessentials());

    StateMachine_enterMission();
    TEST_ASSERT_FALSE(StateMachine_shouldShutdownNonessentials());

    StateMachine_enterRecovery();
    TEST_ASSERT_TRUE(StateMachine_shouldShutdownNonessentials());
}

// ---------------------------------------------------------------------------
// Unity entry point
// ---------------------------------------------------------------------------

void setUp(void) {
    stub_set_millis(0);
    stub_relay_reset();
}

void tearDown(void) {}

int main(int argc, char** argv) {
    UNITY_BEGIN();

    // Init
    RUN_TEST(test_init_starts_in_pre_mission);
    RUN_TEST(test_init_clears_failsafe);
    RUN_TEST(test_init_powers_nonessentials);

    // Valid transitions
    RUN_TEST(test_pre_mission_to_self_test);
    RUN_TEST(test_self_test_to_mission);
    RUN_TEST(test_mission_to_recovery);
    RUN_TEST(test_full_happy_path);

    // Invalid transitions
    RUN_TEST(test_cannot_start_self_test_from_mission);
    RUN_TEST(test_cannot_enter_mission_from_pre_mission);
    RUN_TEST(test_cannot_enter_mission_from_recovery);

    // Reset
    RUN_TEST(test_reset_returns_to_pre_mission);
    RUN_TEST(test_reset_restores_nonessentials);

    // Failsafe
    RUN_TEST(test_failsafe_enters_recovery);
    RUN_TEST(test_failsafe_sets_source);
    RUN_TEST(test_failsafe_triggers_release_relay);
    RUN_TEST(test_failsafe_does_not_double_trigger_relay);
    RUN_TEST(test_failsafe_all_sources);

    // Recovery behavior
    RUN_TEST(test_recovery_shuts_down_nonessentials);
    RUN_TEST(test_recovery_enables_strobe);

    // Transmission rules
    RUN_TEST(test_iridium_allowed_in_self_test_and_recovery);
    RUN_TEST(test_meshtastic_always_allowed);

    // State tracking
    RUN_TEST(test_previous_state_tracked);
    RUN_TEST(test_time_in_state_advances);
    RUN_TEST(test_should_shutdown_nonessentials_only_in_recovery);

    return UNITY_END();
}
