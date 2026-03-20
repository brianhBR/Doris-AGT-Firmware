/*
 * Doris AGT Firmware - Simplified Drop Camera
 *
 * Core functions:
 * - GPS -> ArduSub via MAVLink over USB
 * - GPS -> Meshtastic via protobuf (built-in position)
 * - Iridium: position + mission stats (voltage, leak, max depth)
 * - Failsafe: release relay on low voltage, leak, max depth, no heartbeat
 * - LEDs: status + strobe in Recovery for locating
 *
 * States: PRE_MISSION -> SELF_TEST -> MISSION -> RECOVERY
 * - Self Test -> Mission: depth > 2m (from MAVLink)
 * - Mission -> Recovery: depth < 3m OR GPS fix
 */

#include <Arduino.h>
#include <Wire.h>
#include "config.h"
#include <SparkFun_u-blox_GNSS_Arduino_Library.h>
#include <IridiumSBD.h>
#include <ArduinoJson.h>
#include <RTC.h>

#include "modules/gps_manager.h"
#include "modules/iridium_manager.h"
#include "modules/meshtastic_interface.h"
#include "modules/mavlink_interface.h"
#include "modules/neopixel_controller.h"
#include "modules/psm_interface.h"
#include "modules/relay_controller.h"
#include "modules/config_manager.h"
#include "modules/state_machine.h"
#include "modules/mission_data.h"
#include "modules/doris_protocol.h"

SFE_UBLOX_GNSS* myGPSPtr = nullptr;
IridiumSBD* modemPtr = nullptr;
SystemConfig sysConfig;
extern Apollo3RTC rtc;
Apollo3RTC& myRTC = rtc;

unsigned long lastIridiumSend = 0;
unsigned long lastMeshtasticUpdate = 0;
unsigned long lastMAVLinkUpdate = 0;


unsigned long lastPSMUpdate = 0;
unsigned long lastStatusPrint = 0;
LEDState currentLEDState = LED_STATE_BOOT;

void setupPins();
void loadConfiguration();
void updateLEDState();
void processSerialCommands();
void checkFailsafe();
void checkStateTransitions();
void set_leak_from_serial(const String& arg);
void buildDorisReport(DorisReport* report);
void handleMTMessage(uint8_t msgId, const DorisConfig* config, const DorisCommand* command);

// Convert RTC date/time to Unix timestamp (microseconds). Returns 0 if RTC not set.
// RTC is synced from GPS in updateLEDState(). ArduSub uses this when BRD_RTC_TYPES=2.
static uint64_t getRTCUnixUsec() {
    myRTC.getTime();
    uint16_t y = myRTC.year;
    if (y < 100) y += 2000;
    if (y < 1970 || y > 2099) return 0;
    uint8_t mo = myRTC.month, d = myRTC.dayOfMonth;
    uint8_t h = myRTC.hour, mi = myRTC.minute, s = myRTC.seconds;
    if (mo < 1 || mo > 12 || d < 1 || d > 31) return 0;
    // Days since 1970-01-01 (UTC)
    uint32_t days = (y - 1970) * 365UL + (y - 1969) / 4 - (y - 1901) / 100 + (y - 1601) / 400;
    static const uint16_t daysToMonth[] = { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 };
    days += daysToMonth[mo - 1] + (d - 1);
    if (mo >= 3 && (y % 4 == 0) && ((y % 100 != 0) || (y % 400 == 0))) days++;
    uint64_t sec = ((uint64_t)days * 24 + h) * 3600 + (uint64_t)mi * 60 + s;
    return sec * 1000000ULL;
}

