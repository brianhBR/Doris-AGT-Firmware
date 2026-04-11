/*
 * Doris AGT Firmware — Subordinate Safety Monitor & Comms Relay
 *
 * The Lua dive script on ArduSub controls the dive profile.  The AGT provides:
 *   - GPS -> ArduSub via MAVLink (GPS_INPUT for navigation)
 *   - GPS -> Meshtastic via NMEA (surface tracking)
 *   - Iridium SBD reporting (pre-dive test + recovery)
 *   - Safety failsafes: voltage, leak, heartbeat -> release relay
 *   - Status LEDs / strobe in recovery
 *   - Power relay: low-power mode in recovery
 *
 * States: PRE_DIVE -> DIVING -> RECOVERY
 *   PRE_DIVE -> DIVING:   depth > threshold (vehicle went underwater)
 *   DIVING   -> RECOVERY: depth < threshold AND GPS fix (after min duration)
 *                          OR failsafe trigger
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
// PSM removed — battery voltage comes from MAVLink autopilot
#include "modules/relay_controller.h"
#include "modules/config_manager.h"
#include "modules/state_machine.h"
#include "modules/mission_data.h"
#include "modules/doris_protocol.h"

#include "mavlink_platform.h"
#include <common/mavlink.h>

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

bool iridiumTestRequested = false;

void setupPins();
void loadConfiguration();
void updateLEDState();
void processSerialInput();
void processCommand(const String& cmd);
void checkFailsafe();
void checkStateTransitions();
void set_leak_from_serial(const String& arg);
void buildDorisReport(DorisReport* report);
void handleMTMessage(uint8_t msgId, const DorisConfig* config, const DorisCommand* command);

static uint64_t getRTCUnixUsec() {
    myRTC.getTime();
    uint16_t y = myRTC.year;
    if (y < 100) y += 2000;
    if (y < 1970 || y > 2099) return 0;
    uint8_t mo = myRTC.month, d = myRTC.dayOfMonth;
    uint8_t h = myRTC.hour, mi = myRTC.minute, s = myRTC.seconds;
    if (mo < 1 || mo > 12 || d < 1 || d > 31) return 0;
    uint32_t days = (y - 1970) * 365UL + (y - 1969) / 4 - (y - 1901) / 100 + (y - 1601) / 400;
    static const uint16_t daysToMonth[] = { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 };
    days += daysToMonth[mo - 1] + (d - 1);
    if (mo >= 3 && (y % 4 == 0) && ((y % 100 != 0) || (y % 400 == 0))) days++;
    uint64_t sec = ((uint64_t)days * 24 + h) * 3600 + (uint64_t)mi * 60 + s;
    return sec * 1000000ULL;
}

void setup() {
    setupPins();

    Serial.begin(MAVLINK_BAUD);
    unsigned long serialWait = millis();
    while (!Serial && (millis() - serialWait < 2000)) { ; }
    delay(100);

    DebugPrintln(F("==========================================="));
    DebugPrintln(F("  Doris AGT — Safety Monitor & Comms Relay"));
    DebugPrintln(F("==========================================="));
    myRTC.setTime(0, 0, 0, 0, 1, 1, 2025);

    loadConfiguration();
    MissionData_init();
    RelayController_init();
    StateMachine_init();

    myGPSPtr = new SFE_UBLOX_GNSS();
    if (!GPSManager_init(myGPSPtr)) {
        DebugPrintln(F("WARNING: GPS init failed"));
    }

    modemPtr = new IridiumSBD(IRIDIUM_SERIAL, IRIDIUM_SLEEP, IRIDIUM_RI);
    if (sysConfig.enableIridium) {
        IridiumManager_configure(modemPtr);
        DebugPrintln(F("Iridium: configured, init deferred until first send"));
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


    lastIridiumSend = millis();

    DebugPrintln(F("Setup complete. State: PRE_DIVE (ready)"));
    DebugPrintln(F("Type 'help' for commands."));
}

void loop() {
    unsigned long now = millis();

    // LEDs first: animation must stay smooth regardless of blocking I/O below.
    // Uses state/data from the previous iteration — perfectly fine for display.
    if (sysConfig.enableNeoPixels) {
        updateLEDState();
        NeoPixelController_update();
    }

    StateMachine_update();
    GPSManager_update();

    processSerialInput();

    if (sysConfig.enableMeshtastic) {
        MeshtasticInterface_update();
    }

    checkStateTransitions();
    if (StateMachine_getState() == STATE_DIVING) {
        checkFailsafe();
    }

    if (sysConfig.enableMAVLink) {
        MAVLinkInterface_sendHeartbeat();
    }

    // Manual Iridium test: triggered via MAVLink command or serial
    if (iridiumTestRequested && sysConfig.enableIridium && modemPtr) {
        iridiumTestRequested = false;

        DebugPrintln(F("==================================="));
        DebugPrintln(F("  IRIDIUM TEST: Starting"));
        DebugPrintln(F("==================================="));
        MAVLinkInterface_sendStatusText(6, "IRIDIUM: Test starting");

        DorisReport report;
        buildDorisReport(&report);

        uint8_t mtMsgId = 0;
        DorisConfig mtConfig = {};
        DorisCommand mtCommand = {};

        if (IridiumManager_sendDorisReport(&report, &mtMsgId, &mtConfig, &mtCommand)) {
            lastIridiumSend = millis();
            DebugPrintln(F("==================================="));
            DebugPrintln(F("  IRIDIUM TEST: PASSED"));
            DebugPrintln(F("==================================="));
            MAVLinkInterface_sendStatusText(6, "IRIDIUM: Test PASSED");
            if (mtMsgId != 0) {
                handleMTMessage(mtMsgId, &mtConfig, &mtCommand);
            }
        } else {
            lastIridiumSend = millis();
            DebugPrintln(F("==================================="));
            DebugPrintln(F("  IRIDIUM TEST: FAILED"));
            DebugPrintln(F("==================================="));
            MAVLinkInterface_sendStatusText(3, "IRIDIUM: Test FAILED");
        }

        delay(2000);
        GPSManager_reinit();
    }

    if (sysConfig.enableMAVLink &&
        (now - lastMAVLinkUpdate >= sysConfig.mavlinkInterval)) {
        uint64_t rtcUsec = getRTCUnixUsec();
        GPSData gpsData = GPSManager_getData();
        MAVLinkInterface_sendGPS(&gpsData, rtcUsec);
        if (rtcUsec != 0) {
            MAVLinkInterface_sendSystemTime(rtcUsec);
        }
        lastMAVLinkUpdate = now;
    }

    // Meshtastic: NMEA GPS position relay (always allowed)
    if (sysConfig.enableMeshtastic &&
        (now - lastMeshtasticUpdate >= sysConfig.meshtasticInterval)) {
        lastMeshtasticUpdate = now;
        if (GPSManager_hasFix()) {
            GPSData gpsData = GPSManager_getData();
            MeshtasticInterface_sendPosition(&gpsData);
        } else {
            MeshtasticInterface_sendNoFixNMEA();
        }
    }

    // Iridium: binary Doris report (allowed in PRE_DIVE and RECOVERY only)
    if (sysConfig.enableIridium && modemPtr && StateMachine_canTransmitIridium() &&
        (now - lastIridiumSend >= sysConfig.iridiumInterval)) {
        if (GPSManager_hasFix()) {
            DorisReport report;
            buildDorisReport(&report);

            uint8_t mtMsgId = 0;
            DorisConfig mtConfig = {};
            DorisCommand mtCommand = {};

            bool sent = IridiumManager_sendDorisReport(&report, &mtMsgId, &mtConfig, &mtCommand);
            lastIridiumSend = millis();
            if (sent && mtMsgId != 0) {
                handleMTMessage(mtMsgId, &mtConfig, &mtCommand);
            }

            DebugPrintln(F("GPS: Re-initializing after Iridium send..."));
            delay(2000);
            GPSManager_reinit();
        }
    }

    if (now - lastStatusPrint > 60000) {
        StateMachine_printState();
        lastStatusPrint = now;
    }

    delay(10);
}

void setupPins() {
#ifndef NO_RELAYS
    pinMode(RELAY_POWER_MGMT, OUTPUT);
    digitalWrite(RELAY_POWER_MGMT, LOW);
    pinMode(RELAY_TIMED_EVENT, OUTPUT);
    digitalWrite(RELAY_TIMED_EVENT, LOW);
#endif

    pinMode(IRIDIUM_PWR_EN, OUTPUT);
    digitalWrite(IRIDIUM_PWR_EN, LOW);
    pinMode(IRIDIUM_SLEEP, OUTPUT);
    digitalWrite(IRIDIUM_SLEEP, LOW);
    pinMode(SUPERCAP_CHG_EN, OUTPUT);
    digitalWrite(SUPERCAP_CHG_EN, LOW);

    am_hal_gpio_pincfg_t pinCfg = g_AM_HAL_GPIO_OUTPUT;
    pinCfg.eGPOutcfg = AM_HAL_GPIO_PIN_OUTCFG_OPENDRAIN;
    pin_config(PinName(GNSS_EN), pinCfg);
    delay(1);
    digitalWrite(GNSS_EN, HIGH);

    pinMode(BUS_VOLTAGE_MON_EN, OUTPUT);
    pinMode(LED_WHITE, OUTPUT);
    pinMode(IRIDIUM_NA, INPUT);
    pinMode(IRIDIUM_RI, INPUT);
    pinMode(SUPERCAP_PGOOD, INPUT);
    pinMode(GEOFENCE_PIN, INPUT);

    digitalWrite(BUS_VOLTAGE_MON_EN, HIGH);
    digitalWrite(LED_WHITE, LOW);
}

void loadConfiguration() {
    if (!ConfigManager_load(&sysConfig)) {
        ConfigManager_setDefaults(&sysConfig);
    }
    ConfigManager_printConfig(&sysConfig);
}

void updateLEDState() {
    // Sync RTC whenever we have a GPS fix
    if (GPSManager_hasFix()) {
        GPSData g = GPSManager_getData();
        myRTC.setTime(g.hour, g.minute, g.second, 0, g.day, g.month, g.year);
    }

    if (StateMachine_getState() == STATE_RECOVERY) {
        NeoPixelController_setMode(LED_MODE_RECOVERY);
        return;
    }

    if (StateMachine_getState() == STATE_DIVING) {
        if (NeoPixelController_isLuaActive()) {
            NeoPixelController_setMode(LED_MODE_LUA);
        } else {
            NeoPixelController_setMode(LED_MODE_DIVING);
        }
        return;
    }

    int dorisState = MissionData_getDorisState();

    // MISSION_START (0+): Lua passed pre-arm checks and armed.
    // Stay green until the Lua script reports DESCENT and we enter DIVING.
    if (MissionData_hasDorisState() && dorisState >= 0) {
        NeoPixelController_setMode(LED_MODE_READY);
        return;
    }

    // CONFIG (-1) or no Lua state yet: show pre-arm status
    bool gps = GPSManager_hasFix();
    bool pi  = MissionData_isPiConnected();

    MissionData md;
    MissionData_get(&md);
    bool failsafe = md.leak_detected ||
                    (md.voltage_from_autopilot && md.battery_voltage > 0 &&
                     md.battery_voltage < BATTERY_LOW_VOLTAGE) ||
                    (!pi && MissionData_hasHadHeartbeat()) ||
                    MissionData_isAutopilotFailsafe() ||
                    MissionData_hasUnhealthySensors();

    if (failsafe) {
        NeoPixelController_setMode(LED_MODE_ERROR);
    } else if (pi && gps) {
        NeoPixelController_setMode(LED_MODE_READY);
    } else {
        NeoPixelController_setMode(LED_MODE_STANDBY);
    }
}

void checkStateTransitions() {
    int dorisState = MissionData_getDorisState();

    // Follow autopilot: CONFIG/MISSION_START → PRE_DIVE
    if (dorisState <= 0 && StateMachine_getState() != STATE_PRE_DIVE) {
        StateMachine_reset();
    }

    // Follow autopilot: DESCENT/ON_BOTTOM/ASCENT → DIVING
    if (dorisState >= 1 && dorisState <= 3 &&
        StateMachine_getState() == STATE_PRE_DIVE) {
        StateMachine_enterDiving();
    }

    // Follow autopilot: RECOVERY
    if (dorisState >= 4 && StateMachine_getState() != STATE_RECOVERY) {
        StateMachine_enterRecovery();
    }

    // Independent surface detection: ASCENT + shallow + GPS → RECOVERY
    if (StateMachine_getState() == STATE_DIVING && dorisState >= 3) {
        MissionData md;
        MissionData_get(&md);
        bool shallow = md.depth_valid && md.depth_m < RECOVERY_DEPTH_THRESHOLD_M;
        bool gpsFix = GPSManager_hasFix();
        if (shallow && gpsFix) {
            StateMachine_enterRecovery();
        }
    }
}

void checkFailsafe() {
    MissionData md;
    MissionData_get(&md);
    unsigned long now = millis();
    unsigned long timeInDive = StateMachine_getTimeInState() * 1000UL;

    if (md.voltage_from_autopilot &&
        md.battery_voltage > 0 && md.battery_voltage < BATTERY_CRITICAL_VOLTAGE) {
        StateMachine_triggerFailsafe(FAILSAFE_LOW_VOLTAGE);
        return;
    }
    if (md.leak_detected) {
        StateMachine_triggerFailsafe(FAILSAFE_LEAK);
        return;
    }
    if (md.heartbeat_valid &&
        timeInDive > DIVE_HEARTBEAT_GRACE_MS &&
        (now - md.last_heartbeat_ms) > FAILSAFE_HEARTBEAT_TIMEOUT_MS) {
        StateMachine_triggerFailsafe(FAILSAFE_NO_HEARTBEAT);
        return;
    }
}

// Unified serial reader: routes bytes to MAVLink parser and text command buffer.
static char cmdBuf[128];
static uint8_t cmdLen = 0;

void processSerialInput() {
    if (!Serial.available()) return;

    mavlink_message_t msg;
    mavlink_status_t mavStatus;

    // Limit bytes per loop iteration to keep LED animation smooth.
    // A full MAVLink message is ~60 bytes; 64 handles one message per loop.
    uint8_t bytesRead = 0;
    while (Serial.available() && bytesRead < 64) {
        bytesRead++;
        uint8_t c = Serial.read();

        if (sysConfig.enableMAVLink &&
            mavlink_parse_char(MAVLINK_COMM_0, c, &msg, &mavStatus)) {
            MAVLinkInterface_handleMessage(&msg);
            continue;
        }

        mavlink_status_t* chanStatus = mavlink_get_channel_status(MAVLINK_COMM_0);
        if (chanStatus->parse_state != MAVLINK_PARSE_STATE_IDLE) {
            continue;
        }

        if (c == '\n' || c == '\r') {
            if (cmdLen > 0) {
                cmdBuf[cmdLen] = '\0';
                String cmd(cmdBuf);
                cmd.trim();
                cmdLen = 0;
                if (cmd.length() > 0) {
                    processCommand(cmd);
                }
            }
        } else if (c >= 0x20 && c < 0x7F && cmdLen < sizeof(cmdBuf) - 1) {
            cmdBuf[cmdLen++] = c;
        }
    }
}

void processCommand(const String& cmd) {
    if (cmd == "help") {
        DebugPrintln(F("--- Commands ---"));
        DebugPrintln(F("status / gps      State and GPS"));
        DebugPrintln(F("gps_diag          GPS BBR/backup battery diagnostics"));
        DebugPrintln(F("release_now       Trigger release relay (failsafe)"));
        DebugPrintln(F("reset             Back to PRE_DIVE"));
        DebugPrintln(F("iridium_test      Send Iridium test message"));
        DebugPrintln(F("reboot            Software reboot"));
        DebugPrintln(F("set_leak <0|1>    Set leak flag for testing"));
        DebugPrintln(F("config / save / set_* / enable_* / disable_*"));
        DebugPrintln(F("mesh_test / mesh_test_gps / mesh_send <text>"));
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
            DebugPrintln(buf);
        } else {
            GPSData g = GPSManager_getData();
            DebugPrint(F("Sats: ")); DebugPrintln(g.satellites);
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
    if (cmd == "iridium_test") {
        iridiumTestRequested = true;
        DebugPrintln(F("Iridium test queued"));
        return;
    }
    if (cmd == "reboot") {
        DebugPrintln(F("Rebooting..."));
        Serial.flush();
        delay(100);
        NVIC_SystemReset();
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
    DebugPrint(F("Leak flag: "));
    DebugPrintln(v ? F("SET") : F("CLEAR"));
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

    // Map AGT state to Doris protocol state
    SystemState st = StateMachine_getState();
    StateMachineStatus smStatus = StateMachine_getStatus();
    switch (st) {
        case STATE_PRE_DIVE:  report->mission_state = DORIS_STATE_PRE_DIVE; break;
        case STATE_DIVING:    report->mission_state = DORIS_STATE_DIVING;   break;
        case STATE_RECOVERY:  report->mission_state = DORIS_STATE_RECOVERY; break;
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
    if (mission.heartbeat_valid &&
        (millis() - mission.last_heartbeat_ms) > FAILSAFE_HEARTBEAT_TIMEOUT_MS)
        report->failsafe_flags |= DORIS_FAILSAFE_NO_HEARTBEAT;
    if (smStatus.lastFailsafeSource == FAILSAFE_MANUAL)
        report->failsafe_flags |= DORIS_FAILSAFE_MANUAL;

    // Power (battery voltage from MAVLink autopilot, no PSM)
    report->battery_voltage = (uint16_t)(mission.battery_voltage * 1000.0f);
    report->battery_current = 0;
    report->bus_voltage     = (uint16_t)(iridiumGetBusVoltage() * 1000.0f);

    // Time
    report->uptime_s = (uint32_t)(millis() / 1000UL);
    uint64_t rtcUsec = getRTCUnixUsec();
    report->time_unix = (uint32_t)(rtcUsec / 1000000ULL);

    report->reserved = 0;
}

void handleMTMessage(uint8_t msgId, const DorisConfig* config, const DorisCommand* command) {
    DebugPrint(F("MT: Received message ID "));
    DebugPrintln(msgId);

    if (msgId == DORIS_MSG_ID_CONFIG && config != nullptr) {
        if (config->iridium_interval_s > 0) {
            uint32_t newInterval = (uint32_t)config->iridium_interval_s * 1000UL;
            DebugPrint(F("MT: Setting Iridium interval to "));
            DebugPrint(config->iridium_interval_s);
            DebugPrintln(F("s"));
            ConfigManager_setIridiumInterval(newInterval);
        }
        if (config->led_mode != DORIS_LED_NO_CHANGE) {
            DebugPrint(F("MT: LED mode -> "));
            DebugPrintln(config->led_mode);
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
            DebugPrint(F("MT: NeoPixel brightness -> "));
            DebugPrintln(config->neopixel_brightness);
            NeoPixelController_setBrightness(config->neopixel_brightness);
        }
        if (config->power_save_voltage_mv > 0) {
            float newV = config->power_save_voltage_mv / 1000.0f;
            DebugPrint(F("MT: Power save voltage -> "));
            DebugPrintln(newV, 2);
            ConfigManager_setPowerSaveVoltage(newV);
        }
        ConfigManager_save(&sysConfig);
    }

    if (msgId == DORIS_MSG_ID_COMMAND && command != nullptr) {
        DebugPrint(F("MT: Command "));
        DebugPrintln(command->command);
        switch (command->command) {
            case DORIS_CMD_SEND_REPORT:
                DebugPrintln(F("MT: Forcing immediate report on next cycle"));
                lastIridiumSend = 0;
                break;
            case DORIS_CMD_RELEASE:
                DebugPrintln(F("MT: Remote release triggered!"));
                StateMachine_triggerFailsafe(FAILSAFE_MANUAL);
                break;
            case DORIS_CMD_RESET_STATE:
                DebugPrintln(F("MT: Resetting state machine"));
                StateMachine_reset();
                break;
            case DORIS_CMD_REBOOT:
                DebugPrintln(F("MT: Rebooting..."));
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
                DebugPrintln(F("MT: Unknown command"));
                break;
        }
    }
}
