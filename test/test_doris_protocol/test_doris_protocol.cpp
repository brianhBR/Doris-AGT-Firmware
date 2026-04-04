#include <unity.h>
#include <string.h>
#include "modules/doris_protocol.h"

// ---------------------------------------------------------------------------
// Report serialization
// ---------------------------------------------------------------------------

void test_serialize_report_writes_header(void) {
    DorisReport report;
    memset(&report, 0, sizeof(report));
    uint8_t buf[64];

    size_t n = DorisProtocol_serializeReport(&report, buf, sizeof(buf));

    TEST_ASSERT_EQUAL(DORIS_REPORT_WIRE_SIZE, n);
    TEST_ASSERT_EQUAL_UINT8('$', buf[0]);
    TEST_ASSERT_EQUAL_UINT8(DORIS_MSG_ID_REPORT, buf[1]);
}

void test_serialize_report_payload_matches_struct(void) {
    DorisReport report;
    memset(&report, 0, sizeof(report));
    report.mission_state = DORIS_STATE_DIVING;
    report.latitude = 47.6062f;
    report.longitude = -122.3321f;
    report.battery_voltage = 12500;
    report.depth = 150;  // 15.0m in decimeters
    report.uptime_s = 3600;

    uint8_t buf[64];
    size_t n = DorisProtocol_serializeReport(&report, buf, sizeof(buf));
    TEST_ASSERT_EQUAL(DORIS_REPORT_WIRE_SIZE, n);

    DorisReport decoded;
    memcpy(&decoded, buf + DORIS_MSG_HEADER_SIZE, sizeof(DorisReport));
    TEST_ASSERT_EQUAL_UINT8(DORIS_STATE_DIVING, decoded.mission_state);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 47.6062f, decoded.latitude);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, -122.3321f, decoded.longitude);
    TEST_ASSERT_EQUAL_UINT16(12500, decoded.battery_voltage);
    TEST_ASSERT_EQUAL_UINT16(150, decoded.depth);
    TEST_ASSERT_EQUAL_UINT32(3600, decoded.uptime_s);
}

void test_serialize_report_fails_on_short_buffer(void) {
    DorisReport report;
    memset(&report, 0, sizeof(report));
    uint8_t buf[4];

    size_t n = DorisProtocol_serializeReport(&report, buf, sizeof(buf));
    TEST_ASSERT_EQUAL(0, n);
}

// ---------------------------------------------------------------------------
// ACK serialization
// ---------------------------------------------------------------------------

void test_serialize_ack(void) {
    DorisAck ack;
    ack.acked_msg_id = DORIS_MSG_ID_CONFIG;
    ack.result = 0;

    uint8_t buf[16];
    size_t n = DorisProtocol_serializeAck(&ack, buf, sizeof(buf));

    TEST_ASSERT_EQUAL(DORIS_ACK_WIRE_SIZE, n);
    TEST_ASSERT_EQUAL_UINT8('$', buf[0]);
    TEST_ASSERT_EQUAL_UINT8(DORIS_MSG_ID_ACK, buf[1]);

    DorisAck decoded;
    memcpy(&decoded, buf + DORIS_MSG_HEADER_SIZE, sizeof(DorisAck));
    TEST_ASSERT_EQUAL_UINT8(DORIS_MSG_ID_CONFIG, decoded.acked_msg_id);
    TEST_ASSERT_EQUAL_UINT8(0, decoded.result);
}

void test_serialize_ack_fails_on_short_buffer(void) {
    DorisAck ack = {DORIS_MSG_ID_CONFIG, 0};
    uint8_t buf[2];
    TEST_ASSERT_EQUAL(0, DorisProtocol_serializeAck(&ack, buf, sizeof(buf)));
}

// ---------------------------------------------------------------------------
// MT parsing — Config (ID 6)
// ---------------------------------------------------------------------------

void test_parse_mt_config(void) {
    DorisConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.iridium_interval_s = 300;
    cfg.led_mode = DORIS_LED_STROBE;
    cfg.neopixel_brightness = 128;

    uint8_t wire[32];
    wire[0] = DORIS_MSG_START;
    wire[1] = DORIS_MSG_ID_CONFIG;
    memcpy(wire + DORIS_MSG_HEADER_SIZE, &cfg, sizeof(DorisConfig));

    DorisConfig parsed;
    DorisCommand cmd;
    uint8_t id = DorisProtocol_parseMT(wire, DORIS_CONFIG_WIRE_SIZE, &parsed, &cmd);

    TEST_ASSERT_EQUAL_UINT8(DORIS_MSG_ID_CONFIG, id);
    TEST_ASSERT_EQUAL_UINT16(300, parsed.iridium_interval_s);
    TEST_ASSERT_EQUAL_UINT8(DORIS_LED_STROBE, parsed.led_mode);
    TEST_ASSERT_EQUAL_UINT8(128, parsed.neopixel_brightness);
}