void setup() {
    Serial.begin(MAVLINK_BAUD);
    while (!Serial) { ; }
    delay(100);

    Serial.println(F("==========================================="));
    Serial.println(F("  Doris AGT - Drop Camera (Simplified)"));
    Serial.println(F("==========================================="));

    setupPins();
    myRTC.setTime(0, 0, 0, 0, 1, 1, 2025);

    loadConfiguration();
    MissionData_init();
    RelayController_init();
    StateMachine_init();

    // GPS first — Iridium deferred until first send to avoid power-cycling GPS
    myGPSPtr = new SFE_UBLOX_GNSS();
    if (!GPSManager_init(myGPSPtr)) {
        Serial.println(F("WARNING: GPS init failed"));
    }

    modemPtr = new IridiumSBD(IRIDIUM_SERIAL, IRIDIUM_SLEEP, IRIDIUM_RI);
    if (sysConfig.enableIridium) {
        IridiumManager_configure(modemPtr);
        Serial.println(F("Iridium: configured, init deferred until first send"));
    }

    if (sysConfig.enableMeshtastic) {
        MeshtasticInterface_init();
    }

    if (sysConfig.enableMAVLink) {
        MAVLinkInterface_init();
    }

    if (sysConfig.enableNeoPixels) {
        NeoPixelController_init();
    }

    if (sysConfig.enablePSM) {
        if (!PSMInterface_init()) sysConfig.enablePSM = false;
    }

    Serial.println(F("Setup complete. State: PRE_MISSION"));
    Serial.println(F("Send 'start_self_test' to begin. Type 'help' for commands."));
}

void loop() {
    unsigned long now = millis();

    StateMachine_update();
    GPSManager_update();

    processSerialCommands();

    if (sysConfig.enableMAVLink) {
        MAVLinkInterface_update();
    }
    if (sysConfig.enableMeshtastic) {
        MeshtasticInterface_update();
    }

    checkStateTransitions();
    if (StateMachine_getState() == STATE_MISSION) {
        checkFailsafe();
    }

    updateLEDState();
    if (sysConfig.enableNeoPixels) {
        NeoPixelController_update(currentLEDState);
    }

    // GPS -> MAVLink (feed ArduSub) + RTC time for ArduSub clock (set BRD_RTC_TYPES=2 on autopilot)
    if (sysConfig.enableMAVLink && (now - lastMAVLinkUpdate >= sysConfig.mavlinkInterval)) {
        if (GPSManager_hasFix()) {
            GPSData gpsData = GPSManager_getData();
            MAVLinkInterface_sendGPS(&gpsData);
        }
        if (sysConfig.enablePSM) {
            BatteryData b = PSMInterface_getData();
            MAVLinkInterface_sendStatus(b.voltage, b.current);
        }
        uint64_t rtcUsec = getRTCUnixUsec();
        if (rtcUsec != 0) {
            MAVLinkInterface_sendSystemTime(rtcUsec);
        }
        MAVLinkInterface_sendHeartbeat();
        lastMAVLinkUpdate = now;
    }

    // Meshtastic: NMEA GPS on pin 39 (D39). Send real position when fix, else "no fix" so UART sees activity.
    if (sysConfig.enableMeshtastic && StateMachine_canTransmitMeshtastic() &&
        (now - lastMeshtasticUpdate >= sysConfig.meshtasticInterval)) {
        lastMeshtasticUpdate = now;
        if (GPSManager_hasFix()) {
            GPSData gpsData = GPSManager_getData();
            MeshtasticInterface_sendPosition(&gpsData);
        } else {
            MeshtasticInterface_sendNoFixNMEA();
        }
    }

    // Iridium: binary Doris report + MT command check
    // Full RF switch cycle: GPS off -> Iridium on -> send -> Iridium off -> GPS on -> GPS reinit
    if (sysConfig.enableIridium && modemPtr && StateMachine_canTransmitIridium() &&
        (now - lastIridiumSend >= sysConfig.iridiumInterval)) {
        if (GPSManager_hasFix()) {
            DorisReport report;
            buildDorisReport(&report);

            currentLEDState = LED_STATE_IRIDIUM_TX;

            uint8_t mtMsgId = 0;
            DorisConfig mtConfig = {};
            DorisCommand mtCommand = {};

            if (IridiumManager_sendDorisReport(&report, &mtMsgId, &mtConfig, &mtCommand)) {
                lastIridiumSend = millis();
                if (mtMsgId != 0) {
                    handleMTMessage(mtMsgId, &mtConfig, &mtCommand);
                }
            }
            Serial.println(F("GPS: Re-initializing after Iridium send..."));
            delay(2000);
            GPSManager_reinit();
        }
    }

    if (sysConfig.enablePSM && (now - lastPSMUpdate >= PSM_UPDATE_MS)) {
        PSMInterface_update();
        lastPSMUpdate = now;
        MissionData md;
        MissionData_get(&md);
        if (md.battery_voltage <= 0) {
            BatteryData b = PSMInterface_getData();
            MissionData_update_voltage(b.voltage);
        }
    }

    if (now - lastStatusPrint > 60000) {
        StateMachine_printState();
        lastStatusPrint = now;
    }

    delay(10);
}

