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

static void surfaceTick(void) {
    MissionData_update_doris_state(4);
    StateMachine_updateSurfacePower();
}

static void enterDiveRecovery(void) {
    StateMachine_enterDiving();
    StateMachine_enterRecovery();
}

static void confirmLuaRecovery(void) {
    for (int i = 0; i < SURFACE_RECOVERY_MESSAGES; i++) {
        surfaceTick();
        if (i + 1 < SURFACE_RECOVERY_MESSAGES) {
            stub_advance_millis(100);
        }
    }
}

static void advanceWithFreshSurfaceReports(uint32_t durationMs) {
    uint32_t elapsed = 0;
    while (elapsed < durationMs) {
        uint32_t step = durationMs - elapsed;
        if (step > 1000UL) {
            step = 1000UL;
        }
        stub_advance_millis(step);
        surfaceTick();
        elapsed += step;
    }
}

static void finishSurfaceDwell(void) {
    advanceWithFreshSurfaceReports(SURFACE_LOGGING_DWELL_MS);
}

void test_one_recovery_report_cannot_start_surface_dwell(void) {
    stub_set_millis(100);
    StateMachine_init();
    enterDiveRecovery();
    surfaceTick();

    StateMachineStatus s = StateMachine_getStatus();
    TEST_ASSERT_FALSE(s.surfaceQualified);
    TEST_ASSERT_FALSE(StateMachine_isShutdownRequested());
    TEST_ASSERT_TRUE(RelayController_getPowerManagement());
}

void test_repeated_fresh_recovery_starts_surface_dwell(void) {
    stub_set_millis(100);
    StateMachine_init();
    enterDiveRecovery();
    confirmLuaRecovery();

    StateMachineStatus s = StateMachine_getStatus();
    TEST_ASSERT_TRUE(s.surfaceQualified);
    TEST_ASSERT_FALSE(StateMachine_isShutdownRequested());
    TEST_ASSERT_FALSE(StateMachine_acknowledgeShutdown());
}

void test_surface_power_enforces_logging_dwell_then_ack_grace(void) {
    stub_set_millis(100);
    StateMachine_init();
    enterDiveRecovery();
    confirmLuaRecovery();

    advanceWithFreshSurfaceReports(SURFACE_LOGGING_DWELL_MS - 1);
    TEST_ASSERT_FALSE(StateMachine_isShutdownRequested());

    advanceWithFreshSurfaceReports(1);
    TEST_ASSERT_TRUE(StateMachine_isShutdownRequested());
    TEST_ASSERT_TRUE(RelayController_getPowerManagement());

    TEST_ASSERT_TRUE(StateMachine_acknowledgeShutdown());
    stub_advance_millis(POWER_SHUTDOWN_FINAL_GRACE_MS - 1);
    StateMachine_updateSurfacePower();
    TEST_ASSERT_TRUE(RelayController_getPowerManagement());
    stub_advance_millis(1);
    StateMachine_updateSurfacePower();
    TEST_ASSERT_FALSE(RelayController_getPowerManagement());
    TEST_ASSERT_TRUE(StateMachine_shouldShutdownNonessentials());
}

void test_depth_never_authorizes_payload_shutdown(void) {
    stub_set_millis(100);
    StateMachine_init();
    StateMachine_enterDiving();
    MissionData_update_depth(30.0f);
    for (uint32_t i = 0; i <= SURFACE_QUALIFY_MS / 1000UL; i++) {
        MissionData_update_doris_state(3);
        MissionData_update_depth((i % 2 == 0) ? 0.40f : 0.43f);
        StateMachine_updateSurfaceBackstop();
        stub_advance_millis(1000);
    }
    TEST_ASSERT_EQUAL(STATE_RECOVERY, StateMachine_getState());

    stub_advance_millis(SURFACE_LOGGING_DWELL_MS);
    StateMachine_updateSurfacePower();
    TEST_ASSERT_FALSE(StateMachine_isShutdownRequested());
    TEST_ASSERT_FALSE(StateMachine_acknowledgeShutdown());
    TEST_ASSERT_TRUE(RelayController_getPowerManagement());
}

