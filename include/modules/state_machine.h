#ifndef STATE_MACHINE_H
#define STATE_MACHINE_H

#include <stdint.h>

// ============================================================================
// SYSTEM STATE DEFINITIONS
// ============================================================================

enum SystemState {
    STATE_PREDEPLOYMENT,    // Configuration and system test (surface)
    STATE_MISSION,          // Active mission (ArduPilot leads decision making)
    STATE_RECOVERY,         // Low power surface mode (post-mission)
    STATE_EMERGENCY         // Emergency mode (release + shutdown nonessentials)
};

// State transition triggers
enum StateTransition {
    TRANSITION_NONE,
    TRANSITION_START_MISSION,      // PreDeployment → Mission (commanded by ArduPilot)
    TRANSITION_ENTER_RECOVERY,     // Mission → Recovery (ballast released, surfaced)
    TRANSITION_ENTER_EMERGENCY,    // Any → Emergency (triggered by ArduPilot or AGT sensors)
    TRANSITION_EXIT_EMERGENCY,     // Emergency → Recovery (after emergency handled)
    TRANSITION_RESET               // Any → PreDeployment (system reset)
};

// Emergency trigger sources
enum EmergencySource {
    EMERGENCY_NONE,
    EMERGENCY_ARDUPILOT,           // Triggered by ArduPilot/Navigator command
    EMERGENCY_DEPTH_SENSOR,        // Depth sensor threshold exceeded
    EMERGENCY_TEMPERATURE,         // Temperature out of range
    EMERGENCY_PRESSURE,            // Pressure sensor issue
    EMERGENCY_TIMEOUT,             // Mission timeout exceeded
    EMERGENCY_MANUAL               // Manual trigger via command
};

// State machine status structure
struct StateMachineStatus {
    SystemState currentState;
    SystemState previousState;
    unsigned long stateEntryTime;      // When current state was entered (millis)
    unsigned long timeInState;         // Time spent in current state (ms)
    EmergencySource emergencySource;   // What triggered emergency (if in emergency state)
    bool dropWeightArmed;              // Drop weight relay ready to trigger
    bool dropWeightReleased;           // Drop weight has been released
    bool nonessentialsPowered;         // Status of Relay 1 (Navigator/Pi/Camera/Lights)
};

// ============================================================================
// STATE MACHINE FUNCTIONS
// ============================================================================

// Initialize state machine
void StateMachine_init();

// Update state machine (call in main loop)
void StateMachine_update();

// Get current state
SystemState StateMachine_getState();

// Get state machine status
StateMachineStatus StateMachine_getStatus();

// Request state transition
bool StateMachine_requestTransition(StateTransition transition);

// Trigger emergency mode
bool StateMachine_triggerEmergency(EmergencySource source);

// Check if in emergency mode
bool StateMachine_isEmergency();

// Arm drop weight for timed release
void StateMachine_armDropWeight(bool useGMT, uint32_t triggerTime, uint32_t durationSeconds);

// Manually trigger drop weight (immediate release)
void StateMachine_releaseDropWeight();

// Get time in current state (seconds)
uint32_t StateMachine_getTimeInState();

// State-specific queries
bool StateMachine_canTransmitIridium();   // Is Iridium transmission allowed?
bool StateMachine_canTransmitMeshtastic(); // Is Meshtastic transmission allowed?
bool StateMachine_shouldShutdownNonessentials(); // Should power relay turn off?

// Print current state to serial
void StateMachine_printState();

#endif // STATE_MACHINE_H
