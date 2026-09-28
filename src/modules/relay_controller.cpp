#include "modules/relay_controller.h"
#include "config.h"
#include <Arduino.h>

// The native relay tests include this file once per driver mode, each time
// inside its own namespace. Firmware builds leave RELAY_NS unset.
#if defined(RELAY_NS)
namespace RELAY_NS {
#endif

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

#elif defined(PAYLOAD_POWER_POLULU)

static void pulseHigh(int pin) {
    digitalWrite(pin, HIGH);
    delay(PAYLOAD_POWER_PULSE_MS);
    digitalWrite(pin, LOW);
}

// Declared here so init's call resolves to this driver. The header also
// declares the same name at global scope, and a later definition would lose.
void RelayController_setPowerManagement(bool state);

void RelayController_init() {
    // Clear both latches before either pad drives. A high glitch on GPIO4
    // while it becomes an output is an OFF pulse and would drop a payload
    // that is already on. Reset and init must never emit that pulse.
    digitalWrite(PAYLOAD_POWER_ON_PIN, LOW);
    digitalWrite(PAYLOAD_POWER_OFF_PIN, LOW);
    pinMode(PAYLOAD_POWER_ON_PIN, OUTPUT);
    pinMode(PAYLOAD_POWER_OFF_PIN, OUTPUT);

    // Each init represents a boot. Force the logical state off first so this
    // call pulses ON exactly once, including when init runs again in a test,
    // and so an already-on Pololu never sees OFF.
    powerMgmtState = false;
    RelayController_setPowerManagement(true);
    DebugPrintln(F("Relay: Payload power initialized ON"));
}

void RelayController_setPowerManagement(bool state) {
    if (state == powerMgmtState) {
        return;
    }
    powerMgmtState = state;
    if (state) {
        pulseHigh(PAYLOAD_POWER_ON_PIN);
    } else {
        pulseHigh(PAYLOAD_POWER_OFF_PIN);
    }

    DebugPrint(F("Relay: Power management "));
    DebugPrintln(state ? F("ON") : F("OFF"));
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

#if defined(RELAY_NS)
} // namespace RELAY_NS
#endif
