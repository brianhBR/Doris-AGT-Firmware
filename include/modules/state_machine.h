#ifndef STATE_MACHINE_H
#define STATE_MACHINE_H

#include <stdint.h>

// ============================================================================
// SIMPLIFIED STATE MACHINE - Drop Camera
// ============================================================================
// PRE_MISSION  -> SELF_TEST (run tests: GPS, Iridium, battery, mission loaded)
// SELF_TEST    -> MISSION   when MAVLink depth > 2m
// MISSION      -> RECOVERY  when depth < 3m OR GPS fix (surfaced)
// Failsafe in MISSION: low voltage, leak, max depth, no heartbeat -> release relay, RECOVERY

enum SystemState {
    STATE_PRE_MISSION,   // Initial setup
    STATE_SELF_TEST,     // GPS, Iridium, battery health, mission loaded to autopilot
    STATE_MISSION,       // Monitor MAVLink, check GPS; run failsafe checks
    STATE_RECOVERY       // Send position, strobe lights, relay off (low power)
};

// Failsafe trigger sources
enum FailsafeSource {
    FAILSAFE_NONE,
    FAILSAFE_LOW_VOLTAGE,
    FAILSAFE_LEAK,
    FAILSAFE_MAX_DEPTH,
    FAILSAFE_NO_HEARTBEAT,
    FAILSAFE_MANUAL
};

struct StateMachineStatus {
    SystemState currentState;
    SystemState previousState;
    unsigned long stateEntryTime;
    unsigned long timeInState;
    FailsafeSource lastFailsafeSource;
    bool releaseTriggered;       // Release relay has been fired
    bool nonessentialsPowered;   // Relay 1 (Navigator/Pi, camera, lights)
};

// Initialize / update
void StateMachine_init();
void StateMachine_update();

// Queries
SystemState StateMachine_getState();
StateMachineStatus StateMachine_getStatus();
uint32_t StateMachine_getTimeInState();

// Pre-mission -> Self Test (e.g. serial command "start_self_test")
void StateMachine_startSelfTest();

// Self Test -> Mission when MAVLink depth > 2m
void StateMachine_enterMission();

// Mission -> Recovery when depth < 3m or GPS fix
void StateMachine_enterRecovery();

// Reset to Pre-mission (for testing)
void StateMachine_reset();

// Failsafe: trigger release relay and enter recovery
void StateMachine_triggerFailsafe(FailsafeSource source);

// State queries
bool StateMachine_canTransmitIridium();
bool StateMachine_canTransmitMeshtastic();
bool StateMachine_shouldShutdownNonessentials();  // Relay off for low power
bool StateMachine_isRecoveryStrobe();            // Recovery: strobe for locating

void StateMachine_printState();

#endif // STATE_MACHINE_H
