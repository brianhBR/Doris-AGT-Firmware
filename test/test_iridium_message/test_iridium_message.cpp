#include <unity.h>

#include "modules/iridium_message.h"

#include "../../src/modules/iridium_message.cpp"

static IridiumP1Fields fields;

static const uint8_t CANONICAL_EXAMPLE[IRIDIUM_P1_PAYLOAD_SIZE] = {
    'P', ',', '1', ',',
    '+', '0', '2', '1', '.', '4', '3', '2', '5', '5', ',',
    '-', '1', '5', '7', '.', '7', '8', '9', '3', '3', ',',
    '1', '2', ',',
    '0', '4', '5', ',',
    '0', '0', '2', '8', ',',
    '1', '4', '.', '7', ',',
    0x00, 0x00
};

void test_formats_canonical_p1_example() {
    fields.gpsValid = true;
    fields.latitudeDegrees = 21.43255;
    fields.longitudeDegrees = -157.78933;
    fields.groundSpeedMetersPerSecond = 1.2f;
    fields.courseDegrees = 45.0f;
    fields.maximumDepthMeters = 28.0f;
    fields.batteryVoltage = 14.7f;

    uint8_t payload[IRIDIUM_SBD_MO_SIZE];
    TEST_ASSERT_EQUAL(
        IRIDIUM_P1_PAYLOAD_SIZE,
        IridiumMessage_formatP1(payload, sizeof(payload), fields));
    TEST_ASSERT_EQUAL_MEMORY(
        CANONICAL_EXAMPLE, payload, IRIDIUM_P1_PAYLOAD_SIZE);
}

void test_formats_located_payload_widths() {
    fields.gpsValid = true;
    fields.latitudeDegrees = 33.12345;
    fields.longitudeDegrees = -118.12345;
    fields.groundSpeedMetersPerSecond = 0.74f;
    fields.courseDegrees = 244.6f;
    fields.maximumDepthMeters = 2237.6f;
    fields.batteryVoltage = 14.84f;

    uint8_t payload[IRIDIUM_SBD_MO_SIZE];
    TEST_ASSERT_EQUAL(
        IRIDIUM_P1_PAYLOAD_SIZE,
        IridiumMessage_formatP1(payload, sizeof(payload), fields));

    const char* prefix = "P,1,+033.12345,-118.12345,07,245,2238,14.8,";
    TEST_ASSERT_EQUAL_MEMORY(prefix, payload, 43);
    TEST_ASSERT_EQUAL_UINT8(0x00, payload[43]);
    TEST_ASSERT_EQUAL_UINT8(0x00, payload[44]);
}

void test_no_fix_uses_zero_navigation_fields() {
    fields.gpsValid = false;
    fields.latitudeDegrees = 33.12345;
    fields.longitudeDegrees = -118.12345;
    fields.groundSpeedMetersPerSecond = 2.0f;
    fields.courseDegrees = 180.0f;
    fields.maximumDepthMeters = 12.0f;
    fields.batteryVoltage = 13.2f;

    uint8_t payload[IRIDIUM_SBD_MO_SIZE];
    TEST_ASSERT_EQUAL(
        IRIDIUM_P1_PAYLOAD_SIZE,
        IridiumMessage_formatP1(payload, sizeof(payload), fields));

    const char* prefix = "P,1,+000.00000,+000.00000,00,000,0012,13.2,";
    TEST_ASSERT_EQUAL_MEMORY(prefix, payload, 43);
}

void test_values_are_rounded_normalized_and_clamped() {
    fields.gpsValid = true;
    fields.latitudeDegrees = 90.1;
    fields.longitudeDegrees = -181.0;
    fields.groundSpeedMetersPerSecond = 12.0f;
    fields.courseDegrees = 359.6f;
    fields.maximumDepthMeters = 12000.0f;
    fields.batteryVoltage = 120.0f;

    uint8_t payload[IRIDIUM_SBD_MO_SIZE];
    TEST_ASSERT_EQUAL(
        IRIDIUM_P1_PAYLOAD_SIZE,
        IridiumMessage_formatP1(payload, sizeof(payload), fields));

    const char* prefix = "P,1,+090.00000,-180.00000,99,000,9999,99.9,";
    TEST_ASSERT_EQUAL_MEMORY(prefix, payload, 43);
}

void test_negative_course_wraps_to_positive_heading() {
    fields.gpsValid = true;
    fields.courseDegrees = -1.0f;

    uint8_t payload[IRIDIUM_SBD_MO_SIZE];
    TEST_ASSERT_EQUAL(
        IRIDIUM_P1_PAYLOAD_SIZE,
        IridiumMessage_formatP1(payload, sizeof(payload), fields));

    const char* prefix = "P,1,+000.00000,+000.00000,00,359,0000,00.0,";
    TEST_ASSERT_EQUAL_MEMORY(prefix, payload, 43);
}

void test_battery_zero_pads_to_canonical_width() {
    fields.gpsValid = true;
    fields.batteryVoltage = 5.2f;

    uint8_t payload[IRIDIUM_SBD_MO_SIZE];
    TEST_ASSERT_EQUAL(
        IRIDIUM_P1_PAYLOAD_SIZE,
        IridiumMessage_formatP1(payload, sizeof(payload), fields));

    const char* prefix = "P,1,+000.00000,+000.00000,00,000,0000,05.2,";
    TEST_ASSERT_EQUAL_MEMORY(prefix, payload, 43);
}

void test_rejects_missing_or_small_destination() {
    uint8_t payload[IRIDIUM_P1_PAYLOAD_SIZE];
    TEST_ASSERT_EQUAL(0, IridiumMessage_formatP1(nullptr, 0, fields));
    TEST_ASSERT_EQUAL(
        0, IridiumMessage_formatP1(payload, IRIDIUM_P1_PAYLOAD_SIZE - 1, fields));
    TEST_ASSERT_EQUAL(
        IRIDIUM_P1_PAYLOAD_SIZE,
        IridiumMessage_formatP1(payload, sizeof(payload), fields));
}

void setUp() {
    fields = {};
}

void tearDown() {}

int main(int argc, char** argv) {
    UNITY_BEGIN();

    RUN_TEST(test_formats_canonical_p1_example);
    RUN_TEST(test_formats_located_payload_widths);
    RUN_TEST(test_no_fix_uses_zero_navigation_fields);
    RUN_TEST(test_values_are_rounded_normalized_and_clamped);
    RUN_TEST(test_negative_course_wraps_to_positive_heading);
    RUN_TEST(test_battery_zero_pads_to_canonical_width);
    RUN_TEST(test_rejects_missing_or_small_destination);

    return UNITY_END();
}