void test_stale_surface_state_cancels_before_ack(void) {
    stub_set_millis(100);
    StateMachine_init();
    enterDiveRecovery();
    confirmLuaRecovery();
    finishSurfaceDwell();
    TEST_ASSERT_TRUE(StateMachine_isShutdownRequested());

    stub_advance_millis(MISSION_DATA_FRESHNESS_MS + 1);
    StateMachine_updateSurfacePower();
    TEST_ASSERT_FALSE(StateMachine_isShutdownRequested());
    TEST_ASSERT_FALSE(StateMachine_acknowledgeShutdown());
    TEST_ASSERT_TRUE(RelayController_getPowerManagement());
}

void test_reverted_surface_state_cancels_before_ack(void) {
    stub_set_millis(100);
    StateMachine_init();
    enterDiveRecovery();
    confirmLuaRecovery();
    finishSurfaceDwell();
    TEST_ASSERT_TRUE(StateMachine_isShutdownRequested());

    MissionData_update_doris_state(3);
    StateMachine_updateSurfacePower();
    TEST_ASSERT_FALSE(StateMachine_isShutdownRequested());
    TEST_ASSERT_FALSE(StateMachine_acknowledgeShutdown());
    TEST_ASSERT_TRUE(RelayController_getPowerManagement());
}

void test_ack_latches_cutoff_through_transport_loss(void) {
    stub_set_millis(100);
    StateMachine_init();
    enterDiveRecovery();
    confirmLuaRecovery();
    finishSurfaceDwell();

    TEST_ASSERT_TRUE(StateMachine_acknowledgeShutdown());
    MissionData_update_doris_state(3);
    stub_advance_millis(POWER_SHUTDOWN_FINAL_GRACE_MS);
    StateMachine_updateSurfacePower();
    TEST_ASSERT_FALSE(RelayController_getPowerManagement());
    StateMachineStatus s = StateMachine_getStatus();
    TEST_ASSERT_TRUE(s.shutdownRequested);
    TEST_ASSERT_TRUE(s.shutdownAcknowledged);
}

void test_repeated_ack_does_not_restart_final_grace(void) {
    stub_set_millis(100);
    StateMachine_init();
    enterDiveRecovery();
    confirmLuaRecovery();
    finishSurfaceDwell();

    TEST_ASSERT_TRUE(StateMachine_acknowledgeShutdown());
    stub_advance_millis(POWER_SHUTDOWN_FINAL_GRACE_MS / 2);
    TEST_ASSERT_TRUE(StateMachine_acknowledgeShutdown());
    stub_advance_millis(POWER_SHUTDOWN_FINAL_GRACE_MS / 2);
    StateMachine_updateSurfacePower();
    TEST_ASSERT_FALSE(RelayController_getPowerManagement());
}

void test_surface_dwell_handles_millis_rollover(void) {
    stub_set_millis(ULONG_MAX - (SURFACE_LOGGING_DWELL_MS / 2));
    StateMachine_init();
    enterDiveRecovery();
    confirmLuaRecovery();
    finishSurfaceDwell();

    TEST_ASSERT_TRUE(StateMachine_isShutdownRequested());
    TEST_ASSERT_TRUE(RelayController_getPowerManagement());
}

void test_boot_at_surface_cannot_request_shutdown(void) {
    stub_set_millis(100);
    StateMachine_init();
    StateMachine_enterRecovery();
    confirmLuaRecovery();
    finishSurfaceDwell();
    TEST_ASSERT_FALSE(StateMachine_isShutdownRequested());
    TEST_ASSERT_TRUE(RelayController_getPowerManagement());
}

// ---------------------------------------------------------------------------
// Power cycle behavior
// ---------------------------------------------------------------------------

