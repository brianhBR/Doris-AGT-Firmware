#include <unity.h>
#include "Arduino.h"
#include "EEPROM.h"
#include "config.h"

// The config_manager.cpp references an extern sysConfig — provide one
SystemConfig sysConfig;

#include "../../src/modules/config_manager.cpp"

// ---------------------------------------------------------------------------
// Defaults
// ---------------------------------------------------------------------------

void test_defaults_sets_magic_number(void) {
    SystemConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    ConfigManager_setDefaults(&cfg);
    TEST_ASSERT_EQUAL_HEX32(0xDEADBEEF, cfg.magic);
}

void test_defaults_sets_intervals(void) {
    SystemConfig cfg;
    ConfigManager_setDefaults(&cfg);
    TEST_ASSERT_EQUAL(DEFAULT_IRIDIUM_INTERVAL, cfg.iridiumInterval);
    TEST_ASSERT_EQUAL(DEFAULT_MESHTASTIC_INTERVAL, cfg.meshtasticInterval);
    TEST_ASSERT_EQUAL(DEFAULT_MAVLINK_INTERVAL, cfg.mavlinkInterval);
}

void test_defaults_enables_correct_features(void) {
    SystemConfig cfg;
    ConfigManager_setDefaults(&cfg);
    TEST_ASSERT_TRUE(cfg.enableIridium);
    TEST_ASSERT_TRUE(cfg.enableMeshtastic);
    TEST_ASSERT_TRUE(cfg.enableMAVLink);
    TEST_ASSERT_FALSE(cfg.enablePSM);
    TEST_ASSERT_TRUE(cfg.enableNeoPixels);
}

void test_defaults_timed_event_disabled(void) {
    SystemConfig cfg;
    ConfigManager_setDefaults(&cfg);
    TEST_ASSERT_FALSE(cfg.timedEvent.enabled);
    TEST_ASSERT_EQUAL(1500, cfg.timedEvent.durationSeconds);
}

void test_defaults_power_save_voltage(void) {
    SystemConfig cfg;
    ConfigManager_setDefaults(&cfg);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, DEFAULT_POWER_SAVE_VOLTAGE, cfg.powerSaveVoltage);
}

// ---------------------------------------------------------------------------
// Checksum
// ---------------------------------------------------------------------------

void test_checksum_is_deterministic(void) {
    SystemConfig cfg;
    ConfigManager_setDefaults(&cfg);
    uint32_t cs1 = calculateChecksum(&cfg);
    uint32_t cs2 = calculateChecksum(&cfg);
    TEST_ASSERT_EQUAL_UINT32(cs1, cs2);
}

void test_checksum_changes_on_field_modification(void) {
    SystemConfig cfg;
    ConfigManager_setDefaults(&cfg);
    uint32_t cs1 = calculateChecksum(&cfg);

    cfg.iridiumInterval = 999999;
    uint32_t cs2 = calculateChecksum(&cfg);

    TEST_ASSERT_NOT_EQUAL(cs1, cs2);
}

void test_defaults_checksum_is_valid(void) {
    SystemConfig cfg;
    ConfigManager_setDefaults(&cfg);
    uint32_t expected = calculateChecksum(&cfg);
    TEST_ASSERT_EQUAL_UINT32(expected, cfg.checksum);
}

// ---------------------------------------------------------------------------
// EEPROM round-trip (save + load)
// ---------------------------------------------------------------------------

void test_save_and_load_round_trip(void) {
    EEPROM.clear();

    SystemConfig original;
    ConfigManager_setDefaults(&original);
    original.iridiumInterval = 120000;
    original.enablePSM = true;
    ConfigManager_save(&original);

    SystemConfig loaded;
    memset(&loaded, 0xFF, sizeof(loaded));
    bool ok = ConfigManager_load(&loaded);

    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_HEX32(0xDEADBEEF, loaded.magic);
    TEST_ASSERT_EQUAL(120000, loaded.iridiumInterval);
    TEST_ASSERT_TRUE(loaded.enablePSM);
}

void test_load_fails_on_blank_eeprom(void) {
    EEPROM.clear();
    SystemConfig cfg;
    bool ok = ConfigManager_load(&cfg);
    TEST_ASSERT_FALSE(ok);
}

void test_load_fails_on_corrupted_checksum(void) {
    EEPROM.clear();

    SystemConfig cfg;
    ConfigManager_setDefaults(&cfg);
    ConfigManager_save(&cfg);

    // Corrupt one byte in EEPROM
    uint8_t b = EEPROM.read(8);
    EEPROM.write(8, b ^ 0xFF);

    SystemConfig loaded;
    bool ok = ConfigManager_load(&loaded);
    TEST_ASSERT_FALSE(ok);
}

// ---------------------------------------------------------------------------
// Feature enable/disable
// ---------------------------------------------------------------------------