void setupPins() {
    // CRITICAL: Force both RF paths OFF immediately at boot.
    // Apollo3 GPIOs default to input (high-Z) — GNSS_EN could float LOW (GPS on)
    // and IRIDIUM_PWR_EN could float (Iridium on), damaging the AS179 switch.
    pinMode(IRIDIUM_PWR_EN, OUTPUT);
    digitalWrite(IRIDIUM_PWR_EN, LOW);     // Iridium power OFF
    pinMode(IRIDIUM_SLEEP, OUTPUT);
    digitalWrite(IRIDIUM_SLEEP, LOW);      // Iridium modem sleep
    pinMode(SUPERCAP_CHG_EN, OUTPUT);
    digitalWrite(SUPERCAP_CHG_EN, LOW);    // Supercap charger OFF

    // GNSS_EN must be open-drain (per SparkFun AGT schematic)
    am_hal_gpio_pincfg_t pinCfg = g_AM_HAL_GPIO_OUTPUT;
    pinCfg.eGPOutcfg = AM_HAL_GPIO_PIN_OUTCFG_OPENDRAIN;
    pin_config(PinName(GNSS_EN), pinCfg);
    delay(1);
    digitalWrite(GNSS_EN, HIGH);           // GPS power OFF (active low)

    // Other pins
    pinMode(BUS_VOLTAGE_MON_EN, OUTPUT);
    pinMode(LED_WHITE, OUTPUT);
    pinMode(IRIDIUM_NA, INPUT);
    pinMode(IRIDIUM_RI, INPUT);
    pinMode(SUPERCAP_PGOOD, INPUT);
    pinMode(GEOFENCE_PIN, INPUT);
    // Relay pins: keep Pi/Navigator powered from the first moment
    pinMode(RELAY_POWER_MGMT, OUTPUT);
    pinMode(RELAY_TIMED_EVENT, OUTPUT);

    digitalWrite(BUS_VOLTAGE_MON_EN, HIGH);
    digitalWrite(LED_WHITE, LOW);
    digitalWrite(RELAY_POWER_MGMT, RELAY_ACTIVE_HIGH ? HIGH : LOW);
    digitalWrite(RELAY_TIMED_EVENT, RELAY_ACTIVE_HIGH ? LOW : HIGH);
}

void loadConfiguration() {
    if (!ConfigManager_load(&sysConfig)) {
        ConfigManager_setDefaults(&sysConfig);
    }
    ConfigManager_printConfig(&sysConfig);
}

void updateLEDState() {
    if (StateMachine_isRecoveryStrobe()) {
        currentLEDState = LED_STATE_RECOVERY_STROBE;
        return;
    }
    switch (StateMachine_getState()) {
        case STATE_PRE_MISSION:
            currentLEDState = LED_STATE_PRE_MISSION;
            break;
        case STATE_SELF_TEST:
            currentLEDState = LED_STATE_SELF_TEST;
            break;
        case STATE_MISSION:
            currentLEDState = GPSManager_hasFix() ? LED_STATE_GPS_FIX : LED_STATE_GPS_SEARCH;
            break;
        case STATE_RECOVERY:
            currentLEDState = LED_STATE_RECOVERY_STROBE;
            break;
    }
    if (GPSManager_hasFix()) {
        GPSData g = GPSManager_getData();
        myRTC.setTime(g.hour, g.minute, g.second, 0, g.day, g.month, g.year);
    }
}

