#include "modules/relay_controller.h"
#include "config.h"
#include <Arduino.h>
#include <EEPROM.h>

static bool powerMgmtState = false;
static bool timedEventActive = false;
static unsigned long timedEventStartTime = 0;
static uint32_t timedEventDurationSeconds = 0;

// Separate from SystemConfig and written only on release state transitions.
// A persisted ON marker makes an AGT reboot re-energize GPIO35; a corrupt or
// unknown record fails safe to OFF rather than inventing a release request.
static const int RELEASE_EEPROM_ADDRESS = 256;
#ifdef NO_RELAYS
static const uint32_t RELEASE_EEPROM_MAGIC = 0x52454C30UL; // "REL0" simulation
#else
static const uint32_t RELEASE_EEPROM_MAGIC = 0x52454C31UL; // "REL1"
#endif
static_assert(sizeof(SystemConfig) <= RELEASE_EEPROM_ADDRESS,
              "Release EEPROM record overlaps SystemConfig");
struct ReleaseRecord {
    uint32_t magic;
    uint8_t active;
    uint8_t inverse;
    uint16_t check;
};

static bool releaseRecordInBounds() {
    return RELEASE_EEPROM_ADDRESS >= (int)sizeof(SystemConfig) &&
           (size_t)RELEASE_EEPROM_ADDRESS + sizeof(ReleaseRecord) <=
               (size_t)EEPROM.length();
}

static void persistRelease(bool active) {
    if (!releaseRecordInBounds()) {
        return;
    }
    ReleaseRecord record = {
        RELEASE_EEPROM_MAGIC,
        (uint8_t)(active ? 1 : 0),
        (uint8_t)(active ? 0 : 1),
        (uint16_t)(0xA55AU ^ (active ? 1U : 0U))
    };
    EEPROM.put(RELEASE_EEPROM_ADDRESS, record);
}

static bool loadPersistedRelease() {
    if (!releaseRecordInBounds()) {
        return false;
    }
    ReleaseRecord record;
    EEPROM.get(RELEASE_EEPROM_ADDRESS, record);
    return record.magic == RELEASE_EEPROM_MAGIC &&
           record.active <= 1 &&
           record.inverse == (uint8_t)(record.active ? 0 : 1) &&
           record.check == (uint16_t)(0xA55AU ^ record.active) &&
           record.active == 1;
}

#ifdef NO_RELAYS

// Relays disabled at compile time — state tracking and logging only, no pin drives.
// Build with -DNO_RELAYS (no-relays environment).

void RelayController_init() {
    powerMgmtState = true;
    timedEventActive = loadPersistedRelease();
    timedEventStartTime = millis();
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
    timedEventDurationSeconds = durationSeconds;
    RelayController_requestRelease();
    DebugPrint(F("Relay: Timed event logged (simulated) for "));
    DebugPrint(durationSeconds);
    DebugPrintln(F("s"));
}

bool RelayController_isTimedEventActive() {
    return timedEventActive;
}

void RelayController_update() {
}

void RelayController_requestRelease() {
    if (timedEventActive) {
        return;
    }
    timedEventActive = true;
    timedEventStartTime = millis();
    persistRelease(true);
}

bool RelayController_requestReleaseOff(bool surfaceSafe) {
    if (!timedEventActive) {
        return true;
    }
    if (!surfaceSafe ||
        (millis() - timedEventStartTime) < RELEASE_MIN_HOLD_SEC * 1000UL) {
        return false;
    }
    timedEventActive = false;
    persistRelease(false);
    return true;
}

bool RelayController_isReleaseActive() {
    return timedEventActive;
}

void RelayController_emergencyDisable() {
    powerMgmtState = false;
    DebugPrintln(F("Relay: EMERGENCY power disable (release latch preserved, simulated)"));
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
    timedEventActive = loadPersistedRelease();
    timedEventStartTime = millis();
    driveRelay(RELAY_TIMED_EVENT, RELAY_TIMED_EVENT_NC, timedEventActive);

    powerMgmtState = true;
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
    timedEventDurationSeconds = durationSeconds;
    RelayController_requestRelease();
}

bool RelayController_isTimedEventActive() {
    return timedEventActive;
}

void RelayController_update() {
}

void RelayController_requestRelease() {
    if (timedEventActive) {
        // Repeated Lua RELAY=1 messages keep the latched output asserted.
        driveRelay(RELAY_TIMED_EVENT, RELAY_TIMED_EVENT_NC, true);
        return;
    }
    timedEventActive = true;
    timedEventStartTime = millis();
    persistRelease(true);
    driveRelay(RELAY_TIMED_EVENT, RELAY_TIMED_EVENT_NC, true);
    DebugPrintln(F("Relay: Release latched ON"));
}

bool RelayController_requestReleaseOff(bool surfaceSafe) {
    if (!timedEventActive) {
        return true;
    }
    if (!surfaceSafe ||
        (millis() - timedEventStartTime) < RELEASE_MIN_HOLD_SEC * 1000UL) {
        return false;
    }
    driveRelay(RELAY_TIMED_EVENT, RELAY_TIMED_EVENT_NC, false);
    timedEventActive = false;
    persistRelease(false);
    DebugPrintln(F("Relay: Release explicitly cleared"));
    return true;
}

bool RelayController_isReleaseActive() {
    return timedEventActive;
}

void RelayController_emergencyDisable() {
    driveRelay(RELAY_POWER_MGMT, RELAY_POWER_MGMT_NC, false);

    powerMgmtState = false;

    DebugPrintln(F("Relay: EMERGENCY power disable (release latch preserved)"));
}

#endif // NO_RELAYS
