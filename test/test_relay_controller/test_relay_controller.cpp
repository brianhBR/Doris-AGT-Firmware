#include <unity.h>
#include "Arduino.h"

#define NO_RELAYS
// This suite exercises the real relay controller, not the stub in test/stubs
// that shadows it for every other suite. Including the real header first claims
// the shared guard, so the stub is empty by the time the .cpp pulls it in.
#include "../../include/modules/relay_controller.h"
#include "../../src/modules/relay_controller.cpp"

void setUp(void) {
    RelayController_init();
}

void tearDown(void) {}

void test_init_restores_payload_power(void) {
    TEST_ASSERT_TRUE(RelayController_getPowerManagement());
}

void test_payload_power_state_tracks_commands(void) {
    RelayController_setPowerManagement(false);
    TEST_ASSERT_FALSE(RelayController_getPowerManagement());
    RelayController_setPowerManagement(true);
    TEST_ASSERT_TRUE(RelayController_getPowerManagement());
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_init_restores_payload_power);
    RUN_TEST(test_payload_power_state_tracks_commands);
    return UNITY_END();
}
