#include <unity.h>
#include "Arduino.h"
#include "config.h"

// Claim the real header's guard before src includes "modules/relay_controller.h",
// which would otherwise hit the stub earlier on the native include path.
#include "../../include/modules/relay_controller.h"

enum { GPIO_LOG_MAX = 48 };

struct GpioEvent {
    char kind;
    uint8_t pin;
    unsigned long value;
};

static GpioEvent gpioLog[GPIO_LOG_MAX];
static int gpioCount = 0;
static bool gpioOverflow = false;
static uint8_t pinLevel[64];
static bool pinKnown[64];

extern "C" void testRelayDigitalWrite(uint8_t pin, uint8_t val) {
    if (gpioCount < GPIO_LOG_MAX) {
        gpioLog[gpioCount].kind = 'W';
        gpioLog[gpioCount].pin = pin;
        gpioLog[gpioCount].value = val;
        gpioCount++;
    } else {
        gpioOverflow = true;
    }
    if (pin < 64) {
        pinLevel[pin] = val;
        pinKnown[pin] = true;
    }
}

extern "C" void testRelayPinMode(uint8_t pin, uint8_t mode) {
    if (gpioCount < GPIO_LOG_MAX) {
        gpioLog[gpioCount].kind = 'M';
        gpioLog[gpioCount].pin = pin;
        gpioLog[gpioCount].value = mode;
        gpioCount++;
    } else {
        gpioOverflow = true;
    }
}

extern "C" void testRelayDelay(unsigned long ms) {
    if (gpioCount < GPIO_LOG_MAX) {
        gpioLog[gpioCount].kind = 'D';
        gpioLog[gpioCount].pin = 0;
        gpioLog[gpioCount].value = ms;
        gpioCount++;
    } else {
        gpioOverflow = true;
    }
}

static void clearLog(void) {
    gpioCount = 0;
    gpioOverflow = false;
}

static int countWrites(uint8_t pin, unsigned long level) {
    int n = 0;
    for (int i = 0; i < gpioCount; ++i) {
        if (gpioLog[i].kind == 'W' && gpioLog[i].pin == pin &&
            gpioLog[i].value == level) {
            n++;
        }
    }
    return n;
}

static int countKind(char kind) {
    int n = 0;
    for (int i = 0; i < gpioCount; ++i) {
        if (gpioLog[i].kind == kind) {
            n++;
        }
    }
    return n;
}

static int firstIndex(char kind, uint8_t pin, unsigned long value) {
    for (int i = 0; i < gpioCount; ++i) {
        if (gpioLog[i].kind == kind && gpioLog[i].pin == pin &&
            gpioLog[i].value == value) {
            return i;
        }
    }
    return -1;
}

// One HIGH on pin, then PAYLOAD_POWER_PULSE_MS, then LOW. No other pin goes HIGH.
static void assertSinglePulse(uint8_t pin) {
    int highAt = -1;
    int delayAt = -1;
    int lowAfter = -1;
    TEST_ASSERT_FALSE(gpioOverflow);
    for (int i = 0; i < gpioCount; ++i) {
        if (gpioLog[i].kind == 'W' && gpioLog[i].value == HIGH) {
            TEST_ASSERT_EQUAL_UINT8(pin, gpioLog[i].pin);
            TEST_ASSERT_EQUAL_INT(-1, highAt);
            highAt = i;
        }
        if (gpioLog[i].kind == 'D') {
            TEST_ASSERT_EQUAL_INT(-1, delayAt);
            TEST_ASSERT_EQUAL_UINT32(PAYLOAD_POWER_PULSE_MS, gpioLog[i].value);
            delayAt = i;
        }
        if (gpioLog[i].kind == 'W' && gpioLog[i].pin == pin &&
            gpioLog[i].value == LOW && highAt >= 0 && lowAfter < 0) {
            lowAfter = i;
        }
    }
    TEST_ASSERT_TRUE(highAt >= 0);
    TEST_ASSERT_TRUE(delayAt > highAt);
    TEST_ASSERT_TRUE(lowAfter > delayAt);
}

