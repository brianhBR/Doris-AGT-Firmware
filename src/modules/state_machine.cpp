#include "modules/state_machine.h"
#include "modules/relay_controller.h"
#include "modules/mission_data.h"
#include "config.h"
#include <Arduino.h>

static StateMachineStatus status;
static const char* stateNames[] = {
    "PRE_DIVE",
    "DIVING",
    "RECOVERY"
};
static const char* failsafeNames[] = {
    "NONE", "LOW_VOLTAGE", "LEAK", "NO_HEARTBEAT", "MANUAL", "IRIDIUM"
};
static unsigned long shutdownAckTime = 0;
static unsigned long surfaceDwellStart = 0;
static bool surfaceDwellActive = false;
static unsigned long criticalVoltageStart = 0;
static bool criticalVoltageTimingActive = false;

// Depth is used only by the independent backstop that can enter RECOVERY for
// Iridium and strobe behavior if Lua is wedged. It is deliberately not a vote
// for payload power cutoff; only Lua's repeated, fresh STATE=4 can authorize
// that operation.
struct DepthWindow {
    unsigned long start;
    float min;
    float max;
    bool active;
};

static DepthWindow backstopWindow;

static void enterState(SystemState newState);

static void depthWindowReset(DepthWindow* w) {
    w->start = 0;
    w->min = 0.0f;
    w->max = 0.0f;
    w->active = false;
}

static bool depthWindowQualified(DepthWindow* w, float depth,
                                 unsigned long holdMs) {
    if (!w->active) {
        w->active = true;
        w->start = millis();
        w->min = depth;
        w->max = depth;
    } else {
        if (depth < w->min) w->min = depth;
        if (depth > w->max) w->max = depth;
    }
    return (w->max - w->min) >= SURFACE_DEPTH_LIVENESS_M &&
           millis() - w->start >= holdMs;
}

void StateMachine_init() {
    status.currentState = STATE_PRE_DIVE;
    status.previousState = STATE_PRE_DIVE;
    status.stateEntryTime = millis();
    status.timeInState = 0;
    status.lastFailsafeSource = FAILSAFE_NONE;
    status.releaseTriggered = RelayController_isReleaseActive();
    status.nonessentialsPowered = true;
    status.surfaceQualified = false;
    status.shutdownRequested = false;
    status.shutdownAcknowledged = false;
    shutdownAckTime = 0;
    surfaceDwellStart = 0;
    surfaceDwellActive = false;
    criticalVoltageStart = 0;
    criticalVoltageTimingActive = false;
    depthWindowReset(&backstopWindow);
    // A power cycle always restores Pi power. Nothing about the cutoff decision
    // is persisted, and the dive evidence behind surface qualification is RAM
    // only, so the vehicle must dive and reach recovery again before the AGT
    // can cut power a second time.
    RelayController_setPowerManagement(true);
    DebugPrintln(F("State: PRE_DIVE"));
}

void StateMachine_update() {
    status.timeInState = millis() - status.stateEntryTime;
    RelayController_update();
    status.releaseTriggered = RelayController_isReleaseActive();

    if (status.currentState != STATE_DIVING ||
        status.timeInState < DIVE_HEARTBEAT_GRACE_MS) {
        criticalVoltageTimingActive = false;
        return;
    }

    MissionData md;
    MissionData_get(&md);
    if (md.leak_detected) {
        StateMachine_triggerFailsafe(FAILSAFE_LEAK);
        return;
    }

    bool criticalVoltage = MissionData_isAutopilotVoltageFresh() &&
                           md.battery_voltage > 0.0f &&
                           md.battery_voltage <= BATTERY_CRITICAL_VOLTAGE;
    if (criticalVoltage) {
        if (!criticalVoltageTimingActive) {
            criticalVoltageStart = millis();
            criticalVoltageTimingActive = true;
        }
        if (millis() - criticalVoltageStart >= 10000UL) {
            StateMachine_triggerFailsafe(FAILSAFE_LOW_VOLTAGE);
            return;
        }
    } else {
        criticalVoltageTimingActive = false;
    }

    if (MissionData_hasHadHeartbeat() &&
        millis() - md.last_heartbeat_ms >= FAILSAFE_HEARTBEAT_TIMEOUT_MS) {
        StateMachine_triggerFailsafe(FAILSAFE_NO_HEARTBEAT);
    }
}