void test_enable_feature_iridium(void) {
    ConfigManager_setDefaults(&sysConfig);
    ConfigManager_enableFeature("iridium", false);
    TEST_ASSERT_FALSE(sysConfig.enableIridium);
    ConfigManager_enableFeature("iridium", true);
    TEST_ASSERT_TRUE(sysConfig.enableIridium);
}

void test_enable_feature_case_insensitive(void) {
    ConfigManager_setDefaults(&sysConfig);
    ConfigManager_enableFeature("MESHTASTIC", false);
    TEST_ASSERT_FALSE(sysConfig.enableMeshtastic);
}

void test_enable_feature_unknown_does_not_crash(void) {
    ConfigManager_setDefaults(&sysConfig);
    ConfigManager_enableFeature("nonexistent", true);
    // Should not crash; features unchanged
    TEST_ASSERT_TRUE(sysConfig.enableIridium);
}

// ---------------------------------------------------------------------------
// Interval setters
// ---------------------------------------------------------------------------

void test_set_iridium_interval(void) {
    ConfigManager_setDefaults(&sysConfig);
    ConfigManager_setIridiumInterval(300000);
    TEST_ASSERT_EQUAL(300000, sysConfig.iridiumInterval);
}

void test_set_meshtastic_interval(void) {
    ConfigManager_setDefaults(&sysConfig);
    ConfigManager_setMeshtasticInterval(5000);
    TEST_ASSERT_EQUAL(5000, sysConfig.meshtasticInterval);
}

void test_set_mavlink_interval(void) {
    ConfigManager_setDefaults(&sysConfig);
    ConfigManager_setMAVLinkInterval(500);
    TEST_ASSERT_EQUAL(500, sysConfig.mavlinkInterval);
}

// ---------------------------------------------------------------------------
// Timed event
// ---------------------------------------------------------------------------

void test_set_timed_event_delay_mode(void) {
    ConfigManager_setDefaults(&sysConfig);
    ConfigManager_setTimedEvent(false, 3600, 1200);

    TEST_ASSERT_TRUE(sysConfig.timedEvent.enabled);
    TEST_ASSERT_FALSE(sysConfig.timedEvent.useAbsoluteTime);
    TEST_ASSERT_EQUAL(3600, sysConfig.timedEvent.triggerTime);
    TEST_ASSERT_EQUAL(1200, sysConfig.timedEvent.durationSeconds);
}

void test_set_timed_event_gmt_mode(void) {
    ConfigManager_setDefaults(&sysConfig);
    ConfigManager_setTimedEvent(true, 1700000000, 600);

    TEST_ASSERT_TRUE(sysConfig.timedEvent.enabled);
    TEST_ASSERT_TRUE(sysConfig.timedEvent.useAbsoluteTime);
    TEST_ASSERT_EQUAL_UINT32(1700000000, sysConfig.timedEvent.triggerTime);
}

// ---------------------------------------------------------------------------
// Power save voltage
// ---------------------------------------------------------------------------

void test_set_power_save_voltage(void) {
    ConfigManager_setDefaults(&sysConfig);
    ConfigManager_setPowerSaveVoltage(10.5f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 10.5f, sysConfig.powerSaveVoltage);
}

// ---------------------------------------------------------------------------
// Unity entry point
// ---------------------------------------------------------------------------

void setUp(void) {
    stub_set_millis(0);
    EEPROM.clear();
    memset(&sysConfig, 0, sizeof(sysConfig));
}

void tearDown(void) {}

int main(int argc, char** argv) {
    UNITY_BEGIN();

    // Defaults
    RUN_TEST(test_defaults_sets_magic_number);
    RUN_TEST(test_defaults_sets_intervals);
    RUN_TEST(test_defaults_enables_correct_features);
    RUN_TEST(test_defaults_timed_event_disabled);
    RUN_TEST(test_defaults_power_save_voltage);

    // Checksum
    RUN_TEST(test_checksum_is_deterministic);
    RUN_TEST(test_checksum_changes_on_field_modification);
    RUN_TEST(test_defaults_checksum_is_valid);

    // EEPROM round-trip
    RUN_TEST(test_save_and_load_round_trip);
    RUN_TEST(test_load_fails_on_blank_eeprom);
    RUN_TEST(test_load_fails_on_corrupted_checksum);

    // Feature enable/disable
    RUN_TEST(test_enable_feature_iridium);
    RUN_TEST(test_enable_feature_case_insensitive);
    RUN_TEST(test_enable_feature_unknown_does_not_crash);

    // Intervals
    RUN_TEST(test_set_iridium_interval);
    RUN_TEST(test_set_meshtastic_interval);
    RUN_TEST(test_set_mavlink_interval);

    // Timed event
    RUN_TEST(test_set_timed_event_delay_mode);
    RUN_TEST(test_set_timed_event_gmt_mode);

    // Power save
    RUN_TEST(test_set_power_save_voltage);

    return UNITY_END();
}