void checkStateTransitions() {
    MissionData md;
    MissionData_get(&md);

    // SELF_TEST -> MISSION: only on confirmed depth from autopilot
    if (StateMachine_getState() == STATE_SELF_TEST &&
        md.depth_valid && md.depth_m > MISSION_DEPTH_THRESHOLD_M) {
        StateMachine_enterMission();
    }

    // MISSION -> RECOVERY: require confirmed depth data for the depth check.
    // GPS fix alone can trigger recovery (GPS doesn't work underwater, so fix = surface).
    if (StateMachine_getState() == STATE_MISSION) {
        bool shallow = md.depth_valid && md.depth_m < RECOVERY_DEPTH_THRESHOLD_M;
        if (shallow || GPSManager_hasFix()) {
            StateMachine_enterRecovery();
        }
    }
}

void checkFailsafe() {
    MissionData md;
    MissionData_get(&md);
    unsigned long now = millis();

    // Battery failsafe: only act on voltage confirmed from the autopilot via MAVLink.
    // PSM fallback data is used for reporting but not for failsafe decisions.
    if (md.voltage_from_autopilot &&
        md.battery_voltage > 0 && md.battery_voltage < BATTERY_CRITICAL_VOLTAGE) {
        StateMachine_triggerFailsafe(FAILSAFE_LOW_VOLTAGE);
        return;
    }
    if (md.leak_detected) {
        StateMachine_triggerFailsafe(FAILSAFE_LEAK);
        return;
    }
    if (md.depth_valid && md.max_depth_m >= FAILSAFE_MAX_DEPTH_M) {
        StateMachine_triggerFailsafe(FAILSAFE_MAX_DEPTH);
        return;
    }
    if (md.heartbeat_valid && (now - md.last_heartbeat_ms) > FAILSAFE_HEARTBEAT_TIMEOUT_MS) {
        StateMachine_triggerFailsafe(FAILSAFE_NO_HEARTBEAT);
        return;
    }
}

void processSerialCommands() {
    if (!Serial.available()) return;

    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd.length() == 0) return;

    if (cmd == "help") {
        Serial.println(F("--- Commands ---"));
        Serial.println(F("start_self_test   Start self test; depth>2m -> Mission"));
        Serial.println(F("status / gps      State and GPS"));
        Serial.println(F("gps_diag          GPS BBR/backup battery diagnostics"));
        Serial.println(F("release_now       Trigger release relay (failsafe)"));
        Serial.println(F("reset             Back to PRE_MISSION"));
        Serial.println(F("set_leak <0|1>   Set leak flag for testing"));
        Serial.println(F("config / save / set_* / enable_* / disable_*"));
        Serial.println(F("mesh_test / mesh_test_gps / mesh_send <text>"));
        return;
    }

    if (cmd == "start_self_test") {
        StateMachine_startSelfTest();
        return;
    }
    if (cmd == "status") {
        StateMachine_printState();
        return;
    }
    if (cmd == "gps") {
        if (GPSManager_hasFix()) {
            char buf[200];
            GPSManager_getDataString(buf, sizeof(buf));
            Serial.println(buf);
        } else {
            GPSData g = GPSManager_getData();
            Serial.print(F("Sats: ")); Serial.println(g.satellites);
        }
        return;
    }
    if (cmd == "gps_diag") {
        GPSManager_printDiagnostics();
        return;
    }
    if (cmd == "release_now") {
        StateMachine_triggerFailsafe(FAILSAFE_MANUAL);
        return;
    }
    if (cmd == "reset") {
        StateMachine_reset();
        return;
    }
    if (cmd.startsWith("set_leak ")) {
        set_leak_from_serial(cmd.substring(9));
        return;
    }
    if (cmd == "mesh_test") {
        MeshtasticInterface_sendText("AGT test");
        return;
    }
    if (cmd == "mesh_test_gps") {
        MeshtasticInterface_sendTestNMEA();
        return;
    }
    if (cmd.startsWith("mesh_send ")) {
        MeshtasticInterface_sendText(cmd.c_str() + 10);
        return;
    }

    ConfigManager_processCommand(cmd);
}

