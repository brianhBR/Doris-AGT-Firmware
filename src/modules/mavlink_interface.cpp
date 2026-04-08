#include "modules/mavlink_interface.h"
#include "modules/mission_data.h"
#include "modules/neopixel_controller.h"
#include "config.h"
#include <Arduino.h>

extern bool iridiumTestRequested;

// Platform glue for MAVLink (maps MAVLink UART helpers to our Serial)
#include "mavlink_platform.h"

// Include MAVLink v2 headers
#include <common/mavlink.h>

static bool initialized = false;
static uint8_t systemId = MAVLINK_SYSTEM_ID;
static uint8_t componentId = MAVLINK_COMPONENT_ID;
static unsigned long lastHeartbeat = 0;

// Depth comes from the autopilot EKF (VFR_HUD.alt and GLOBAL_POSITION_INT.relative_alt).
// No raw pressure conversion needed — ArduSub's EKF handles surface calibration.

void MAVLinkInterface_init() {
    // MAVLink communicates over USB Serial
    // Serial is already initialized in main setup
    DebugPrintln(F("MAVLink: Interface initialized"));
    // Populate mavlink_system with configured IDs so MAVLink helpers use them
    mavlink_system.sysid = MAVLINK_SYSTEM_ID;
    mavlink_system.compid = MAVLINK_COMPONENT_ID;
    initialized = true;
}

static void calendarToGPSTime(uint16_t year, uint8_t month, uint8_t day,
                              uint8_t hour, uint8_t minute, uint8_t second,
                              uint16_t* gpsWeek, uint32_t* gpsTowMs) {
    // GPS epoch: 1980-01-06 00:00:00 UTC
    // Julian Day Number for a given date
    auto jdn = [](int y, int m, int d) -> int32_t {
        int a = (14 - m) / 12;
        int y2 = y + 4800 - a;
        int m2 = m + 12 * a - 3;
        return d + (153 * m2 + 2) / 5 + 365 * y2 + y2 / 4 - y2 / 100 + y2 / 400 - 32045;
    };
    int32_t jdnDate  = jdn(year, month, day);
    int32_t jdnEpoch = jdn(1980, 1, 6);
    int32_t daysSinceEpoch = jdnDate - jdnEpoch;
    if (daysSinceEpoch < 0) {
        *gpsWeek = 0;
        *gpsTowMs = 0;
        return;
    }
    *gpsWeek  = (uint16_t)(daysSinceEpoch / 7);
    uint32_t dow = daysSinceEpoch % 7;
    *gpsTowMs = (dow * 86400UL + (uint32_t)hour * 3600UL +
                 (uint32_t)minute * 60UL + second) * 1000UL;
}

