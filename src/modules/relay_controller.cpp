#include "modules/relay_controller.h"
#include "config.h"
#include <Arduino.h>

static bool powerMgmtState = false;

#ifdef NO_RELAYS

// Payload relay disabled at compile time; state tracking remains for tests.

void RelayController_init() {
    powerMgmtState = true;
    DebugPrintln(F("Relay: Payload power output disabled by build flag"));
}

void RelayController_setPowerManagement(bool state) {
    powerMgmtState = state;
    DebugPrint(F("Relay: Power management -> "));
    DebugPrintln(state ? F("ON (simulated)") : F("OFF (simulated)"));
}

bool RelayController_getPowerManagement() {
    return powerMgmtState;
}

#else

// Production build: the AGT drives only the GPIO4 payload-power relay.

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

    driveRelay(RELAY_POWER_MGMT, RELAY_POWER_MGMT_NC, true);

    powerMgmtState = true;
    DebugPrintln(F("Relay: Payload power initialized ON"));
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

#endif // NO_RELAYS
