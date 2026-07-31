// The NAMED_VALUE_FLOAT name field is a fixed char[10] and MAVLink's packer
// copies all ten bytes from whatever pointer it is given. Passing a shorter
// literal reads past its end, so the bytes after the terminator carry whatever
// the linker happened to place next. That shipped: the AGT was observed
// advertising "AGT_CAP\0RE", "REL_STAT\0P" and "PWR_SHDN\0v", and the BlueOS
// extension — which strips NULs rather than truncating at one — read those as
// unknown names and ignored them, leaving it with no AGT capability
// advertisement and no release status.

#include <unity.h>
#include <string.h>
#include "Arduino.h"

#include "config.h"
#include "mavlink_name_field.h"

static const int LEN = MAVLINK_NAME_FIELD_LEN;

// How a reader that stops at the terminator sees the field.
static const char* asCString(const char field[LEN], char scratch[LEN + 1]) {
    memcpy(scratch, field, LEN);
    scratch[LEN] = '\0';
    return scratch;
}

// How a reader that strips NULs sees it — the extension's decoder, and the
// thing that actually broke.
static void asNulStripped(const char field[LEN], char out[LEN + 1]) {
    int n = 0;
    for (int i = 0; i < LEN; i++) {
        if (field[i] != '\0') out[n++] = field[i];
    }
    out[n] = '\0';
}

void test_short_name_is_zero_padded_to_the_full_field(void) {
    char field[LEN];
    memset(field, 0x5A, sizeof(field));  // poison, as an uninitialised field would be
    mavlinkNameField(field, "AGT_CAP");

    TEST_ASSERT_EQUAL_STRING_LEN("AGT_CAP", field, 7);
    for (int i = 7; i < LEN; i++) {
        TEST_ASSERT_EQUAL_HEX8(0x00, field[i]);
    }
}

void test_nothing_survives_after_the_terminator(void) {
    // Exactly the bytes seen on the wire: the name, its terminator, then the
    // start of the next literal in .rodata.
    const char bled[LEN] = {'A', 'G', 'T', '_', 'C', 'A', 'P', '\0', 'R', 'E'};
    char stripped[LEN + 1];
    asNulStripped(bled, stripped);
    TEST_ASSERT_EQUAL_STRING("AGT_CAPRE", stripped);  // the observed failure

    char field[LEN];
    mavlinkNameField(field, bled);
    asNulStripped(field, stripped);
    TEST_ASSERT_EQUAL_STRING("AGT_CAP", stripped);
}

void test_both_readings_agree_for_every_protocol_name(void) {
    const char* names[] = {
        MAVLINK_NAME_AGT_CAPABILITY,
        MAVLINK_NAME_RELEASE_STATUS,
        MAVLINK_NAME_POWER_REQUEST,
        MAVLINK_NAME_POWER_ACK,
        MAVLINK_NAME_RELEASE_COMMAND,
    };
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        char field[LEN];
        char scratch[LEN + 1];
        char stripped[LEN + 1];
        mavlinkNameField(field, names[i]);

        TEST_ASSERT_TRUE(strlen(names[i]) <= (size_t)LEN);
        TEST_ASSERT_EQUAL_STRING(names[i], asCString(field, scratch));
        asNulStripped(field, stripped);
        TEST_ASSERT_EQUAL_STRING(names[i], stripped);
    }
}

void test_a_ten_character_name_fills_the_field_without_a_terminator(void) {
    char field[LEN];
    mavlinkNameField(field, "ABCDEFGHIJ");
    TEST_ASSERT_EQUAL_STRING_LEN("ABCDEFGHIJ", field, LEN);
}

void test_a_name_longer_than_the_field_is_truncated_not_overrun(void) {
    char field[LEN];
    mavlinkNameField(field, "ABCDEFGHIJKLMNOP");
    TEST_ASSERT_EQUAL_STRING_LEN("ABCDEFGHIJ", field, LEN);
}

void test_a_null_name_yields_an_empty_field(void) {
    char field[LEN];
    memset(field, 0x5A, sizeof(field));
    mavlinkNameField(field, NULL);
    for (int i = 0; i < LEN; i++) {
        TEST_ASSERT_EQUAL_HEX8(0x00, field[i]);
    }
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_short_name_is_zero_padded_to_the_full_field);
    RUN_TEST(test_nothing_survives_after_the_terminator);
    RUN_TEST(test_both_readings_agree_for_every_protocol_name);
    RUN_TEST(test_a_ten_character_name_fills_the_field_without_a_terminator);
    RUN_TEST(test_a_name_longer_than_the_field_is_truncated_not_overrun);
    RUN_TEST(test_a_null_name_yields_an_empty_field);
    return UNITY_END();
}

void setUp(void) {}
void tearDown(void) {}
