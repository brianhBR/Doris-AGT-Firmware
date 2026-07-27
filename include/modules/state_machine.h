#ifndef STATE_MACHINE_H
#define STATE_MACHINE_H

#include <stdint.h>

// ============================================================================
// DORIS AGT STATE MACHINE — subordinate to Lua dive script
// ============================================================================
// The AGT does NOT control the dive.  It provides GPS relay, Iridium comms,
// status LEDs, and safety failsafes (voltage / leak / heartbeat).
//
// PRE_DIVE  -> DIVING    Lua STATE=1..3
// DIVING    -> RECOVERY  Lua STATE=4, ascent + shallow depth + AGT GPS backup,
//                        or guarded failsafe
// RECOVERY  -> PRE_DIVE  manual reset only
//
// RECOVERY never directly cuts Pi power. The separate surface-power guard
// requires repeated fresh recovery reports, fresh shallow depth, AGT GPS,
// sustained qualification, BlueOS ACK, and final grace.

enum SystemState {
    STATE_PRE_DIVE,   // Surface: GPS relay, Iridium test, Meshtastic, ready
    STATE_DIVING,     // Underwater: monitor voltage/leak/heartbeat failsafes
    STATE_RECOVERY    // Post-dive: Iridium/strobe; power handshake pending
};

enum FailsafeSource {
    FAILSAFE_NONE,
    FAILSAFE_LOW_VOLTAGE,
    FAILSAFE_LEAK,
    FAILSAFE_NO_HEARTBEAT,
    FAILSAFE_MANUAL,
    FAILSAFE_IRIDIUM
};

struct StateMachineStatus {
    SystemState currentState;
    SystemState previousState;
    unsigned long stateEntryTime;
    unsigned long timeInState;
    FailsafeSource lastFailsafeSource;
    bool releaseTriggered;       // Release relay has been fired
    bool nonessentialsPowered;   // Relay 1 (Navigator/Pi, camera, lights)
    bool surfaceQualified;
    bool shutdownRequested;
    bool shutdownAcknowledged;
};

void StateMachine_init();
void StateMachine_update();
void StateMachine_updateSurfacePower(bool agtGpsFix);
bool StateMachine_acknowledgeShutdown();
bool StateMachine_isShutdownRequested();
bool StateMachine_handleReleaseCommand(bool releaseOn);

SystemState StateMachine_getState();
StateMachineStatus StateMachine_getStatus();
uint32_t StateMachine_getTimeInState();

// PRE_DIVE -> DIVING (called when Lua reports an active dive state)
void StateMachine_enterDiving();

// Enter RECOVERY mission behavior; does not authorize power cutoff
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
