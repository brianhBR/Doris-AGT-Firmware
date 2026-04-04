#include "modules/relay_controller.h"
#include "config.h"
#include <Arduino.h>

static bool powerMgmtState = false;
static bool timedEventActive = false;
static unsigned long timedEventStartTime = 0;
static uint32_t timedEventDurationSeconds = 0;

#ifdef NO_RELAYS

// Relays disabled at compile time — state tracking and logging only, no pin drives.
// Build with -DNO_RELAYS (no-relays environment).

void RelayController_init() {
    powerMgmtState = true;
    timedEventActive = false;
    DebugPrintln(F("Relay: DISABLED (build flag) — no pins will be driven"));
}

void RelayController_setPowerManagement(bool state) {
    powerMgmtState = state;
    DebugPrint(F("Relay: Power management -> "));
    DebugPrintln(state ? F("ON (simulated)") : F("OFF (simulated)"));
}

bool RelayController_getPowerManagement() {
    return powerMgmtState;
}

void RelayController_triggerTimedEvent(uint32_t durationSeconds) {
    if (timedEventActive) return;
    timedEventActive = true;
    timedEventStartTime = millis();
    timedEventDurationSeconds = durationSeconds;
    DebugPrint(F("Relay: Timed event logged (simulated) for "));
    DebugPrint(durationSeconds);
    DebugPrintln(F("s"));
}

bool RelayController_isTimedEventActive() {
    return timedEventActive;
}

void RelayController_update() {
    if (timedEventActive) {
        unsigned long elapsedSeconds = (millis() - timedEventStartTime) / 1000;
        if (elapsedSeconds >= timedEventDurationSeconds) {
            timedEventActive = false;
            DebugPrintln(F("Relay: Timed event completed (simulated)"));
        }
    }
}

void RelayController_emergencyDisable() {
    powerMgmtState = false;
    timedEventActive = false;
    DebugPrintln(F("Relay: EMERGENCY DISABLE (simulated)"));
}

#else

// Production build — relay pins are driven.

static void driveRelay(int pin, bool nc, bool conduct) {
    bool coilOn;
    if (nc) {
        coilOn = !conduct;
    } else {
        coilOn = conduct;
    }
    digitalWrite(pin, (RELAY_COIL_ACTIVE_HIGH == coilOn) ? HIGH : LOW);
}

void RelayController_init() {
    pinMode(RELAY_POWER_MGMT, OUTPUT);
    pinMode(RELAY_TIMED_EVENT, OUTPUT);

    driveRelay(RELAY_POWER_MGMT, RELAY_POWER_MGMT_NC, true);   // devices ON
    driveRelay(RELAY_TIMED_EVENT, RELAY_TIMED_EVENT_NC, false); // release OFF

    powerMgmtState = true;
    timedEventActive = false;

    DebugPrintln(F("Relay: Controller initialized (Pi power ON)"));
}

void RelayController_setPowerManagement(bool state) {
    powerMgmtState = state;
    driveRelay(RELAY_POWER_MGMT, RELAY_POWER_MGMT_NC, state);

    DebugPrint(F("Relay: Power management "));
    DebugPrintln(state ? F("ON") : F("OFF"));
}

bool RelayController_getPowerManagement() {
    return powerMgmtState;
}

void RelayController_triggerTimedEvent(uint32_t durationSeconds) {
    if (timedEventActive) {
        DebugPrintln(F("Relay: Timed event already active!"));
        return;
    }

    DebugPrint(F("Relay: Triggering timed event for "));
    DebugPrint(durationSeconds);
    DebugPrintln(F("s"));

    timedEventActive = true;
    timedEventStartTime = millis();
    timedEventDurationSeconds = durationSeconds;

    driveRelay(RELAY_TIMED_EVENT, RELAY_TIMED_EVENT_NC, true);
}

bool RelayController_isTimedEventActive() {
    return timedEventActive;
}

void RelayController_update() {
    if (timedEventActive) {
        unsigned long elapsedSeconds = (millis() - timedEventStartTime) / 1000;
        if (elapsedSeconds >= timedEventDurationSeconds) {
            driveRelay(RELAY_TIMED_EVENT, RELAY_TIMED_EVENT_NC, false);
            timedEventActive = false;
            DebugPrintln(F("Relay: Timed event completed"));
        }
    }
}

void RelayController_emergencyDisable() {
    driveRelay(RELAY_POWER_MGMT, RELAY_POWER_MGMT_NC, false);
    driveRelay(RELAY_TIMED_EVENT, RELAY_TIMED_EVENT_NC, false);

    powerMgmtState = false;
    timedEventActive = false;

    DebugPrintln(F("Relay: EMERGENCY DISABLE"));
}

#endif // NO_RELAYS
