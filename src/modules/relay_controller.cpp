#include "modules/relay_controller.h"
#include "config.h"
#include <Arduino.h>

static bool powerMgmtState = false;
static bool timedEventActive = false;
static unsigned long timedEventStartTime = 0;
static uint32_t timedEventDurationSeconds = 0;

void RelayController_init() {
    pinMode(RELAY_POWER_MGMT, OUTPUT);
    pinMode(RELAY_TIMED_EVENT, OUTPUT);

    // Pi/Navigator relay defaults ON — never cut power unless we have a confirmed reason
    digitalWrite(RELAY_POWER_MGMT, RELAY_ACTIVE_HIGH ? HIGH : LOW);
    digitalWrite(RELAY_TIMED_EVENT, RELAY_ACTIVE_HIGH ? LOW : HIGH);

    powerMgmtState = true;
    timedEventActive = false;

    Serial.println(F("Relay: Controller initialized (Pi power ON)"));
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

void RelayController_triggerTimedEvent(uint32_t durationSeconds) {
    if (timedEventActive) {
        Serial.println(F("Relay: Timed event already active!"));
        return;
    }

    Serial.print(F("Relay: Triggering timed event for "));
    Serial.print(durationSeconds);
    Serial.println(F("s"));

    timedEventActive = true;
    timedEventStartTime = millis();
    timedEventDurationSeconds = durationSeconds;

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
        unsigned long elapsedMs = currentMillis - timedEventStartTime;
        unsigned long elapsedSeconds = elapsedMs / 1000;

        if (elapsedSeconds >= timedEventDurationSeconds) {
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