void MAVLinkInterface_sendGPS(GPSData* gpsData) {
    if (!initialized || !MAVLINK_SERIAL) return;

    mavlink_message_t msg;
    uint8_t buf[MAVLINK_MAX_PACKET_LEN];

    int32_t lat = (int32_t)(gpsData->latitude * 1e7);
    int32_t lon = (int32_t)(gpsData->longitude * 1e7);
    int32_t alt = (int32_t)(gpsData->altitude * 1000);  // mm
    uint16_t eph = (uint16_t)(gpsData->hdop * 100);     // pDOP * 100
    uint16_t epv = eph;                                  // pDOP as conservative proxy for VDOP
    uint16_t vel = (uint16_t)(gpsData->speed * 100);    // cm/s
    uint16_t cog = (uint16_t)(gpsData->course * 100);   // cdeg
    uint8_t satsVisible = gpsData->satellites;
    uint64_t timeUsec = millis() * 1000ULL;

    // Map u-blox fixType for ArduPilot: 0=NO_GPS means "not connected" in
    // ArduPilot, but u-blox 0 just means "no fix yet." Use 1 (NO_FIX) instead
    // so the GPS driver stays alive.
    uint8_t fixType = gpsData->fixType;
    if (fixType == 0) fixType = 1;

    // GPS_RAW_INT always sent so autopilot sees sensor status + sat count
    mavlink_msg_gps_raw_int_pack(
        systemId,
        componentId,
        &msg,
        timeUsec,
        fixType,
        lat,
        lon,
        alt,
        eph,
        epv,
        vel,
        cog,
        satsVisible,
        gpsData->alt_ellipsoid,     // alt_ellipsoid (mm)
        gpsData->h_acc_mm,          // h_acc (mm)
        gpsData->v_acc_mm,          // v_acc (mm)
        gpsData->s_acc_mm,          // vel_acc (mm/s)
        0,                          // hdg_acc
        0                           // yaw
    );
    uint16_t len = mavlink_msg_to_send_buffer(buf, &msg);
    MAVLINK_SERIAL.write(buf, len);

    // GPS_INPUT always sent so AP_GPS_MAV driver stays healthy (avoids
    // 4-second timeout that triggers "GPS1 not healthy").  ArduPilot uses
    // fixType to decide whether the position is usable.
    {
        uint16_t gpsWeek = 0;
        uint32_t gpsTowMs = 0;
        if (gpsData->year >= 1980) {
            calendarToGPSTime(gpsData->year, gpsData->month, gpsData->day,
                              gpsData->hour, gpsData->minute, gpsData->second,
                              &gpsWeek, &gpsTowMs);
        }

        uint16_t ignoreFlags = GPS_INPUT_IGNORE_FLAG_VDOP |
                               GPS_INPUT_IGNORE_FLAG_VEL_VERT;

        float horizAcc = gpsData->h_acc_mm / 1000.0f;  // mm → m
        float vertAcc  = gpsData->v_acc_mm / 1000.0f;
        float speedAcc = gpsData->s_acc_mm / 1000.0f;

        mavlink_msg_gps_input_pack(
            systemId,
            componentId,
            &msg,
            timeUsec,           // time_usec
            0,                  // gps_id
            ignoreFlags,        // ignore_flags
            gpsTowMs,           // time_week_ms
            gpsWeek,            // time_week
            fixType,            // fix_type
            lat,                // lat (1e7)
            lon,                // lon (1e7)
            gpsData->altitude,  // alt (m, float)
            gpsData->hdop,      // hdop (float)
            0.0f,               // vdop (ignored)
            gpsData->speed,     // vn (m/s) — ground speed as proxy
            0.0f,               // ve
            0.0f,               // vd (ignored)
            speedAcc,           // speed_accuracy (m/s)
            horizAcc,           // horiz_accuracy (m)
            vertAcc,            // vert_accuracy (m)
            satsVisible,        // satellites_visible
            0                   // yaw (cdeg, 0 = not available)
        );
        len = mavlink_msg_to_send_buffer(buf, &msg);
        MAVLINK_SERIAL.write(buf, len);
    }

}

void MAVLinkInterface_sendHeartbeat() {
    if (!initialized || !MAVLINK_SERIAL) {
        return;
    }

    unsigned long currentMillis = millis();
    if (currentMillis - lastHeartbeat < MAVLINK_HEARTBEAT_MS) {
        return;
    }

    mavlink_message_t msg;
    uint8_t buf[MAVLINK_MAX_PACKET_LEN];

    mavlink_msg_heartbeat_pack(
        systemId,
        componentId,
        &msg,
        MAV_TYPE_ONBOARD_CONTROLLER,  // type
        MAV_AUTOPILOT_INVALID,         // autopilot
        MAV_MODE_FLAG_CUSTOM_MODE_ENABLED,  // base_mode
        0,                             // custom_mode
        MAV_STATE_ACTIVE               // system_status
    );

    uint16_t len = mavlink_msg_to_send_buffer(buf, &msg);
    MAVLINK_SERIAL.write(buf, len);

    lastHeartbeat = currentMillis;
}

void MAVLinkInterface_sendSystemTime(uint64_t time_unix_usec) {
    if (!initialized || !MAVLINK_SERIAL) return;
    mavlink_message_t msg;
    uint8_t buf[MAVLINK_MAX_PACKET_LEN];
    uint32_t time_boot_ms = (uint32_t)millis();
    mavlink_msg_system_time_pack(
        systemId,
        componentId,
        &msg,
        time_unix_usec,
        time_boot_ms
    );
    uint16_t len = mavlink_msg_to_send_buffer(buf, &msg);
    MAVLINK_SERIAL.write(buf, len);
}

