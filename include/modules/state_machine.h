#ifndef STATE_MACHINE_H
#define STATE_MACHINE_H

#include <stdint.h>

// ============================================================================
// DORIS AGT STATE MACHINE — subordinate to Lua dive script
// ============================================================================
// The AGT does NOT control the dive.  It provides GPS relay, Iridium comms,
// status LEDs, and safety failsafes (voltage / leak / heartbeat).
//
// PRE_DIVE  -> DIVING    depth > DIVE_DEPTH_THRESHOLD_M (vehicle went underwater)
// DIVING    -> RECOVERY  depth < RECOVERY_DEPTH_THRESHOLD_M AND GPS fix
//                        (after DIVE_MIN_DURATION_MS), OR failsafe trigger
// RECOVERY  -> PRE_DIVE  manual reset only

enum SystemState {
    STATE_PRE_DIVE,   // Surface: GPS relay, Iridium test, Meshtastic, ready
    STATE_DIVING,     // Underwater: monitor voltage/leak/heartbeat failsafes
    STATE_RECOVERY    // Post-dive: Iridium reporting, strobe, low-power
};

enum FailsafeSource {
    FAILSAFE_NONE,
    FAILSAFE_LOW_VOLTAGE,
    FAILSAFE_LEAK,
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

void StateMachine_init();
void StateMachine_update();

SystemState StateMachine_getState();
StateMachineStatus StateMachine_getStatus();
uint32_t StateMachine_getTimeInState();

// PRE_DIVE -> DIVING (called when depth crosses threshold)
void StateMachine_enterDiving();

// DIVING -> RECOVERY (called when surfaced with GPS, or failsafe)
void StateMachine_enterRecovery();

// Reset to PRE_DIVE
void StateMachine_reset();

// Failsafe: trigger release relay and enter recovery
void StateMachine_triggerFailsafe(FailsafeSource source);

// Transmission gating
bool StateMachine_canTransmitIridium();

// Recovery queries
bool StateMachine_shouldShutdownNonessentials();
bool StateMachine_isRecoveryStrobe();

void StateMachine_printState();

#endif // STATE_MACHINE_H
