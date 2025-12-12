#include "modules/state_machine.h"
#include "modules/relay_controller.h"
#include "config.h"
#include <Arduino.h>
#include <RTC.h>

// ============================================================================
// STATE MACHINE IMPLEMENTATION
// ============================================================================

static StateMachineStatus status;
static TimedEventConfig dropWeightConfig;

// State names for debugging
static const char* stateNames[] = {
    "PREDEPLOYMENT",
    "MISSION",
    "RECOVERY",
    "EMERGENCY"
};

static const char* emergencySourceNames[] = {
    "NONE",
    "ARDUPILOT",
    "DEPTH_SENSOR",
    "TEMPERATURE",
    "PRESSURE",
    "TIMEOUT",
    "MANUAL"
};

// Forward declarations
static void enterState(SystemState newState);
static void handlePreDeploymentState();
static void handleMissionState();
static void handleRecoveryState();
static void handleEmergencyState();
static void checkDropWeightTrigger();

void StateMachine_init() {
    Serial.println(F("================================="));
    Serial.println(F("State Machine: Initializing"));
    Serial.println(F("================================="));

    // Initialize status
    status.currentState = STATE_PREDEPLOYMENT;
    status.previousState = STATE_PREDEPLOYMENT;
    status.stateEntryTime = millis();
    status.timeInState = 0;
    status.emergencySource = EMERGENCY_NONE;
    status.dropWeightArmed = false;
    status.dropWeightReleased = false;
    status.nonessentialsPowered = true;  // Start with everything powered

    // Initialize drop weight config
    dropWeightConfig.enabled = false;
    dropWeightConfig.useAbsoluteTime = false;
    dropWeightConfig.triggerTime = 0;
    dropWeightConfig.durationSeconds = 1500;  // Default 25 minutes (1500s) for electrolytic release

    // Ensure relays in safe state
    RelayController_setPowerManagement(true);  // Nonessentials ON initially

    Serial.print(F("State Machine: Initial state = "));
    Serial.println(stateNames[status.currentState]);
}

void StateMachine_update() {
    unsigned long currentMillis = millis();
    status.timeInState = currentMillis - status.stateEntryTime;

    // Handle current state
    switch (status.currentState) {
        case STATE_PREDEPLOYMENT:
            handlePreDeploymentState();
            break;

        case STATE_MISSION:
            handleMissionState();
            break;

        case STATE_RECOVERY:
            handleRecoveryState();
            break;

        case STATE_EMERGENCY:
            handleEmergencyState();
            break;
    }

    // Update relay controller (handles timed events)
    RelayController_update();
}

SystemState StateMachine_getState() {
    return status.currentState;
}

StateMachineStatus StateMachine_getStatus() {
    return status;
}

bool StateMachine_requestTransition(StateTransition transition) {
    SystemState newState = status.currentState;
    bool allowed = false;

    switch (transition) {
        case TRANSITION_START_MISSION:
            if (status.currentState == STATE_PREDEPLOYMENT) {
                newState = STATE_MISSION;
                allowed = true;
            }
            break;

        case TRANSITION_ENTER_RECOVERY:
            if (status.currentState == STATE_MISSION ||
                status.currentState == STATE_EMERGENCY) {
                newState = STATE_RECOVERY;
                allowed = true;
            }
            break;

        case TRANSITION_ENTER_EMERGENCY:
            // Emergency can be entered from any state except already in emergency
            if (status.currentState != STATE_EMERGENCY) {
                newState = STATE_EMERGENCY;
                allowed = true;
            }
            break;

        case TRANSITION_EXIT_EMERGENCY:
            if (status.currentState == STATE_EMERGENCY) {
                newState = STATE_RECOVERY;
                allowed = true;
            }
            break;

        case TRANSITION_RESET:
            newState = STATE_PREDEPLOYMENT;
            allowed = true;
            break;

        default:
            break;
    }

    if (allowed) {
        enterState(newState);
        return true;
    }

    Serial.print(F("State Machine: Transition denied from "));
    Serial.println(stateNames[status.currentState]);
    return false;
}

bool StateMachine_triggerEmergency(EmergencySource source) {
    if (status.currentState == STATE_EMERGENCY) {
        Serial.println(F("State Machine: Already in emergency mode"));
        return false;
    }

    Serial.print(F("State Machine: EMERGENCY TRIGGERED by "));
    Serial.println(emergencySourceNames[source]);

    status.emergencySource = source;
    enterState(STATE_EMERGENCY);
    return true;
}

