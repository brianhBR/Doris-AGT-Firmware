#include <unity.h>

#include "modules/iridium_message.h"

#include "../../src/modules/iridium_message.cpp"

static IridiumProtocolBFields fields;

void test_formats_exact_located_protocol_b_payload() {
    fields.gpsValid = true;
    fields.latitudeDegrees = 33.12345;
    fields.longitudeDegrees = -118.12345;
    fields.groundSpeedMetersPerSecond = 0.74f;
    fields.courseDegrees = 244.6f;
    fields.maximumDepthMeters = 2237.6f;
    fields.batteryVoltage = 14.84f;
    fields.temperatureValid = true;
    fields.minimumTemperatureCelsius = 3.24f;

    char message[96];
    TEST_ASSERT_TRUE(
        IridiumMessage_formatProtocolB(message, sizeof(message), fields));
    TEST_ASSERT_EQUAL_STRING(
        "B,+033.12345,-118.12345,07,245,2238,14.8,3.2,00",
        message);
}

void test_no_fix_uses_zero_navigation_fields() {
    fields.gpsValid = false;
    fields.latitudeDegrees = 33.12345;
    fields.longitudeDegrees = -118.12345;
    fields.groundSpeedMetersPerSecond = 2.0f;
    fields.courseDegrees = 180.0f;
    fields.maximumDepthMeters = 12.0f;
    fields.batteryVoltage = 13.2f;
    fields.temperatureValid = false;

    char message[96];
    TEST_ASSERT_TRUE(
        IridiumMessage_formatProtocolB(message, sizeof(message), fields));
    TEST_ASSERT_EQUAL_STRING(
        "B,+000.00000,+000.00000,00,000,0012,13.2,0.0,00",
        message);
}

void test_values_are_rounded_normalized_and_clamped() {
    fields.gpsValid = true;
    fields.latitudeDegrees = 90.1;
    fields.longitudeDegrees = -181.0;
    fields.groundSpeedMetersPerSecond = 12.0f;
    fields.courseDegrees = 359.6f;
    fields.maximumDepthMeters = 12000.0f;
    fields.batteryVoltage = 120.0f;
    fields.temperatureValid = true;
    fields.minimumTemperatureCelsius = -120.0f;

    char message[96];
    TEST_ASSERT_TRUE(
        IridiumMessage_formatProtocolB(message, sizeof(message), fields));
    TEST_ASSERT_EQUAL_STRING(
        "B,+090.00000,-180.00000,99,000,9999,99.9,-99.9,00",
        message);
}

void test_negative_course_wraps_to_positive_heading() {
    fields.gpsValid = true;
    fields.courseDegrees = -1.0f;

    char message[96];
    TEST_ASSERT_TRUE(
        IridiumMessage_formatProtocolB(message, sizeof(message), fields));
    TEST_ASSERT_EQUAL_STRING(
        "B,+000.00000,+000.00000,00,359,0000,0.0,0.0,00",
        message);
}

void test_rejects_missing_or_small_destination() {
    char message[8];
    TEST_ASSERT_FALSE(
        IridiumMessage_formatProtocolB(nullptr, 0, fields));
    TEST_ASSERT_FALSE(
        IridiumMessage_formatProtocolB(message, sizeof(message), fields));
}

void setUp() {
    fields = {};
}

void tearDown() {}

int main(int argc, char** argv) {
    UNITY_BEGIN();

    RUN_TEST(test_formats_exact_located_protocol_b_payload);
    RUN_TEST(test_no_fix_uses_zero_navigation_fields);
    RUN_TEST(test_values_are_rounded_normalized_and_clamped);
    RUN_TEST(test_negative_course_wraps_to_positive_heading);
    RUN_TEST(test_rejects_missing_or_small_destination);

    return UNITY_END();
}
