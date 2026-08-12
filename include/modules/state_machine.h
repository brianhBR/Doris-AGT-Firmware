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
// DIVING    -> RECOVERY  Lua STATE=4, or the AGT's own sustained shallow-depth
//                        backstop, or guarded failsafe
// RECOVERY  -> PRE_DIVE  manual reset only
//
// RECOVERY never directly cuts Pi power. The separate surface-power guard
// requires repeated fresh Lua STATE=4 reports, a powered surface-logging
// dwell, BlueOS ACK, and a final electrical grace. The independent depth
// backstop may enter RECOVERY for communications but cannot authorize cutoff.

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
    bool surfaceQualified;      // Fresh repeated Lua STATE=4 is confirmed
    bool shutdownRequested;
    bool shutdownAcknowledged;
};

void StateMachine_init();
void StateMachine_update();
void StateMachine_updateSurfacePower();

// The AGT's own path to RECOVERY when Lua is wedged in ASCENT: sustained
// shallow depth that is also moving, with no GPS fix required. This enables
// recovery communications only and never authorizes power cutoff. Call every
// loop; returns true on the tick it enters RECOVERY.
bool StateMachine_updateSurfaceBackstop();

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

// Automatic recovery transmission is allowed only after the acknowledged
// payload-power cutoff. Manual operator tests are gated separately.
bool StateMachine_canTransmitIridium();

// Recovery queries
bool StateMachine_shouldShutdownNonessentials();
bool StateMachine_isRecoveryStrobe();

void StateMachine_printState();

#endif // STATE_MACHINE_H