// Drive a complete dive, surface qualification, ACK, and final grace so the
// power relay is left cut.
static void qualifyAndCutPower(void) {
    enterDiveRecovery();
    confirmLuaRecovery();
    finishSurfaceDwell();
    TEST_ASSERT_TRUE(StateMachine_acknowledgeShutdown());
    stub_advance_millis(POWER_SHUTDOWN_FINAL_GRACE_MS);
    StateMachine_updateSurfacePower();
    TEST_ASSERT_FALSE(RelayController_getPowerManagement());
}

void test_power_cycle_restores_pi_power(void) {
    stub_set_millis(100);
    StateMachine_init();
    qualifyAndCutPower();

    // Power cycle: nothing about the cutoff is persisted.
    RelayController_init();
    MissionData_init();
    StateMachine_init();

    TEST_ASSERT_TRUE(RelayController_getPowerManagement());
    TEST_ASSERT_FALSE(StateMachine_shouldShutdownNonessentials());
    TEST_ASSERT_EQUAL(STATE_PRE_DIVE, StateMachine_getState());
    StateMachineStatus s = StateMachine_getStatus();
    TEST_ASSERT_TRUE(s.nonessentialsPowered);
    TEST_ASSERT_FALSE(s.surfaceQualified);
    TEST_ASSERT_FALSE(s.shutdownRequested);
    TEST_ASSERT_FALSE(s.shutdownAcknowledged);
}

void test_rebooted_autopilot_cannot_cut_power(void) {
    // After a power cycle Lua restarts in CONFIG and reports STATE=-1, so the
    // AGT must leave the Pi powered on deck.
    stub_set_millis(100);
    StateMachine_init();
    for (int i = 0; i < 120; i++) {
        MissionData_update_doris_state(-1);
        MissionData_update_depth((i % 2 == 0) ? 0.20f : 0.23f);
        StateMachine_updateSurfacePower();
        stub_advance_millis(1000);
    }
    TEST_ASSERT_FALSE(StateMachine_isShutdownRequested());
    TEST_ASSERT_TRUE(RelayController_getPowerManagement());
    TEST_ASSERT_EQUAL(STATE_PRE_DIVE, StateMachine_getState());
}

// A new AGT boot cannot treat replayed STATE=4 packets as a completed mission:
// the RAM-only PRE_DIVE -> DIVING sequence must be observed first.
void test_replayed_recovery_after_reboot_cannot_cut_power(void) {
    stub_set_millis(100);
    StateMachine_init();
    StateMachine_enterRecovery();
    confirmLuaRecovery();
    finishSurfaceDwell();
    TEST_ASSERT_FALSE(StateMachine_isShutdownRequested());
    TEST_ASSERT_TRUE(RelayController_getPowerManagement());
}

// ---------------------------------------------------------------------------
// Independent surface backstop
// ---------------------------------------------------------------------------

// Lua is wedged in ASCENT waiting on a fix that is not coming. This is the
// AGT's own way out, and gating it on a fix made it useless in exactly the
// conditions it exists for.
static void backstopTick(int i, float depth) {
    MissionData_update_doris_state(3);
    MissionData_update_depth(depth);
    StateMachine_updateSurfaceBackstop();
}

static float livingSurfaceDepth(int i) {
    return (i % 2 == 0) ? 0.40f : 0.43f;
}

void test_backstop_enters_recovery_on_depth_alone(void) {
    stub_set_millis(100);
    StateMachine_init();
    StateMachine_enterDiving();
    MissionData_update_depth(30.0f);

    for (int i = 0; i < 31; i++) {
        backstopTick(i, livingSurfaceDepth(i));
        stub_advance_millis(1000);
    }

    TEST_ASSERT_EQUAL(STATE_RECOVERY, StateMachine_getState());
}

void test_backstop_needs_the_full_window(void) {
    stub_set_millis(100);
    StateMachine_init();
    StateMachine_enterDiving();
    MissionData_update_depth(30.0f);

    for (int i = 0; i < 25; i++) {
        backstopTick(i, livingSurfaceDepth(i));
        stub_advance_millis(1000);
    }

    TEST_ASSERT_EQUAL(STATE_DIVING, StateMachine_getState());
}

