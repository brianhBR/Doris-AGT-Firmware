#include "modules/state_machine.h"
#include "modules/relay_controller.h"
#include "config.h"
#include <Arduino.h>

static StateMachineStatus status;
static const char* stateNames[] = {
    "PRE_DIVE",
    "DIVING",
    "RECOVERY"
};
static const char* failsafeNames[] = {
    "NONE", "LOW_VOLTAGE", "LEAK", "NO_HEARTBEAT", "MANUAL"
};

static void enterState(SystemState newState);

void StateMachine_init() {
    status.currentState = STATE_PRE_DIVE;
    status.previousState = STATE_PRE_DIVE;
    status.stateEntryTime = millis();
    status.timeInState = 0;
    status.lastFailsafeSource = FAILSAFE_NONE;
    status.releaseTriggered = false;
    status.nonessentialsPowered = true;
    RelayController_setPowerManagement(true);
    DebugPrintln(F("State: PRE_DIVE"));
}

void StateMachine_update() {
    status.timeInState = millis() - status.stateEntryTime;
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

void StateMachine_enterDiving() {
    if (status.currentState != STATE_PRE_DIVE) {
        DebugPrintln(F("State: enterDiving only from PRE_DIVE"));
        return;
    }
    enterState(STATE_DIVING);
}

void StateMachine_enterRecovery() {
    enterState(STATE_RECOVERY);
}

void StateMachine_reset() {
    status.lastFailsafeSource = FAILSAFE_NONE;
    status.releaseTriggered = false;
    enterState(STATE_PRE_DIVE);
}

void StateMachine_triggerFailsafe(FailsafeSource source) {
    status.lastFailsafeSource = source;
    DebugPrint(F("FAILSAFE: "));
    DebugPrintln(failsafeNames[source]);
    if (!status.releaseTriggered) {
        status.releaseTriggered = true;
        RelayController_triggerTimedEvent(RELEASE_RELAY_DURATION_SEC);
    }
    enterState(STATE_RECOVERY);
}

bool StateMachine_canTransmitIridium() {
    return status.currentState == STATE_RECOVERY;
}

bool StateMachine_shouldShutdownNonessentials() {
    return status.currentState == STATE_RECOVERY;
}

bool StateMachine_isRecoveryStrobe() {
    return status.currentState == STATE_RECOVERY;
}

void StateMachine_printState() {
    DebugPrintln(F("===== STATE ====="));
    DebugPrint(F("State: "));
    DebugPrintln(stateNames[status.currentState]);
    DebugPrint(F("Time in state: "));
    DebugPrint(StateMachine_getTimeInState());
    DebugPrintln(F(" s"));
    DebugPrint(F("Nonessentials: "));
    DebugPrintln(status.nonessentialsPowered ? F("ON") : F("OFF"));
    DebugPrint(F("Release triggered: "));
    DebugPrintln(status.releaseTriggered ? F("YES") : F("NO"));
    if (status.lastFailsafeSource != FAILSAFE_NONE) {
        DebugPrint(F("Last failsafe: "));
        DebugPrintln(failsafeNames[status.lastFailsafeSource]);
    }
    DebugPrintln(F("================="));
}

static void enterState(SystemState newState) {
    if (newState == status.currentState) return;

    DebugPrint(F("State: "));
    DebugPrint(stateNames[status.currentState]);
    DebugPrint(F(" -> "));
    DebugPrintln(stateNames[newState]);

    status.previousState = status.currentState;
    status.currentState = newState;
    status.stateEntryTime = millis();
    status.timeInState = 0;

    switch (newState) {
        case STATE_PRE_DIVE:
            status.nonessentialsPowered = true;
            RelayController_setPowerManagement(true);
            break;
        case STATE_DIVING:
            status.nonessentialsPowered = true;
            RelayController_setPowerManagement(true);
            break;
        case STATE_RECOVERY:
            status.nonessentialsPowered = false;
            RelayController_setPowerManagement(false);
            break;
    }
}