static void assertNeverLeftHigh(void) {
    bool onHigh = false;
    bool offHigh = false;
    for (int i = 0; i < gpioCount; ++i) {
        if (gpioLog[i].kind != 'W') {
            continue;
        }
        bool high = gpioLog[i].value == HIGH;
        if (gpioLog[i].pin == PAYLOAD_POWER_ON_PIN) {
            onHigh = high;
        } else if (gpioLog[i].pin == PAYLOAD_POWER_OFF_PIN) {
            offHigh = high;
        }
        TEST_ASSERT_FALSE(onHigh && offHigh);
    }
    TEST_ASSERT_FALSE(onHigh);
    TEST_ASSERT_FALSE(offHigh);
    TEST_ASSERT_TRUE(pinKnown[PAYLOAD_POWER_ON_PIN]);
    TEST_ASSERT_TRUE(pinKnown[PAYLOAD_POWER_OFF_PIN]);
    TEST_ASSERT_EQUAL_UINT8(LOW, pinLevel[PAYLOAD_POWER_ON_PIN]);
    TEST_ASSERT_EQUAL_UINT8(LOW, pinLevel[PAYLOAD_POWER_OFF_PIN]);
}

#define RELAY_NS relay_no_relays
#define NO_RELAYS
#include "../../src/modules/relay_controller.cpp"
#undef NO_RELAYS
#undef RELAY_NS

#define RELAY_NS relay_legacy
#include "../../src/modules/relay_controller.cpp"
#undef RELAY_NS

#define RELAY_NS relay_pololu
#define PAYLOAD_POWER_POLULU
#include "../../src/modules/relay_controller.cpp"
#undef PAYLOAD_POWER_POLULU
#undef RELAY_NS

void setUp(void) {
    clearLog();
    for (int i = 0; i < 64; ++i) {
        pinKnown[i] = false;
        pinLevel[i] = LOW;
    }
    stub_digital_write_hook = testRelayDigitalWrite;
    stub_pin_mode_hook = testRelayPinMode;
    stub_delay_hook = testRelayDelay;
}

void tearDown(void) {
    stub_digital_write_hook = 0;
    stub_pin_mode_hook = 0;
    stub_delay_hook = 0;
}

void test_no_relays_tracks_state_without_driving_pins(void) {
    relay_no_relays::RelayController_init();
    TEST_ASSERT_TRUE(relay_no_relays::RelayController_getPowerManagement());
    relay_no_relays::RelayController_setPowerManagement(false);
    TEST_ASSERT_FALSE(relay_no_relays::RelayController_getPowerManagement());
    relay_no_relays::RelayController_setPowerManagement(true);
    TEST_ASSERT_TRUE(relay_no_relays::RelayController_getPowerManagement());
    TEST_ASSERT_EQUAL_INT(0, gpioCount);
}

void test_legacy_init_conducts_and_holds_low(void) {
    relay_legacy::RelayController_init();
    TEST_ASSERT_TRUE(relay_legacy::RelayController_getPowerManagement());
    TEST_ASSERT_EQUAL_INT(0, countKind('D'));
    TEST_ASSERT_EQUAL_INT(0, countWrites(RELAY_POWER_MGMT, HIGH));
    TEST_ASSERT_EQUAL_UINT8(LOW, pinLevel[RELAY_POWER_MGMT]);
    int modeAt = firstIndex('M', RELAY_POWER_MGMT, OUTPUT);
    int lowAt = firstIndex('W', RELAY_POWER_MGMT, LOW);
    TEST_ASSERT_TRUE(modeAt >= 0);
    TEST_ASSERT_TRUE(lowAt > modeAt);
}

void test_legacy_off_holds_coil_high(void) {
    relay_legacy::RelayController_init();
    clearLog();
    relay_legacy::RelayController_setPowerManagement(false);
    TEST_ASSERT_FALSE(relay_legacy::RelayController_getPowerManagement());
    TEST_ASSERT_EQUAL_INT(1, countWrites(RELAY_POWER_MGMT, HIGH));
    TEST_ASSERT_EQUAL_INT(0, countKind('D'));
    TEST_ASSERT_EQUAL_UINT8(HIGH, pinLevel[RELAY_POWER_MGMT]);

    clearLog();
    relay_legacy::RelayController_setPowerManagement(false);
    TEST_ASSERT_EQUAL_INT(1, countWrites(RELAY_POWER_MGMT, HIGH));
    TEST_ASSERT_EQUAL_UINT8(HIGH, pinLevel[RELAY_POWER_MGMT]);
}