void StateMachine_updateSurfacePower() {
    // Once BlueOS has acknowledged that storage is safe, its own shutdown will
    // stop MAVLink. Latch this final countdown in RAM so transport loss cannot
    // cancel the electrical cutoff. A power cycle clears the latch in init().
    if (status.shutdownAcknowledged) {
        if (millis() - shutdownAckTime >= POWER_SHUTDOWN_FINAL_GRACE_MS) {
            RelayController_setPowerManagement(false);
            status.nonessentialsPowered = false;
        }
        return;
    }

    MissionData md;
    MissionData_get(&md);
    bool qualifiedNow =
        status.currentState == STATE_RECOVERY &&
        status.previousState == STATE_DIVING &&
        MissionData_isDorisStateFresh() &&
        md.doris_state == 4 &&
        MissionData_getRecoveryMessageCount() >= SURFACE_RECOVERY_MESSAGES;

    if (!qualifiedNow) {
        surfaceDwellStart = 0;
        surfaceDwellActive = false;
        status.surfaceQualified = false;
        status.shutdownRequested = false;
        return;
    }

    status.surfaceQualified = true;
    if (!surfaceDwellActive) {
        surfaceDwellStart = millis();
        surfaceDwellActive = true;
    }

    status.shutdownRequested =
        millis() - surfaceDwellStart >= SURFACE_LOGGING_DWELL_MS;
}

bool StateMachine_updateSurfaceBackstop() {
    MissionData md;
    MissionData_get(&md);
    bool ascending = status.currentState == STATE_DIVING && md.doris_state >= 3;
    bool shallow = MissionData_isDepthFresh() && md.depth_valid &&
                   md.depth_m < RECOVERY_DEPTH_THRESHOLD_M;
    if (!ascending || !shallow) {
        depthWindowReset(&backstopWindow);
        return false;
    }
    if (!depthWindowQualified(&backstopWindow, md.depth_m, SURFACE_QUALIFY_MS)) {
        return false;
    }
    depthWindowReset(&backstopWindow);
    enterState(STATE_RECOVERY);
    return true;
}

bool StateMachine_acknowledgeShutdown() {
    if (!status.shutdownRequested || !status.surfaceQualified) {
        return false;
    }
    if (status.shutdownAcknowledged) {
        return true;
    }
    status.shutdownAcknowledged = true;
    shutdownAckTime = millis();
    return true;
}

bool StateMachine_isShutdownRequested() {
    return status.shutdownRequested;
}

bool StateMachine_handleReleaseCommand(bool releaseOn) {
    if (releaseOn) {
        RelayController_requestRelease();
        status.releaseTriggered = true;
        return true;
    }
    bool accepted = RelayController_requestReleaseOff(status.surfaceQualified);
    status.releaseTriggered = RelayController_isReleaseActive();
    return accepted;
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
    status.releaseTriggered = RelayController_isReleaseActive();
    enterState(STATE_PRE_DIVE);
}

void StateMachine_triggerFailsafe(FailsafeSource source) {
    bool remoteOrManual = source == FAILSAFE_MANUAL || source == FAILSAFE_IRIDIUM;
    if (status.currentState != STATE_DIVING && !remoteOrManual) {
        return;
    }
    status.lastFailsafeSource = source;
    DebugPrint(F("FAILSAFE: "));
    DebugPrintln(failsafeNames[source]);
    RelayController_requestRelease();
    status.releaseTriggered = true;
    if (status.currentState == STATE_DIVING) {
        enterState(STATE_RECOVERY);
    }
}

bool StateMachine_canTransmitIridium() {
    // Automatic recovery reports use a blocking modem transaction. Starting
    // one before cutoff can postpone the logging dwell and shutdown handshake
    // for many minutes when satellite acquisition is poor.
    return status.currentState == STATE_RECOVERY &&
           !status.nonessentialsPowered;
}

bool StateMachine_shouldShutdownNonessentials() {
    return !status.nonessentialsPowered;
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
            status.surfaceQualified = false;
            status.shutdownRequested = false;
            status.shutdownAcknowledged = false;
            shutdownAckTime = 0;
            surfaceDwellStart = 0;
            surfaceDwellActive = false;
            break;
        case STATE_DIVING:
            status.nonessentialsPowered = true;
            RelayController_setPowerManagement(true);
            status.surfaceQualified = false;
            status.shutdownRequested = false;
            status.shutdownAcknowledged = false;
            shutdownAckTime = 0;
            surfaceDwellStart = 0;
            surfaceDwellActive = false;
            break;
        case STATE_RECOVERY:
            // RECOVERY enables comms/strobe only. Pi power remains on until
            // repeated fresh Lua STATE=4, the logging dwell, and BlueOS ACK.
            status.nonessentialsPowered = true;
            RelayController_setPowerManagement(true);
            break;
    }
}