// ---------------------------------------------------------------------------
// MT parsing — Command (ID 7)
// ---------------------------------------------------------------------------

void test_parse_mt_command(void) {
    DorisCommand cmd;
    cmd.command = DORIS_CMD_RELEASE;
    cmd.param = 42;

    uint8_t wire[32];
    wire[0] = DORIS_MSG_START;
    wire[1] = DORIS_MSG_ID_COMMAND;
    memcpy(wire + DORIS_MSG_HEADER_SIZE, &cmd, sizeof(DorisCommand));

    DorisConfig cfg;
    DorisCommand parsed;
    uint8_t id = DorisProtocol_parseMT(wire, DORIS_COMMAND_WIRE_SIZE, &cfg, &parsed);

    TEST_ASSERT_EQUAL_UINT8(DORIS_MSG_ID_COMMAND, id);
    TEST_ASSERT_EQUAL_UINT8(DORIS_CMD_RELEASE, parsed.command);
    TEST_ASSERT_EQUAL_UINT32(42, parsed.param);
}

// ---------------------------------------------------------------------------
// MT parsing — edge cases
// ---------------------------------------------------------------------------

void test_parse_mt_rejects_short_data(void) {
    uint8_t wire[1] = {'$'};
    DorisConfig cfg;
    DorisCommand cmd;
    TEST_ASSERT_EQUAL_UINT8(0, DorisProtocol_parseMT(wire, 1, &cfg, &cmd));
}

void test_parse_mt_rejects_bad_start_marker(void) {
    uint8_t wire[16];
    memset(wire, 0, sizeof(wire));
    wire[0] = 'X';
    wire[1] = DORIS_MSG_ID_CONFIG;

    DorisConfig cfg;
    DorisCommand cmd;
    TEST_ASSERT_EQUAL_UINT8(0, DorisProtocol_parseMT(wire, sizeof(wire), &cfg, &cmd));
}

void test_parse_mt_rejects_unknown_msg_id(void) {
    uint8_t wire[16];
    memset(wire, 0, sizeof(wire));
    wire[0] = DORIS_MSG_START;
    wire[1] = 99;

    DorisConfig cfg;
    DorisCommand cmd;
    TEST_ASSERT_EQUAL_UINT8(0, DorisProtocol_parseMT(wire, sizeof(wire), &cfg, &cmd));
}

void test_parse_mt_rejects_truncated_config(void) {
    uint8_t wire[4];
    wire[0] = DORIS_MSG_START;
    wire[1] = DORIS_MSG_ID_CONFIG;
    wire[2] = 0;
    wire[3] = 0;

    DorisConfig cfg;
    DorisCommand cmd;
    TEST_ASSERT_EQUAL_UINT8(0, DorisProtocol_parseMT(wire, sizeof(wire), &cfg, &cmd));
}

void test_parse_mt_handles_null_output_pointers(void) {
    uint8_t wire[32];
    memset(wire, 0, sizeof(wire));
    wire[0] = DORIS_MSG_START;
    wire[1] = DORIS_MSG_ID_CONFIG;

    // Should not crash with NULL output pointers
    uint8_t id = DorisProtocol_parseMT(wire, DORIS_CONFIG_WIRE_SIZE, NULL, NULL);
    TEST_ASSERT_EQUAL_UINT8(DORIS_MSG_ID_CONFIG, id);
}

// ---------------------------------------------------------------------------
// Wire sizes are consistent with struct packing
// ---------------------------------------------------------------------------

void test_report_wire_size_matches(void) {
    TEST_ASSERT_EQUAL(sizeof(DorisReport) + 2, DORIS_REPORT_WIRE_SIZE);
}

void test_config_wire_size_matches(void) {
    TEST_ASSERT_EQUAL(sizeof(DorisConfig) + 2, DORIS_CONFIG_WIRE_SIZE);
}

void test_command_wire_size_matches(void) {
    TEST_ASSERT_EQUAL(sizeof(DorisCommand) + 2, DORIS_COMMAND_WIRE_SIZE);
}

void test_ack_wire_size_matches(void) {
    TEST_ASSERT_EQUAL(sizeof(DorisAck) + 2, DORIS_ACK_WIRE_SIZE);
}

