#include "modules/relay_controller.h"
#include "config.h"
#include <Arduino.h>

static bool powerMgmtState = false;
static bool timedEventActive = false;
static unsigned long timedEventStartTime = 0;
static uint16_t timedEventDuration = 0;

void RelayController_init() {
    // Initialize relay pins as outputs
    pinMode(RELAY_POWER_MGMT, OUTPUT);
    pinMode(RELAY_TIMED_EVENT, OUTPUT);

    // Set initial states (relays off)
    digitalWrite(RELAY_POWER_MGMT, RELAY_ACTIVE_HIGH ? LOW : HIGH);
    digitalWrite(RELAY_TIMED_EVENT, RELAY_ACTIVE_HIGH ? LOW : HIGH);

    powerMgmtState = false;
    timedEventActive = false;

    Serial.println(F("Relay: Controller initialized"));
}

void RelayController_setPowerManagement(bool state) {
    powerMgmtState = state;

    if (RELAY_ACTIVE_HIGH) {
        digitalWrite(RELAY_POWER_MGMT, state ? HIGH : LOW);
    } else {
        digitalWrite(RELAY_POWER_MGMT, state ? LOW : HIGH);
    }

    Serial.print(F("Relay: Power management "));
    Serial.println(state ? F("ON") : F("OFF"));
}

bool RelayController_getPowerManagement() {
    return powerMgmtState;
}

void RelayController_triggerTimedEvent(uint16_t durationMs) {
    if (timedEventActive) {
        Serial.println(F("Relay: Timed event already active!"));
        return;
    }

    Serial.print(F("Relay: Triggering timed event for "));
    Serial.print(durationMs);
    Serial.println(F("ms"));

    timedEventActive = true;
    timedEventStartTime = millis();
    timedEventDuration = durationMs;

    // Activate relay
    if (RELAY_ACTIVE_HIGH) {
        digitalWrite(RELAY_TIMED_EVENT, HIGH);
    } else {
        digitalWrite(RELAY_TIMED_EVENT, LOW);
    }
}

bool RelayController_isTimedEventActive() {
    return timedEventActive;
}

void RelayController_update() {
    // Check if timed event should be deactivated
    if (timedEventActive) {
        unsigned long currentMillis = millis();
        unsigned long elapsed = currentMillis - timedEventStartTime;

        if (elapsed >= timedEventDuration) {
            // Deactivate timed event relay
            if (RELAY_ACTIVE_HIGH) {
                digitalWrite(RELAY_TIMED_EVENT, LOW);
            } else {
                digitalWrite(RELAY_TIMED_EVENT, HIGH);
            }

            timedEventActive = false;
            Serial.println(F("Relay: Timed event completed"));
        }
    }
}

void RelayController_emergencyDisable() {
    // Disable both relays immediately
    if (RELAY_ACTIVE_HIGH) {
        digitalWrite(RELAY_POWER_MGMT, LOW);
        digitalWrite(RELAY_TIMED_EVENT, LOW);
    } else {
        digitalWrite(RELAY_POWER_MGMT, HIGH);
        digitalWrite(RELAY_TIMED_EVENT, HIGH);
    }

    powerMgmtState = false;
    timedEventActive = false;

    Serial.println(F("Relay: EMERGENCY DISABLE"));
}
