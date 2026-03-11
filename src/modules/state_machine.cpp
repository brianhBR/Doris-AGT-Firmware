#include "modules/state_machine.h"
#include "modules/relay_controller.h"
#include "config.h"
#include <Arduino.h>

static StateMachineStatus status;
static const char* stateNames[] = {
    "PRE_MISSION",
    "SELF_TEST",
    "MISSION",
    "RECOVERY"
};
static const char* failsafeNames[] = {
    "NONE", "LOW_VOLTAGE", "LEAK", "MAX_DEPTH", "NO_HEARTBEAT", "MANUAL"
};

static void enterState(SystemState newState);
static void handlePreMission();
static void handleSelfTest();
static void handleMission();
static void handleRecovery();

void StateMachine_init() {
    status.currentState = STATE_PRE_MISSION;
    status.previousState = STATE_PRE_MISSION;
    status.stateEntryTime = millis();
    status.timeInState = 0;
    status.lastFailsafeSource = FAILSAFE_NONE;
    status.releaseTriggered = false;
    status.nonessentialsPowered = true;
    RelayController_setPowerManagement(true);
    Serial.println(F("State: PRE_MISSION"));
}

void StateMachine_update() {
    unsigned long now = millis();
    status.timeInState = now - status.stateEntryTime;

    switch (status.currentState) {
        case STATE_PRE_MISSION:
            handlePreMission();
            break;
        case STATE_SELF_TEST:
            handleSelfTest();
            break;
        case STATE_MISSION:
            handleMission();
            break;
        case STATE_RECOVERY:
            handleRecovery();
            break;
    }
    RelayController_update();
}

SystemState StateMachine_getState() {
    return status.currentState;
}

StateMachineStatus StateMachine_getStatus() {
    return status;
}

uint32_t StateMachine_getTimeInState() {
    return status.timeInState / 1000;
}

void StateMachine_startSelfTest() {
    if (status.currentState != STATE_PRE_MISSION) {
        Serial.println(F("State: startSelfTest only from PRE_MISSION"));
        return;
    }
    enterState(STATE_SELF_TEST);
}

void StateMachine_enterMission() {
    if (status.currentState != STATE_SELF_TEST) {
        Serial.println(F("State: enterMission only from SELF_TEST"));
        return;
    }
    enterState(STATE_MISSION);
}

void StateMachine_enterRecovery() {
    enterState(STATE_RECOVERY);
}

void StateMachine_reset() {
    enterState(STATE_PRE_MISSION);
}

void StateMachine_triggerFailsafe(FailsafeSource source) {
    status.lastFailsafeSource = source;
    Serial.print(F("FAILSAFE: "));
    Serial.println(failsafeNames[source]);
    if (!status.releaseTriggered) {
        status.releaseTriggered = true;
        RelayController_triggerTimedEvent(RELEASE_RELAY_DURATION_SEC);
    }
    enterState(STATE_RECOVERY);
}

bool StateMachine_canTransmitIridium() {
    return status.currentState == STATE_SELF_TEST || status.currentState == STATE_RECOVERY;
}

bool StateMachine_canTransmitMeshtastic() {
    return true;
}

bool StateMachine_shouldShutdownNonessentials() {
    return status.currentState == STATE_RECOVERY;
}

bool StateMachine_isRecoveryStrobe() {
    return status.currentState == STATE_RECOVERY;
}

void StateMachine_printState() {
    Serial.println(F("===== STATE ====="));
    Serial.print(F("State: "));
    Serial.println(stateNames[status.currentState]);
    Serial.print(F("Time in state: "));
    Serial.print(StateMachine_getTimeInState());
    Serial.println(F(" s"));
    Serial.print(F("Nonessentials: "));
    Serial.println(status.nonessentialsPowered ? F("ON") : F("OFF"));
    Serial.print(F("Release triggered: "));
    Serial.println(status.releaseTriggered ? F("YES") : F("NO"));
    if (status.lastFailsafeSource != FAILSAFE_NONE) {
        Serial.print(F("Last failsafe: "));
        Serial.println(failsafeNames[status.lastFailsafeSource]);
    }
    Serial.println(F("================="));
}

static void enterState(SystemState newState) {
    if (newState == status.currentState) return;

    Serial.print(F("State: "));
    Serial.print(stateNames[status.currentState]);
    Serial.print(F(" -> "));
    Serial.println(stateNames[newState]);

    status.previousState = status.currentState;
    status.currentState = newState;
    status.stateEntryTime = millis();
    status.timeInState = 0;

    switch (newState) {
        case STATE_PRE_MISSION:
            status.nonessentialsPowered = true;
            RelayController_setPowerManagement(true);
            break;
        case STATE_SELF_TEST:
            status.nonessentialsPowered = true;
            RelayController_setPowerManagement(true);
            break;
        case STATE_MISSION:
            status.nonessentialsPowered = true;
            RelayController_setPowerManagement(true);
            break;
        case STATE_RECOVERY:
            status.nonessentialsPowered = false;
            RelayController_setPowerManagement(false);
            break;
    }
}

static void handlePreMission() {
    // Wait for external trigger to start self test (e.g. serial command or BlueOS)
    (void)status;
}

static void handleSelfTest() {
    // Self test is run from main (GPS, Iridium, battery, mission loaded).
    // Transition to MISSION happens when depth > 2m (from main, using MissionData).
    (void)status;
}

static void handleMission() {
    // Failsafe checks are done in main. Transition to RECOVERY when depth < 3m or GPS fix (in main).
    (void)status;
}

static void handleRecovery() {
    (void)status;
}