bool StateMachine_isEmergency() {
    return (status.currentState == STATE_EMERGENCY);
}

void StateMachine_armDropWeight(bool useGMT, uint32_t triggerTime, uint32_t durationSeconds) {
    dropWeightConfig.enabled = true;
    dropWeightConfig.useAbsoluteTime = useGMT;
    dropWeightConfig.triggerTime = triggerTime;
    dropWeightConfig.durationSeconds = durationSeconds;
    status.dropWeightArmed = true;

    Serial.println(F("State Machine: Drop weight ARMED"));
    Serial.print(F("  Mode: "));
    Serial.println(useGMT ? F("GMT") : F("Delay"));
    Serial.print(F("  Trigger: "));
    Serial.println(triggerTime);
    Serial.print(F("  Duration: "));
    Serial.print(durationSeconds);
    Serial.println(F("s"));
}

void StateMachine_releaseDropWeight() {
    if (status.dropWeightReleased) {
        Serial.println(F("State Machine: Drop weight already released"));
        return;
    }

    Serial.println(F("State Machine: RELEASING DROP WEIGHT NOW"));
    RelayController_triggerTimedEvent(dropWeightConfig.durationSeconds);
    status.dropWeightReleased = true;
    status.dropWeightArmed = false;
}

uint32_t StateMachine_getTimeInState() {
    return status.timeInState / 1000;  // Convert ms to seconds
}

bool StateMachine_canTransmitIridium() {
    // Iridium transmission allowed in all states except PreDeployment (optional)
    // In PreDeployment, only for testing
    return true;  // Allow in all states
}

bool StateMachine_canTransmitMeshtastic() {
    // Meshtastic allowed in all states
    return true;
}

bool StateMachine_shouldShutdownNonessentials() {
    // Shutdown nonessentials in RECOVERY and EMERGENCY states
    return (status.currentState == STATE_RECOVERY ||
            status.currentState == STATE_EMERGENCY);
}

void StateMachine_printState() {
    Serial.println(F("===== STATE MACHINE STATUS ====="));
    Serial.print(F("Current State: "));
    Serial.println(stateNames[status.currentState]);
    Serial.print(F("Time in State: "));
    Serial.print(StateMachine_getTimeInState());
    Serial.println(F(" seconds"));
    Serial.print(F("Drop Weight Armed: "));
    Serial.println(status.dropWeightArmed ? F("YES") : F("NO"));
    Serial.print(F("Drop Weight Released: "));
    Serial.println(status.dropWeightReleased ? F("YES") : F("NO"));
    Serial.print(F("Nonessentials Powered: "));
    Serial.println(status.nonessentialsPowered ? F("YES") : F("NO"));
    if (status.currentState == STATE_EMERGENCY) {
        Serial.print(F("Emergency Source: "));
        Serial.println(emergencySourceNames[status.emergencySource]);
    }
    Serial.println(F("================================"));
}

// ============================================================================
// INTERNAL FUNCTIONS
// ============================================================================

static void enterState(SystemState newState) {
    if (newState == status.currentState) {
        return;  // Already in this state
    }

    Serial.println(F("================================="));
    Serial.print(F("State Transition: "));
    Serial.print(stateNames[status.currentState]);
    Serial.print(F(" → "));
    Serial.println(stateNames[newState]);
    Serial.println(F("================================="));

    status.previousState = status.currentState;
    status.currentState = newState;
    status.stateEntryTime = millis();
    status.timeInState = 0;

    // State entry actions
    switch (newState) {
        case STATE_PREDEPLOYMENT:
            Serial.println(F("Entering PREDEPLOYMENT state"));
            Serial.println(F("- System testing and configuration mode"));
            Serial.println(F("- All systems powered ON"));
            status.nonessentialsPowered = true;
            RelayController_setPowerManagement(true);
            break;

        case STATE_MISSION:
            Serial.println(F("Entering MISSION state"));
            Serial.println(F("- ArduPilot leads decision making"));
            Serial.println(F("- Drop weight armed and monitoring"));
            Serial.println(F("- All systems powered ON"));
            status.nonessentialsPowered = true;
            RelayController_setPowerManagement(true);
            break;

        case STATE_RECOVERY:
            Serial.println(F("Entering RECOVERY state"));
            Serial.println(F("- Low power surface mode"));
            Serial.println(F("- Shutting down nonessential systems"));
            Serial.println(F("- GPS and Iridium reporting for recovery"));
            status.nonessentialsPowered = false;
            RelayController_setPowerManagement(false);  // Shutdown Navigator/Pi/Camera/Lights
            break;

        case STATE_EMERGENCY:
            Serial.println(F("Entering EMERGENCY state"));
            Serial.print(F("- Emergency triggered by: "));
            Serial.println(emergencySourceNames[status.emergencySource]);
            Serial.println(F("- Releasing drop weight IMMEDIATELY"));
            Serial.println(F("- Shutting down nonessential systems"));

            // Immediate actions
            StateMachine_releaseDropWeight();  // Release drop weight now
            status.nonessentialsPowered = false;
            RelayController_setPowerManagement(false);  // Shutdown nonessentials
            break;
    }
}