void test_backstop_refuses_a_frozen_depth_channel(void) {
    stub_set_millis(100);
    StateMachine_init();
    StateMachine_enterDiving();
    MissionData_update_depth(30.0f);

    for (int i = 0; i < 120; i++) {
        backstopTick(i, 0.40f);
        stub_advance_millis(1000);
    }

    TEST_ASSERT_EQUAL(STATE_DIVING, StateMachine_getState());
}

void test_backstop_restarts_when_the_vehicle_goes_under_again(void) {
    stub_set_millis(100);
    StateMachine_init();
    StateMachine_enterDiving();
    MissionData_update_depth(30.0f);

    for (int i = 0; i < 25; i++) {
        backstopTick(i, livingSurfaceDepth(i));
        stub_advance_millis(1000);
    }
    // A swell pushes it back under, so the window starts over.
    backstopTick(0, 4.0f);
    stub_advance_millis(1000);
    for (int i = 0; i < 25; i++) {
        backstopTick(i, livingSurfaceDepth(i));
        stub_advance_millis(1000);
    }

    TEST_ASSERT_EQUAL(STATE_DIVING, StateMachine_getState());
}

// A vehicle sitting shallow at the start of a descent must never be mistaken
// for one that has surfaced.
void test_backstop_does_not_fire_before_ascent(void) {
    stub_set_millis(100);
    StateMachine_init();
    StateMachine_enterDiving();

    for (int i = 0; i < 120; i++) {
        MissionData_update_doris_state(1);
        MissionData_update_depth(livingSurfaceDepth(i));
        StateMachine_updateSurfaceBackstop();
        stub_advance_millis(1000);
    }

    TEST_ASSERT_EQUAL(STATE_DIVING, StateMachine_getState());
}

void test_backstop_ignores_stale_depth(void) {
    stub_set_millis(100);
    StateMachine_init();
    StateMachine_enterDiving();
    MissionData_update_depth(30.0f);
    backstopTick(0, 0.40f);

    // Depth reports stop arriving; the window must not keep maturing on the
    // strength of one old reading.
    stub_advance_millis(MISSION_DATA_FRESHNESS_MS + 1);
    for (int i = 0; i < 60; i++) {
        StateMachine_updateSurfaceBackstop();
        stub_advance_millis(1000);
    }

    TEST_ASSERT_EQUAL(STATE_DIVING, StateMachine_getState());
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
    RUN_TEST(test_one_recovery_report_cannot_start_surface_dwell);
    RUN_TEST(test_repeated_fresh_recovery_starts_surface_dwell);
    RUN_TEST(test_surface_power_enforces_logging_dwell_then_ack_grace);
    RUN_TEST(test_depth_never_authorizes_payload_shutdown);
    RUN_TEST(test_stale_surface_state_cancels_before_ack);
    RUN_TEST(test_reverted_surface_state_cancels_before_ack);
    RUN_TEST(test_ack_latches_cutoff_through_transport_loss);
    RUN_TEST(test_repeated_ack_does_not_restart_final_grace);
    RUN_TEST(test_surface_dwell_handles_millis_rollover);
    RUN_TEST(test_boot_at_surface_cannot_request_shutdown);

    // Power cycle behavior
    RUN_TEST(test_power_cycle_restores_pi_power);
    RUN_TEST(test_rebooted_autopilot_cannot_cut_power);
    RUN_TEST(test_replayed_recovery_after_reboot_cannot_cut_power);

    // Independent surface backstop
    RUN_TEST(test_backstop_enters_recovery_on_depth_alone);
    RUN_TEST(test_backstop_needs_the_full_window);
    RUN_TEST(test_backstop_refuses_a_frozen_depth_channel);
    RUN_TEST(test_backstop_restarts_when_the_vehicle_goes_under_again);
    RUN_TEST(test_backstop_does_not_fire_before_ascent);
    RUN_TEST(test_backstop_ignores_stale_depth);

    return UNITY_END();
}