void set_leak_from_serial(const String& arg) {
    int v = arg.toInt();
    MissionData_set_leak(v != 0);
    Serial.print(F("Leak flag: "));
    Serial.println(v ? F("SET") : F("CLEAR"));
}

// ============================================================================
// DORIS BINARY PROTOCOL HELPERS
// ============================================================================

static float iridiumGetBusVoltage() {
    pinMode(BUS_VOLTAGE_MON_EN, OUTPUT);
    digitalWrite(BUS_VOLTAGE_MON_EN, HIGH);
    delay(10);
    int rawValue = analogRead(BUS_VOLTAGE_PIN);
    float voltage = (rawValue / 16384.0) * 2.0 * 3.0;
    digitalWrite(BUS_VOLTAGE_MON_EN, LOW);
    return voltage;
}

void buildDorisReport(DorisReport* report) {
    memset(report, 0, sizeof(DorisReport));

    GPSData gps = GPSManager_getData();
    MissionData mission;
    MissionData_get(&mission);
    BatteryData batt = PSMInterface_getData();

    // State
    SystemState st = StateMachine_getState();
    StateMachineStatus smStatus = StateMachine_getStatus();
    switch (st) {
        case STATE_PRE_MISSION: report->mission_state = DORIS_STATE_PRE_MISSION; break;
        case STATE_SELF_TEST:   report->mission_state = DORIS_STATE_SELF_TEST;   break;
        case STATE_MISSION:     report->mission_state = DORIS_STATE_MISSION;     break;
        case STATE_RECOVERY:    report->mission_state = DORIS_STATE_RECOVERY;    break;
    }
    if (smStatus.lastFailsafeSource != FAILSAFE_NONE) {
        report->mission_state = DORIS_STATE_FAILSAFE;
    }

    // GPS
    report->gps_fix_type = gps.fixType;
    report->satellites   = gps.satellites;
    report->latitude     = (float)gps.latitude;
    report->longitude    = (float)gps.longitude;
    report->altitude     = (int16_t)gps.altitude;
    report->speed        = (uint16_t)(gps.speed * 100.0f);
    report->course       = (uint16_t)(gps.course * 100.0f);
    report->hdop         = (uint16_t)(gps.hdop * 100.0f);

    // Mission
    report->depth        = (uint16_t)(mission.depth_m * 10.0f);
    report->max_depth    = (uint16_t)(mission.max_depth_m * 10.0f);
    report->leak_detected = mission.leak_detected ? 1 : 0;

    // Failsafe flags
    report->failsafe_flags = 0;
    if (mission.voltage_from_autopilot && mission.battery_voltage > 0) {
        if (mission.battery_voltage < BATTERY_CRITICAL_VOLTAGE)
            report->failsafe_flags |= DORIS_FAILSAFE_CRITICAL_VOLTAGE;
        else if (mission.battery_voltage < BATTERY_LOW_VOLTAGE)
            report->failsafe_flags |= DORIS_FAILSAFE_LOW_VOLTAGE;
    }
    if (mission.leak_detected)
        report->failsafe_flags |= DORIS_FAILSAFE_LEAK;
    if (mission.depth_valid && mission.max_depth_m >= FAILSAFE_MAX_DEPTH_M)
        report->failsafe_flags |= DORIS_FAILSAFE_MAX_DEPTH;
    if (mission.heartbeat_valid &&
        (millis() - mission.last_heartbeat_ms) > FAILSAFE_HEARTBEAT_TIMEOUT_MS)
        report->failsafe_flags |= DORIS_FAILSAFE_NO_HEARTBEAT;
    if (smStatus.lastFailsafeSource == FAILSAFE_MANUAL)
        report->failsafe_flags |= DORIS_FAILSAFE_MANUAL;

    // Power — prefer autopilot voltage; fall back to PSM; bus voltage is AGT supply
    float v = mission.battery_voltage > 0 ? mission.battery_voltage : batt.voltage;
    report->battery_voltage = (uint16_t)(v * 1000.0f);
    report->battery_current = (uint16_t)(batt.current * 1000.0f);
    report->bus_voltage     = (uint16_t)(iridiumGetBusVoltage() * 1000.0f);

    // Time
    report->uptime_s = (uint32_t)(millis() / 1000UL);
    uint64_t rtcUsec = getRTCUnixUsec();
    report->time_unix = (uint32_t)(rtcUsec / 1000000ULL);

    report->reserved = 0;
}