void test_report_fits_iridium_mo_limit(void) {
    // Iridium MO max is 340 bytes
    TEST_ASSERT_TRUE(DORIS_REPORT_WIRE_SIZE <= 340);
}

void test_config_fits_iridium_mt_limit(void) {
    // Iridium MT max is 270 bytes
    TEST_ASSERT_TRUE(DORIS_CONFIG_WIRE_SIZE <= 270);
}

void test_command_fits_iridium_mt_limit(void) {
    TEST_ASSERT_TRUE(DORIS_COMMAND_WIRE_SIZE <= 270);
}

// ---------------------------------------------------------------------------
// Round-trip: serialize report, then parse back the payload
// ---------------------------------------------------------------------------

void test_report_round_trip(void) {
    DorisReport original;
    memset(&original, 0, sizeof(original));
    original.mission_state = DORIS_STATE_RECOVERY;
    original.gps_fix_type = 3;
    original.satellites = 12;
    original.failsafe_flags = DORIS_FAILSAFE_LEAK | DORIS_FAILSAFE_LOW_VOLTAGE;
    original.latitude = -33.8688f;
    original.longitude = 151.2093f;
    original.altitude = -50;
    original.speed = 125;
    original.course = 18000;
    original.hdop = 95;
    original.depth = 1234;
    original.max_depth = 2000;
    original.battery_voltage = 11800;
    original.battery_current = 500;
    original.bus_voltage = 5100;
    original.leak_detected = 1;
    original.time_unix = 1700000000;
    original.uptime_s = 7200;

    uint8_t buf[64];
    size_t n = DorisProtocol_serializeReport(&original, buf, sizeof(buf));
    TEST_ASSERT_GREATER_THAN(0, n);

    // Decode payload portion
    DorisReport decoded;
    memcpy(&decoded, buf + DORIS_MSG_HEADER_SIZE, sizeof(DorisReport));

    TEST_ASSERT_EQUAL_MEMORY(&original, &decoded, sizeof(DorisReport));
}

// ---------------------------------------------------------------------------
// Failsafe flag bitfield tests
// ---------------------------------------------------------------------------

void test_failsafe_flags_are_independent_bits(void) {
    uint8_t flags = 0;
    flags |= DORIS_FAILSAFE_LOW_VOLTAGE;
    flags |= DORIS_FAILSAFE_LEAK;
    flags |= DORIS_FAILSAFE_NO_HEARTBEAT;

    TEST_ASSERT_TRUE(flags & DORIS_FAILSAFE_LOW_VOLTAGE);
    TEST_ASSERT_TRUE(flags & DORIS_FAILSAFE_LEAK);
    TEST_ASSERT_TRUE(flags & DORIS_FAILSAFE_NO_HEARTBEAT);
    TEST_ASSERT_FALSE(flags & DORIS_FAILSAFE_MANUAL);
    TEST_ASSERT_FALSE(flags & DORIS_FAILSAFE_CRITICAL_VOLTAGE);
}

// ---------------------------------------------------------------------------
// Unity entry point
// ---------------------------------------------------------------------------

void setUp(void) {}
void tearDown(void) {}

int main(int argc, char** argv) {
    UNITY_BEGIN();

    // Serialization
    RUN_TEST(test_serialize_report_writes_header);
    RUN_TEST(test_serialize_report_payload_matches_struct);
    RUN_TEST(test_serialize_report_fails_on_short_buffer);
    RUN_TEST(test_serialize_ack);
    RUN_TEST(test_serialize_ack_fails_on_short_buffer);

    // MT parsing
    RUN_TEST(test_parse_mt_config);
    RUN_TEST(test_parse_mt_command);
    RUN_TEST(test_parse_mt_rejects_short_data);
    RUN_TEST(test_parse_mt_rejects_bad_start_marker);
    RUN_TEST(test_parse_mt_rejects_unknown_msg_id);
    RUN_TEST(test_parse_mt_rejects_truncated_config);
    RUN_TEST(test_parse_mt_handles_null_output_pointers);

    // Wire sizes
    RUN_TEST(test_report_wire_size_matches);
    RUN_TEST(test_config_wire_size_matches);
    RUN_TEST(test_command_wire_size_matches);
    RUN_TEST(test_ack_wire_size_matches);
    RUN_TEST(test_report_fits_iridium_mo_limit);
    RUN_TEST(test_config_fits_iridium_mt_limit);
    RUN_TEST(test_command_fits_iridium_mt_limit);

    // Round-trip
    RUN_TEST(test_report_round_trip);

    // Bitfields
    RUN_TEST(test_failsafe_flags_are_independent_bits);

    return UNITY_END();
}