static void handlePreDeploymentState() {
    // PreDeployment: System testing and configuration
    // Waiting for START_MISSION command from ArduPilot/BlueOS

    // All systems should be operational for testing
    // No automatic transitions - requires explicit command

    static unsigned long lastStatusPrint = 0;
    if (millis() - lastStatusPrint > 60000) {  // Every minute
        Serial.println(F("PreDeployment: Waiting for mission start command"));
        lastStatusPrint = millis();
    }
}

static void handleMissionState() {
    // Mission: ArduPilot is in control, AGT monitors for:
    // 1. Drop weight trigger time
    // 2. Emergency conditions

    // Check if drop weight should be released
    if (status.dropWeightArmed && !status.dropWeightReleased) {
        checkDropWeightTrigger();
    }

    // ArduPilot sends commands for state transitions
    // AGT only acts on emergency conditions autonomously

    static unsigned long lastStatusPrint = 0;
    if (millis() - lastStatusPrint > 300000) {  // Every 5 minutes
        Serial.println(F("Mission: Active"));
        Serial.print(F("  Time in mission: "));
        Serial.print(StateMachine_getTimeInState() / 60);
        Serial.println(F(" minutes"));
        lastStatusPrint = millis();
    }
}

static void handleRecoveryState() {
    // Recovery: Low power surface mode
    // - Nonessentials powered OFF
    // - GPS tracking active
    // - Iridium reporting position
    // - Meshtastic active for local range

    // Stay in recovery until retrieved or reset

    static unsigned long lastStatusPrint = 0;
    if (millis() - lastStatusPrint > 600000) {  // Every 10 minutes
        Serial.println(F("Recovery: Awaiting pickup"));
        Serial.print(F("  Time on surface: "));
        Serial.print(StateMachine_getTimeInState() / 3600);
        Serial.println(F(" hours"));
        lastStatusPrint = millis();
    }
}

static void handleEmergencyState() {
    // Emergency: Drop weight released, nonessentials off
    // System is in safe state
    // Can transition to RECOVERY when conditions stabilize

    // Emergency state typically transitions to RECOVERY automatically
    // after emergency is handled

    static bool autoTransitionDone = false;
    if (!autoTransitionDone && status.timeInState > 30000) {  // 30 seconds after emergency
        Serial.println(F("Emergency: Conditions stable, transitioning to Recovery"));
        StateMachine_requestTransition(TRANSITION_EXIT_EMERGENCY);
        autoTransitionDone = true;
    }
}

static void checkDropWeightTrigger() {
    // Check if it's time to release drop weight
    extern Apollo3RTC &myRTC;  // From main.cpp (alias to platform `rtc`)

    if (dropWeightConfig.useAbsoluteTime) {
        // GMT mode - check RTC time
        uint32_t currentTime = myRTC.getEpoch();
        if (currentTime >= dropWeightConfig.triggerTime) {
            Serial.println(F("Drop weight trigger time reached (GMT)"));
            StateMachine_releaseDropWeight();

            // After successful drop weight release, transition to recovery
            StateMachine_requestTransition(TRANSITION_ENTER_RECOVERY);
        }
    } else {
        // Delay mode - check time since boot
        uint32_t uptime = millis() / 1000;
        if (uptime >= dropWeightConfig.triggerTime) {
            Serial.println(F("Drop weight trigger time reached (Delay)"));
            StateMachine_releaseDropWeight();

            // After successful drop weight release, transition to recovery
            StateMachine_requestTransition(TRANSITION_ENTER_RECOVERY);
        }
    }
}