void handleMTMessage(uint8_t msgId, const DorisConfig* config, const DorisCommand* command) {
    Serial.print(F("MT: Received message ID "));
    Serial.println(msgId);

    if (msgId == DORIS_MSG_ID_CONFIG && config != nullptr) {
        if (config->iridium_interval_s > 0) {
            uint32_t newInterval = (uint32_t)config->iridium_interval_s * 1000UL;
            Serial.print(F("MT: Setting Iridium interval to "));
            Serial.print(config->iridium_interval_s);
            Serial.println(F("s"));
            ConfigManager_setIridiumInterval(newInterval);
        }
        if (config->led_mode != DORIS_LED_NO_CHANGE) {
            Serial.print(F("MT: LED mode -> "));
            Serial.println(config->led_mode);
            switch (config->led_mode) {
                case DORIS_LED_OFF:
                    sysConfig.enableNeoPixels = false;
                    break;
                case DORIS_LED_NORMAL:
                    sysConfig.enableNeoPixels = true;
                    break;
                case DORIS_LED_STROBE:
                    sysConfig.enableNeoPixels = true;
                    break;
                default:
                    break;
            }
        }
        if (config->neopixel_brightness > 0) {
            Serial.print(F("MT: NeoPixel brightness -> "));
            Serial.println(config->neopixel_brightness);
            NeoPixelController_setBrightness(config->neopixel_brightness);
        }
        if (config->power_save_voltage_mv > 0) {
            float newV = config->power_save_voltage_mv / 1000.0f;
            Serial.print(F("MT: Power save voltage -> "));
            Serial.println(newV, 2);
            ConfigManager_setPowerSaveVoltage(newV);
        }
        ConfigManager_save(&sysConfig);
    }

    if (msgId == DORIS_MSG_ID_COMMAND && command != nullptr) {
        Serial.print(F("MT: Command "));
        Serial.println(command->command);
        switch (command->command) {
            case DORIS_CMD_SEND_REPORT:
                Serial.println(F("MT: Forcing immediate report on next cycle"));
                lastIridiumSend = 0;
                break;
            case DORIS_CMD_RELEASE:
                Serial.println(F("MT: Remote release triggered!"));
                StateMachine_triggerFailsafe(FAILSAFE_MANUAL);
                break;
            case DORIS_CMD_RESET_STATE:
                Serial.println(F("MT: Resetting state machine"));
                StateMachine_reset();
                break;
            case DORIS_CMD_REBOOT:
                Serial.println(F("MT: Rebooting..."));
                delay(500);
                NVIC_SystemReset();
                break;
            case DORIS_CMD_ENABLE_IRIDIUM:
                sysConfig.enableIridium = true;
                ConfigManager_save(&sysConfig);
                break;
            case DORIS_CMD_DISABLE_IRIDIUM:
                sysConfig.enableIridium = false;
                ConfigManager_save(&sysConfig);
                break;
            default:
                Serial.println(F("MT: Unknown command"));
                break;
        }
    }
}