void test_legacy_on_holds_coil_low(void) {
    relay_legacy::RelayController_init();
    relay_legacy::RelayController_setPowerManagement(false);
    clearLog();
    relay_legacy::RelayController_setPowerManagement(true);
    TEST_ASSERT_TRUE(relay_legacy::RelayController_getPowerManagement());
    TEST_ASSERT_EQUAL_INT(1, countWrites(RELAY_POWER_MGMT, LOW));
    TEST_ASSERT_EQUAL_INT(0, countWrites(RELAY_POWER_MGMT, HIGH));
    TEST_ASSERT_EQUAL_INT(0, countKind('D'));
    TEST_ASSERT_EQUAL_UINT8(LOW, pinLevel[RELAY_POWER_MGMT]);
}

void test_pololu_init_pulses_on_without_off(void) {
    TEST_ASSERT_EQUAL_UINT32(100, PAYLOAD_POWER_PULSE_MS);
    relay_pololu::RelayController_init();
    TEST_ASSERT_TRUE(relay_pololu::RelayController_getPowerManagement());

    int onLow = firstIndex('W', PAYLOAD_POWER_ON_PIN, LOW);
    int offLow = firstIndex('W', PAYLOAD_POWER_OFF_PIN, LOW);
    int onMode = firstIndex('M', PAYLOAD_POWER_ON_PIN, OUTPUT);
    int offMode = firstIndex('M', PAYLOAD_POWER_OFF_PIN, OUTPUT);
    TEST_ASSERT_TRUE(onLow >= 0);
    TEST_ASSERT_TRUE(offLow >= 0);
    TEST_ASSERT_TRUE(onLow < onMode);
    TEST_ASSERT_TRUE(offLow < onMode);
    TEST_ASSERT_TRUE(onLow < offMode);
    TEST_ASSERT_TRUE(offLow < offMode);
    TEST_ASSERT_EQUAL_INT(0, countWrites(PAYLOAD_POWER_OFF_PIN, HIGH));
    TEST_ASSERT_EQUAL_INT(1, countWrites(PAYLOAD_POWER_ON_PIN, HIGH));
    assertSinglePulse(PAYLOAD_POWER_ON_PIN);
    assertNeverLeftHigh();
}

void test_pololu_off_emits_only_off_pulse(void) {
    relay_pololu::RelayController_init();
    clearLog();
    relay_pololu::RelayController_setPowerManagement(false);
    TEST_ASSERT_FALSE(relay_pololu::RelayController_getPowerManagement());
    TEST_ASSERT_EQUAL_INT(0, countWrites(PAYLOAD_POWER_ON_PIN, HIGH));
    TEST_ASSERT_EQUAL_INT(0, countWrites(PAYLOAD_POWER_ON_PIN, LOW));
    assertSinglePulse(PAYLOAD_POWER_OFF_PIN);
    assertNeverLeftHigh();
}

void test_pololu_on_emits_only_on_pulse(void) {
    relay_pololu::RelayController_init();
    relay_pololu::RelayController_setPowerManagement(false);
    clearLog();
    relay_pololu::RelayController_setPowerManagement(true);
    TEST_ASSERT_TRUE(relay_pololu::RelayController_getPowerManagement());
    TEST_ASSERT_EQUAL_INT(0, countWrites(PAYLOAD_POWER_OFF_PIN, HIGH));
    TEST_ASSERT_EQUAL_INT(0, countWrites(PAYLOAD_POWER_OFF_PIN, LOW));
    assertSinglePulse(PAYLOAD_POWER_ON_PIN);
    assertNeverLeftHigh();
}

void test_pololu_repeated_requests_do_not_pulse(void) {
    relay_pololu::RelayController_init();
    clearLog();
    relay_pololu::RelayController_setPowerManagement(true);
    TEST_ASSERT_EQUAL_INT(0, gpioCount);
    TEST_ASSERT_TRUE(relay_pololu::RelayController_getPowerManagement());

    relay_pololu::RelayController_setPowerManagement(false);
    clearLog();
    relay_pololu::RelayController_setPowerManagement(false);
    TEST_ASSERT_EQUAL_INT(0, gpioCount);
    TEST_ASSERT_FALSE(relay_pololu::RelayController_getPowerManagement());
    assertNeverLeftHigh();
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_no_relays_tracks_state_without_driving_pins);
    RUN_TEST(test_legacy_init_conducts_and_holds_low);
    RUN_TEST(test_legacy_off_holds_coil_high);
    RUN_TEST(test_legacy_on_holds_coil_low);
    RUN_TEST(test_pololu_init_pulses_on_without_off);
    RUN_TEST(test_pololu_off_emits_only_off_pulse);
    RUN_TEST(test_pololu_on_emits_only_on_pulse);
    RUN_TEST(test_pololu_repeated_requests_do_not_pulse);
    return UNITY_END();
}
