// Stands in for include/modules/relay_controller.h. test/stubs comes before
// include on the native env's search path, so any code under test that asks for
// "modules/relay_controller.h" lands here instead. It deliberately claims the
// real header's guard, so a suite that wants the real implementation can
// include that header first and reduce this file to nothing -- otherwise the
// real definitions would collide with these inline ones. See
// test_relay_controller.
#ifndef RELAY_CONTROLLER_H
#define RELAY_CONTROLLER_H

static bool _stub_power_mgmt = true;

static inline void RelayController_init() {
    _stub_power_mgmt = true;
}

static inline void RelayController_setPowerManagement(bool state) {
    _stub_power_mgmt = state;
}

static inline bool RelayController_getPowerManagement() {
    return _stub_power_mgmt;
}

static inline void stub_relay_reset() {
    RelayController_init();
}

#endif // RELAY_CONTROLLER_H