void MAVLinkInterface_sendStatus(float voltage, float current) {
    if (!initialized || !MAVLINK_SERIAL) {
        return;
    }

    mavlink_message_t msg;
    uint8_t buf[MAVLINK_MAX_PACKET_LEN];

    // Convert to MAVLink units
    int16_t voltage_mV = (int16_t)(voltage * 1000);
    int16_t current_cA = (int16_t)(current * 100);

    // Calculate battery remaining (simplified)
    // Assumes 4S LiPo: 14.8V full, 11.0V empty
    float batteryPercent = ((voltage - BATTERY_CRITICAL_VOLTAGE) /
                           (BATTERY_FULL_VOLTAGE - BATTERY_CRITICAL_VOLTAGE)) * 100;
    if (batteryPercent < 0) batteryPercent = 0;
    if (batteryPercent > 100) batteryPercent = 100;

    int8_t batteryRemaining = (int8_t)batteryPercent;

    // Battery status
    uint16_t voltages[10];  // Cell voltages (not available)
    for (int i = 0; i < 10; i++) {
        voltages[i] = 0xFFFF;  // Not available indicator for MAVLink
    }

    mavlink_msg_battery_status_pack(
        systemId,
        componentId,
        &msg,
        0,                           // id
        MAV_BATTERY_FUNCTION_ALL,    // battery_function
        MAV_BATTERY_TYPE_LIPO,       // type
        2500,                        // temperature (not available, 25.0°C)
        voltages,                    // voltages
        current_cA,                  // current_battery
        -1,                          // current_consumed (not tracked)
        -1,                          // energy_consumed (not tracked)
        batteryRemaining,            // battery_remaining
        0,                           // time_remaining (not calculated)
        MAV_BATTERY_CHARGE_STATE_OK, // charge_state
        nullptr,                     // voltages_ext (not used)
        MAV_BATTERY_MODE_UNKNOWN,    // mode
        0                            // fault_bitmask
    );

    uint16_t len = mavlink_msg_to_send_buffer(buf, &msg);
    MAVLINK_SERIAL.write(buf, len);
}

void MAVLinkInterface_sendStatusText(uint8_t severity, const char* text) {
    if (!initialized || !MAVLINK_SERIAL) return;
    mavlink_message_t msg;
    uint8_t buf[MAVLINK_MAX_PACKET_LEN];
    char truncated[50];
    strncpy(truncated, text, sizeof(truncated) - 1);
    truncated[sizeof(truncated) - 1] = '\0';
    mavlink_msg_statustext_pack(
        systemId, componentId, &msg,
        severity, truncated, 0, 0
    );
    uint16_t len = mavlink_msg_to_send_buffer(buf, &msg);
    MAVLINK_SERIAL.write(buf, len);
}

