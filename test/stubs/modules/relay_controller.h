// Stands in for include/modules/relay_controller.h. test/stubs comes before
// include on the native env's search path, so any code under test that asks for
// "modules/relay_controller.h" lands here instead. It deliberately claims the
// real header's guard, so a suite that wants the real implementation can
// include that header first and reduce this file to nothing -- otherwise the
// real definitions would collide with these inline ones. See
// test_relay_controller.
#ifndef RELAY_CONTROLLER_H
#define RELAY_CONTROLLER_H

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

static inline void RelayController_requestRelease() {
    if (!_stub_timed_event_active) _stub_timed_event_trigger_count++;
    _stub_timed_event_active = true;
}

static inline bool RelayController_requestReleaseOff(bool surfaceSafe) {
    if (!surfaceSafe) return false;
    _stub_timed_event_active = false;
    return true;
}

static inline bool RelayController_isReleaseActive() {
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

#endif // RELAY_CONTROLLER_H
