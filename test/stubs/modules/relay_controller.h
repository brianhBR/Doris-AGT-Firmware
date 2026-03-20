#ifndef RELAY_CONTROLLER_H_STUB
#define RELAY_CONTROLLER_H_STUB

#include <stdint.h>

static bool _stub_power_mgmt = true;
static bool _stub_timed_event_active = false;
static uint32_t _stub_timed_event_duration = 0;
static int _stub_timed_event_trigger_count = 0;

static inline void RelayController_init() {
    _stub_power_mgmt = true;
    _stub_timed_event_active = false;
    _stub_timed_event_duration = 0;
    _stub_timed_event_trigger_count = 0;
}

static inline void RelayController_setPowerManagement(bool state) {
    _stub_power_mgmt = state;
}

static inline bool RelayController_getPowerManagement() {
    return _stub_power_mgmt;
}

static inline void RelayController_triggerTimedEvent(uint32_t durationSeconds) {
    _stub_timed_event_active = true;
    _stub_timed_event_duration = durationSeconds;
    _stub_timed_event_trigger_count++;
}

static inline bool RelayController_isTimedEventActive() {
    return _stub_timed_event_active;
}

static inline void RelayController_update() {}

static inline void RelayController_emergencyDisable() {
    _stub_power_mgmt = false;
    _stub_timed_event_active = false;
}

static inline void stub_relay_reset() {
    RelayController_init();
}

#endif // RELAY_CONTROLLER_H_STUB
