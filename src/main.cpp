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

SFE_UBLOX_GNSS* myGPSPtr = nullptr;
IridiumSBD* modemPtr = nullptr;
SystemConfig sysConfig;
extern Apollo3RTC rtc;
Apollo3RTC& myRTC = rtc;

unsigned long lastIridiumSend = 0;
unsigned long lastMeshtasticUpdate = 0;
unsigned long lastMAVLinkUpdate = 0;
unsigned long lastNeoPixelUpdate = 0;
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
    Serial.begin(DEBUG_BAUD);
    while (!Serial) { ; }
    delay(100);

    Serial.println(F("==========================================="));
    Serial.println(F("  Doris AGT - Drop Camera (Simplified)"));
    Serial.println(F("==========================================="));

    setupPins();
    myRTC.setTime(0, 0, 0, 0, 1, 1, 2025);

    loadConfiguration();
    MissionData_init();
    StateMachine_init();
    RelayController_init();

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
        NeoPixelController_setColor(COLOR_BOOT);
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
    if (sysConfig.enableNeoPixels && (now - lastNeoPixelUpdate >= NEOPIXEL_UPDATE_MS)) {
        NeoPixelController_update(currentLEDState);
        lastNeoPixelUpdate = now;
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
            Serial.println(F("Mesh: sent fix"));
        } else {
            MeshtasticInterface_sendNoFixNMEA();
            Serial.println(F("Mesh: sent nofix"));
        }
    }

    // Iridium: position + mission stats in Recovery (and Self Test for check)
    // Full RF switch cycle: GPS off -> Iridium on -> send -> Iridium off -> GPS on -> GPS reinit
    if (sysConfig.enableIridium && modemPtr && StateMachine_canTransmitIridium() &&
        (now - lastIridiumSend >= sysConfig.iridiumInterval)) {
        if (GPSManager_hasFix()) {
            GPSData gpsData = GPSManager_getData();
            MissionData mission;
            MissionData_get(&mission);
            currentLEDState = LED_STATE_IRIDIUM_TX;
            if (IridiumManager_sendMissionReport(&gpsData, &mission)) {
                lastIridiumSend = millis();
            }
            // GPS was power-cycled by antenna switch, must re-init
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
    pinMode(RELAY_POWER_MGMT, OUTPUT);
    pinMode(RELAY_TIMED_EVENT, OUTPUT);

    digitalWrite(BUS_VOLTAGE_MON_EN, HIGH);
    digitalWrite(LED_WHITE, LOW);
    digitalWrite(RELAY_POWER_MGMT, LOW);
    digitalWrite(RELAY_TIMED_EVENT, LOW);
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

    if (StateMachine_getState() == STATE_SELF_TEST && md.depth_m > MISSION_DEPTH_THRESHOLD_M) {
        StateMachine_enterMission();
    }
    if (StateMachine_getState() == STATE_MISSION) {
        if (md.depth_m < RECOVERY_DEPTH_THRESHOLD_M || GPSManager_hasFix()) {
            StateMachine_enterRecovery();
        }
    }
}

void checkFailsafe() {
    MissionData md;
    MissionData_get(&md);
    unsigned long now = millis();

    if (md.battery_voltage > 0 && md.battery_voltage < BATTERY_CRITICAL_VOLTAGE) {
        StateMachine_triggerFailsafe(FAILSAFE_LOW_VOLTAGE);
        return;
    }
    if (md.leak_detected) {
        StateMachine_triggerFailsafe(FAILSAFE_LEAK);
        return;
    }
    if (md.max_depth_m >= FAILSAFE_MAX_DEPTH_M) {
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
        Serial.println(F("release_now       Trigger release relay (failsafe)"));
        Serial.println(F("reset             Back to PRE_MISSION"));
        Serial.println(F("set_leak <0|1>   Set leak flag for testing"));
        Serial.println(F("config / save / set_* / enable_* / disable_*"));
        Serial.println(F("mesh_test / mesh_send <text>"));
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
