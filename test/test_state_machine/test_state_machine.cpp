#include <unity.h>
#include <limits.h>
#include "Arduino.h"

// Use stub relay controller instead of real hardware
#include "modules/relay_controller.h"

#include "../../src/modules/mission_data.cpp"
#include "../../src/modules/state_machine.cpp"

// ---------------------------------------------------------------------------
// Initialization
// ---------------------------------------------------------------------------

void test_init_starts_in_pre_dive(void) {
    StateMachine_init();
    TEST_ASSERT_EQUAL(STATE_PRE_DIVE, StateMachine_getState());
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

void test_pre_dive_to_diving(void) {
    StateMachine_init();
    StateMachine_enterDiving();
    TEST_ASSERT_EQUAL(STATE_DIVING, StateMachine_getState());
}

void test_diving_to_recovery(void) {
    StateMachine_init();
    StateMachine_enterDiving();
    StateMachine_enterRecovery();
    TEST_ASSERT_EQUAL(STATE_RECOVERY, StateMachine_getState());
}

void test_full_happy_path(void) {
    StateMachine_init();
    TEST_ASSERT_EQUAL(STATE_PRE_DIVE, StateMachine_getState());

    StateMachine_enterDiving();
    TEST_ASSERT_EQUAL(STATE_DIVING, StateMachine_getState());

    StateMachine_enterRecovery();
    TEST_ASSERT_EQUAL(STATE_RECOVERY, StateMachine_getState());
}

// ---------------------------------------------------------------------------
// Invalid transitions (guard conditions)
// ---------------------------------------------------------------------------

void test_cannot_enter_diving_from_recovery(void) {
    StateMachine_init();
    StateMachine_enterDiving();
    StateMachine_enterRecovery();

    StateMachine_enterDiving();  // should be rejected
    TEST_ASSERT_EQUAL(STATE_RECOVERY, StateMachine_getState());
}

void test_cannot_enter_diving_from_diving(void) {
    StateMachine_init();
    StateMachine_enterDiving();

    // enterDiving again should be no-op (enterState rejects same-state)
    StateMachine_enterDiving();
    TEST_ASSERT_EQUAL(STATE_DIVING, StateMachine_getState());
}

// ---------------------------------------------------------------------------
// Reset
// ---------------------------------------------------------------------------

void test_reset_returns_to_pre_dive(void) {
    StateMachine_init();
    StateMachine_enterDiving();

    StateMachine_reset();
    TEST_ASSERT_EQUAL(STATE_PRE_DIVE, StateMachine_getState());
}

void test_reset_restores_nonessentials(void) {
    StateMachine_init();
    StateMachine_enterDiving();
    StateMachine_enterRecovery();
    RelayController_setPowerManagement(false);

    StateMachine_reset();
    StateMachineStatus s = StateMachine_getStatus();
    TEST_ASSERT_TRUE(s.nonessentialsPowered);
    TEST_ASSERT_TRUE(RelayController_getPowerManagement());
}

void test_reset_clears_failsafe_but_preserves_release_latch(void) {
    StateMachine_init();
    StateMachine_enterDiving();
    StateMachine_triggerFailsafe(FAILSAFE_LOW_VOLTAGE);

    StateMachine_reset();
    StateMachineStatus s = StateMachine_getStatus();
    TEST_ASSERT_EQUAL(FAILSAFE_NONE, s.lastFailsafeSource);
    TEST_ASSERT_TRUE(s.releaseTriggered);
}

// ---------------------------------------------------------------------------
// Failsafe
// ---------------------------------------------------------------------------

void test_failsafe_enters_recovery(void) {
    StateMachine_init();
    StateMachine_enterDiving();

    StateMachine_triggerFailsafe(FAILSAFE_LOW_VOLTAGE);
    TEST_ASSERT_EQUAL(STATE_RECOVERY, StateMachine_getState());
}

void test_failsafe_sets_source(void) {
    StateMachine_init();
    StateMachine_enterDiving();

    StateMachine_triggerFailsafe(FAILSAFE_LEAK);
    StateMachineStatus s = StateMachine_getStatus();
    TEST_ASSERT_EQUAL(FAILSAFE_LEAK, s.lastFailsafeSource);
}

void test_failsafe_triggers_release_relay(void) {
    StateMachine_init();
    stub_relay_reset();
    StateMachine_enterDiving();

    StateMachine_triggerFailsafe(FAILSAFE_NO_HEARTBEAT);

    StateMachineStatus s = StateMachine_getStatus();
    TEST_ASSERT_TRUE(s.releaseTriggered);
    TEST_ASSERT_TRUE(_stub_timed_event_active);
}

void test_failsafe_does_not_double_trigger_relay(void) {
    StateMachine_init();
    stub_relay_reset();
    StateMachine_enterDiving();

    StateMachine_triggerFailsafe(FAILSAFE_LOW_VOLTAGE);
    int count_after_first = _stub_timed_event_trigger_count;

    StateMachine_triggerFailsafe(FAILSAFE_LEAK);
    TEST_ASSERT_EQUAL(count_after_first, _stub_timed_event_trigger_count);
}

void test_failsafe_all_sources(void) {
    FailsafeSource sources[] = {
        FAILSAFE_LOW_VOLTAGE,
        FAILSAFE_LEAK,
        FAILSAFE_NO_HEARTBEAT,
        FAILSAFE_MANUAL
    };

    for (int i = 0; i < 4; i++) {
        StateMachine_init();
        StateMachine_enterDiving();
        StateMachine_triggerFailsafe(sources[i]);

        TEST_ASSERT_EQUAL(STATE_RECOVERY, StateMachine_getState());
        StateMachineStatus s = StateMachine_getStatus();
        TEST_ASSERT_EQUAL(sources[i], s.lastFailsafeSource);
    }
}

// ---------------------------------------------------------------------------
// Recovery state behavior
// ---------------------------------------------------------------------------

void test_recovery_keeps_nonessentials_powered_until_handshake(void) {
    StateMachine_init();
    StateMachine_enterDiving();
    StateMachine_enterRecovery();

    StateMachineStatus s = StateMachine_getStatus();
    TEST_ASSERT_TRUE(s.nonessentialsPowered);
    TEST_ASSERT_TRUE(RelayController_getPowerManagement());
}

void test_recovery_enables_strobe(void) {
    StateMachine_init();
    TEST_ASSERT_FALSE(StateMachine_isRecoveryStrobe());

    StateMachine_enterDiving();
    TEST_ASSERT_FALSE(StateMachine_isRecoveryStrobe());

    StateMachine_enterRecovery();
    TEST_ASSERT_TRUE(StateMachine_isRecoveryStrobe());
}

void test_diving_keeps_nonessentials_powered(void) {
    StateMachine_init();
    StateMachine_enterDiving();

    StateMachineStatus s = StateMachine_getStatus();
    TEST_ASSERT_TRUE(s.nonessentialsPowered);
    TEST_ASSERT_TRUE(RelayController_getPowerManagement());
}

// ---------------------------------------------------------------------------
// Iridium transmission rules
// ---------------------------------------------------------------------------

void test_iridium_allowed_in_recovery_only(void) {
    StateMachine_init();
    TEST_ASSERT_FALSE(StateMachine_canTransmitIridium());

    StateMachine_enterDiving();
    TEST_ASSERT_FALSE(StateMachine_canTransmitIridium());

    StateMachine_enterRecovery();
    TEST_ASSERT_TRUE(StateMachine_canTransmitIridium());
}

// ---------------------------------------------------------------------------
// Previous state tracking
// ---------------------------------------------------------------------------

void test_previous_state_tracked(void) {
    StateMachine_init();
    StateMachine_enterDiving();

    StateMachineStatus s = StateMachine_getStatus();
    TEST_ASSERT_EQUAL(STATE_PRE_DIVE, s.previousState);
    TEST_ASSERT_EQUAL(STATE_DIVING, s.currentState);
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

void test_should_shutdown_nonessentials_only_after_qualification_and_ack(void) {
    StateMachine_init();
    TEST_ASSERT_FALSE(StateMachine_shouldShutdownNonessentials());

    StateMachine_enterDiving();
    TEST_ASSERT_FALSE(StateMachine_shouldShutdownNonessentials());

    StateMachine_enterRecovery();
    TEST_ASSERT_FALSE(StateMachine_shouldShutdownNonessentials());
}

void test_surface_power_requires_sustained_fresh_independent_data_and_ack(void) {
    stub_set_millis(100);
    StateMachine_init();
    StateMachine_enterDiving();
    MissionData_update_depth(3.0f);
    StateMachine_enterRecovery();

    for (int i = 0; i < 34; i++) {
        MissionData_update_doris_state(4);
        MissionData_update_depth(0.5f);
        StateMachine_updateSurfacePower(true);
        stub_advance_millis(1000);
    }

    TEST_ASSERT_TRUE(StateMachine_isShutdownRequested());
    TEST_ASSERT_TRUE(RelayController_getPowerManagement());

    TEST_ASSERT_TRUE(StateMachine_acknowledgeShutdown());
    for (int i = 0; i < 6; i++) {
        MissionData_update_doris_state(4);
        MissionData_update_depth(0.5f);
        StateMachine_updateSurfacePower(true);
        stub_advance_millis(1000);
    }
    StateMachine_updateSurfacePower(true);
    TEST_ASSERT_FALSE(RelayController_getPowerManagement());
    TEST_ASSERT_TRUE(StateMachine_shouldShutdownNonessentials());
}

void test_stale_surface_data_cancels_shutdown_request(void) {
    stub_set_millis(100);
    StateMachine_init();
    StateMachine_enterDiving();
    MissionData_update_depth(3.0f);
    StateMachine_enterRecovery();
    for (int i = 0; i < 34; i++) {
        MissionData_update_doris_state(4);
        MissionData_update_depth(0.5f);
        StateMachine_updateSurfacePower(true);
        stub_advance_millis(1000);
    }
    TEST_ASSERT_TRUE(StateMachine_isShutdownRequested());
    stub_advance_millis(MISSION_DATA_FRESHNESS_MS + 1);
    StateMachine_updateSurfacePower(true);
    TEST_ASSERT_FALSE(StateMachine_isShutdownRequested());
    TEST_ASSERT_TRUE(RelayController_getPowerManagement());
}

void test_premature_shutdown_ack_is_rejected(void) {
    StateMachine_init();
    TEST_ASSERT_FALSE(StateMachine_acknowledgeShutdown());
    StateMachineStatus status = StateMachine_getStatus();
    TEST_ASSERT_FALSE(status.shutdownAcknowledged);
    TEST_ASSERT_TRUE(RelayController_getPowerManagement());
}

void test_stale_qualification_after_ack_cannot_cut_power(void) {
    stub_set_millis(100);
    StateMachine_init();
    StateMachine_enterDiving();
    MissionData_update_depth(3.0f);
    StateMachine_enterRecovery();
    for (int i = 0; i < 34; i++) {
        MissionData_update_doris_state(4);
        MissionData_update_depth(0.5f);
        StateMachine_updateSurfacePower(true);
        stub_advance_millis(1000);
    }

    TEST_ASSERT_TRUE(StateMachine_acknowledgeShutdown());
    stub_advance_millis(MISSION_DATA_FRESHNESS_MS + 1);
    StateMachine_updateSurfacePower(true);
    stub_advance_millis(POWER_SHUTDOWN_FINAL_GRACE_MS + 1);
    StateMachine_updateSurfacePower(true);

    StateMachineStatus status = StateMachine_getStatus();
    TEST_ASSERT_FALSE(status.shutdownAcknowledged);
    TEST_ASSERT_FALSE(StateMachine_isShutdownRequested());
    TEST_ASSERT_TRUE(RelayController_getPowerManagement());
}

void test_transient_qualification_after_ack_requires_new_ack(void) {
    stub_set_millis(100);
    StateMachine_init();
    StateMachine_enterDiving();
    MissionData_update_depth(3.0f);
    StateMachine_enterRecovery();
    for (int i = 0; i < 34; i++) {
        MissionData_update_doris_state(4);
        MissionData_update_depth(0.5f);
        StateMachine_updateSurfacePower(true);
        stub_advance_millis(1000);
    }

    TEST_ASSERT_TRUE(StateMachine_acknowledgeShutdown());
    StateMachine_updateSurfacePower(false);
    stub_advance_millis(POWER_SHUTDOWN_FINAL_GRACE_MS + 1);
    MissionData_update_doris_state(4);
    MissionData_update_depth(0.5f);
    StateMachine_updateSurfacePower(true);

    StateMachineStatus status = StateMachine_getStatus();
    TEST_ASSERT_FALSE(status.shutdownAcknowledged);
    TEST_ASSERT_FALSE(StateMachine_isShutdownRequested());
    TEST_ASSERT_TRUE(RelayController_getPowerManagement());
}

void test_repeated_ack_does_not_restart_final_grace(void) {
    stub_set_millis(100);
    StateMachine_init();
    StateMachine_enterDiving();
    MissionData_update_depth(3.0f);
    StateMachine_enterRecovery();
    for (int i = 0; i < 34; i++) {
        MissionData_update_doris_state(4);
        MissionData_update_depth(0.5f);
        StateMachine_updateSurfacePower(true);
        stub_advance_millis(1000);
    }

    TEST_ASSERT_TRUE(StateMachine_acknowledgeShutdown());
    uint32_t halfGraceSeconds = POWER_SHUTDOWN_FINAL_GRACE_MS / 2000UL;
    for (uint32_t i = 0; i < halfGraceSeconds; i++) {
        stub_advance_millis(1000);
        MissionData_update_doris_state(4);
        MissionData_update_depth(0.5f);
        StateMachine_updateSurfacePower(true);
    }
    TEST_ASSERT_TRUE(StateMachine_acknowledgeShutdown());
    for (uint32_t i = 0; i <= halfGraceSeconds; i++) {
        stub_advance_millis(1000);
        MissionData_update_doris_state(4);
        MissionData_update_depth(0.5f);
        StateMachine_updateSurfacePower(true);
    }

    TEST_ASSERT_FALSE(RelayController_getPowerManagement());
}

void test_surface_qualification_handles_millis_rollover(void) {
    stub_set_millis(ULONG_MAX - 15000UL);
    StateMachine_init();
    StateMachine_enterDiving();
    MissionData_update_depth(3.0f);
    StateMachine_enterRecovery();
    for (int i = 0; i < 34; i++) {
        MissionData_update_doris_state(4);
        MissionData_update_depth(0.5f);
        StateMachine_updateSurfacePower(true);
        stub_advance_millis(1000);
    }

    TEST_ASSERT_TRUE(StateMachine_isShutdownRequested());
    TEST_ASSERT_TRUE(RelayController_getPowerManagement());
}

void test_boot_at_surface_cannot_request_shutdown(void) {
    stub_set_millis(100);
    StateMachine_init();
    StateMachine_enterRecovery();
    for (int i = 0; i < 34; i++) {
        MissionData_update_doris_state(4);
        MissionData_update_depth(0.5f);
        StateMachine_updateSurfacePower(true);
        stub_advance_millis(1000);
    }
    TEST_ASSERT_FALSE(StateMachine_isShutdownRequested());
    TEST_ASSERT_TRUE(RelayController_getPowerManagement());
}

// ---------------------------------------------------------------------------
// Unity entry point
// ---------------------------------------------------------------------------

void setUp(void) {
    stub_set_millis(0);
    stub_relay_reset();
    MissionData_init();
}

void tearDown(void) {}

int main(int argc, char** argv) {
    UNITY_BEGIN();

    // Init
    RUN_TEST(test_init_starts_in_pre_dive);
    RUN_TEST(test_init_clears_failsafe);
    RUN_TEST(test_init_powers_nonessentials);

    // Valid transitions
    RUN_TEST(test_pre_dive_to_diving);
    RUN_TEST(test_diving_to_recovery);
    RUN_TEST(test_full_happy_path);

    // Invalid transitions
    RUN_TEST(test_cannot_enter_diving_from_recovery);
    RUN_TEST(test_cannot_enter_diving_from_diving);

    // Reset
    RUN_TEST(test_reset_returns_to_pre_dive);
    RUN_TEST(test_reset_restores_nonessentials);
    RUN_TEST(test_reset_clears_failsafe_but_preserves_release_latch);

    // Failsafe
    RUN_TEST(test_failsafe_enters_recovery);
    RUN_TEST(test_failsafe_sets_source);
    RUN_TEST(test_failsafe_triggers_release_relay);
    RUN_TEST(test_failsafe_does_not_double_trigger_relay);
    RUN_TEST(test_failsafe_all_sources);

    // Recovery behavior
    RUN_TEST(test_recovery_keeps_nonessentials_powered_until_handshake);
    RUN_TEST(test_recovery_enables_strobe);
    RUN_TEST(test_diving_keeps_nonessentials_powered);

    // Transmission rules
    RUN_TEST(test_iridium_allowed_in_recovery_only);

    // State tracking
    RUN_TEST(test_previous_state_tracked);
    RUN_TEST(test_time_in_state_advances);
    RUN_TEST(test_should_shutdown_nonessentials_only_after_qualification_and_ack);
    RUN_TEST(test_surface_power_requires_sustained_fresh_independent_data_and_ack);
    RUN_TEST(test_stale_surface_data_cancels_shutdown_request);
    RUN_TEST(test_premature_shutdown_ack_is_rejected);
    RUN_TEST(test_stale_qualification_after_ack_cannot_cut_power);
    RUN_TEST(test_transient_qualification_after_ack_requires_new_ack);
    RUN_TEST(test_repeated_ack_does_not_restart_final_grace);
    RUN_TEST(test_surface_qualification_handles_millis_rollover);
    RUN_TEST(test_boot_at_surface_cannot_request_shutdown);

    return UNITY_END();
}
