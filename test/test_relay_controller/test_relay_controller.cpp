#include <unity.h>
#include <limits.h>
#include <stddef.h>
#include "Arduino.h"
#include "EEPROM.h"

#define NO_RELAYS
#include "../../src/modules/relay_controller.cpp"

void setUp(void) {
    EEPROM.clear();
    stub_set_millis(100);
    RelayController_init();
}

void tearDown(void) {}

void test_release_on_latches_and_repeated_on_is_safe(void) {
    RelayController_requestRelease();
    RelayController_requestRelease();
    TEST_ASSERT_TRUE(RelayController_isReleaseActive());
}

void test_release_off_requires_hold_and_surface_guard(void) {
    RelayController_requestRelease();
    TEST_ASSERT_FALSE(RelayController_requestReleaseOff(false));
    stub_advance_millis(RELEASE_MIN_HOLD_SEC * 1000UL);
    TEST_ASSERT_FALSE(RelayController_requestReleaseOff(false));
    TEST_ASSERT_TRUE(RelayController_requestReleaseOff(true));
    TEST_ASSERT_FALSE(RelayController_isReleaseActive());
}

void test_release_active_marker_survives_reinitialization(void) {
    RelayController_requestRelease();
    RelayController_init();
    TEST_ASSERT_TRUE(RelayController_isReleaseActive());
}

void test_release_record_is_bounded_and_corruption_fails_off(void) {
    TEST_ASSERT_TRUE(RELEASE_EEPROM_ADDRESS >= (int)sizeof(SystemConfig));
    TEST_ASSERT_TRUE((size_t)RELEASE_EEPROM_ADDRESS + sizeof(ReleaseRecord) <=
                     EEPROM.length());
    RelayController_requestRelease();
    EEPROM.write(RELEASE_EEPROM_ADDRESS + offsetof(ReleaseRecord, check), 0);
    RelayController_init();
    TEST_ASSERT_FALSE(RelayController_isReleaseActive());
}

void test_release_minimum_hold_handles_millis_rollover(void) {
    stub_set_millis(ULONG_MAX - (RELEASE_MIN_HOLD_SEC * 500UL));
    RelayController_requestRelease();
    stub_advance_millis(RELEASE_MIN_HOLD_SEC * 1000UL);
    TEST_ASSERT_TRUE(RelayController_requestReleaseOff(true));
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_release_on_latches_and_repeated_on_is_safe);
    RUN_TEST(test_release_off_requires_hold_and_surface_guard);
    RUN_TEST(test_release_active_marker_survives_reinitialization);
    RUN_TEST(test_release_record_is_bounded_and_corruption_fails_off);
    RUN_TEST(test_release_minimum_hold_handles_millis_rollover);
    return UNITY_END();
}