void MAVLinkInterface_handleMessage(void* msgPtr) {
    if (!initialized || !msgPtr) return;

    mavlink_message_t* msg = (mavlink_message_t*)msgPtr;

    switch (msg->msgid) {
        case MAVLINK_MSG_ID_HEARTBEAT: {
            mavlink_heartbeat_t hb;
            mavlink_msg_heartbeat_decode(msg, &hb);
            MissionData_update_heartbeat();
            MissionData_update_autopilot_state(hb.system_status);
            break;
        }

        case MAVLINK_MSG_ID_SYS_STATUS: {
            mavlink_sys_status_t sys;
            mavlink_msg_sys_status_decode(msg, &sys);
            if (sys.voltage_battery != 65535) {
                MissionData_update_autopilot_voltage(sys.voltage_battery / 1000.0f);
            }
            MissionData_update_sensor_health(
                sys.onboard_control_sensors_enabled,
                sys.onboard_control_sensors_health);
            break;
        }

        case MAVLINK_MSG_ID_VFR_HUD: {
            mavlink_vfr_hud_t vfr;
            mavlink_msg_vfr_hud_decode(msg, &vfr);
            // ArduSub EKF altitude: negative = below surface
            MissionData_update_depth(-vfr.alt);
            break;
        }

        case MAVLINK_MSG_ID_GLOBAL_POSITION_INT: {
            mavlink_global_position_int_t gpi;
            mavlink_msg_global_position_int_decode(msg, &gpi);
            // relative_alt is mm relative to home (surface); negative = underwater
            MissionData_update_depth(-gpi.relative_alt / 1000.0f);
            break;
        }

        case MAVLINK_MSG_ID_BATTERY_STATUS: {
            mavlink_battery_status_t bat;
            mavlink_msg_battery_status_decode(msg, &bat);
            if (bat.voltages[0] != 65535) {
                MissionData_update_autopilot_voltage(bat.voltages[0] / 1000.0f);
            }
            break;
        }

        case MAVLINK_MSG_ID_NAMED_VALUE_FLOAT: {
            mavlink_named_value_float_t nv;
            mavlink_msg_named_value_float_decode(msg, &nv);
            if (strncmp(nv.name, "DORIS_ST", 8) == 0) {
                MissionData_update_doris_state((int)nv.value);
            }
            break;
        }

        case MAVLINK_MSG_ID_COMMAND_LONG: {
            mavlink_command_long_t cmd;
            mavlink_msg_command_long_decode(msg, &cmd);

            if (cmd.command == MAVLINK_CMD_LED_CONTROL) {
                uint8_t pattern   = (uint8_t)cmd.param1;
                uint32_t color    = (uint32_t)cmd.param2;
                uint16_t speedMs  = (uint16_t)cmd.param3;
                uint8_t bright    = (uint8_t)cmd.param4;
                NeoPixelController_setLuaCommand(pattern, color, speedMs, bright);

                mavlink_message_t ack;
                uint8_t buf[MAVLINK_MAX_PACKET_LEN];
                mavlink_msg_command_ack_pack(systemId, componentId, &ack,
                    cmd.command, MAV_RESULT_ACCEPTED, 0, 0,
                    msg->sysid, msg->compid);
                uint16_t ackLen = mavlink_msg_to_send_buffer(buf, &ack);
                MAVLINK_SERIAL.write(buf, ackLen);
            }
            else if (cmd.command == MAVLINK_CMD_MISSION_STATUS) {
                MissionData_setMissionReady(cmd.param1 > 0.5f);

                mavlink_message_t ack;
                uint8_t buf[MAVLINK_MAX_PACKET_LEN];
                mavlink_msg_command_ack_pack(systemId, componentId, &ack,
                    cmd.command, MAV_RESULT_ACCEPTED, 0, 0,
                    msg->sysid, msg->compid);
                uint16_t ackLen = mavlink_msg_to_send_buffer(buf, &ack);
                MAVLINK_SERIAL.write(buf, ackLen);
            }
            else if (cmd.command == MAVLINK_CMD_GPS_DIAG) {
                GPSManager_printDiagnostics();

                mavlink_message_t ack;
                uint8_t buf[MAVLINK_MAX_PACKET_LEN];
                mavlink_msg_command_ack_pack(systemId, componentId, &ack,
                    cmd.command, MAV_RESULT_ACCEPTED, 0, 0,
                    msg->sysid, msg->compid);
                uint16_t ackLen = mavlink_msg_to_send_buffer(buf, &ack);
                MAVLINK_SERIAL.write(buf, ackLen);
            }
            else if (cmd.command == MAVLINK_CMD_IRIDIUM_TEST) {
                iridiumTestRequested = true;

                mavlink_message_t ack;
                uint8_t buf[MAVLINK_MAX_PACKET_LEN];
                mavlink_msg_command_ack_pack(systemId, componentId, &ack,
                    cmd.command, MAV_RESULT_ACCEPTED, 0, 0,
                    msg->sysid, msg->compid);
                uint16_t ackLen = mavlink_msg_to_send_buffer(buf, &ack);
                MAVLINK_SERIAL.write(buf, ackLen);
            }
            break;
        }

        default:
            break;
    }
}

void MAVLinkInterface_update() {
    if (!initialized) return;

    mavlink_message_t msg;
    mavlink_status_t status;

    while (MAVLINK_SERIAL.available()) {
        uint8_t c = MAVLINK_SERIAL.read();
        if (mavlink_parse_char(MAVLINK_COMM_0, c, &msg, &status)) {
            MAVLinkInterface_handleMessage(&msg);
        }
    }
}
