#include "modules/mavlink_interface.h"
#include "config.h"
#include <Arduino.h>

// Include MAVLink v2 headers
// Note: You may need to adjust the dialect based on your needs
#define MAVLINK_USE_CONVENIENCE_FUNCTIONS
#include <common/mavlink.h>

static bool initialized = false;
static uint8_t systemId = MAVLINK_SYSTEM_ID;
static uint8_t componentId = MAVLINK_COMPONENT_ID;
static unsigned long lastHeartbeat = 0;

void MAVLinkInterface_init() {
    // MAVLink communicates over USB Serial
    // Serial is already initialized in main setup
    Serial.println(F("MAVLink: Interface initialized"));
    initialized = true;
}

void MAVLinkInterface_sendGPS(GPSData* gpsData) {
    if (!initialized || !gpsData->valid) {
        return;
    }

    mavlink_message_t msg;
    uint8_t buf[MAVLINK_MAX_PACKET_LEN];

    // Convert GPS data to MAVLink GPS_RAW_INT message
    int32_t lat = (int32_t)(gpsData->latitude * 1e7);
    int32_t lon = (int32_t)(gpsData->longitude * 1e7);
    int32_t alt = (int32_t)(gpsData->altitude * 1000);  // mm
    uint16_t eph = (uint16_t)(gpsData->hdop * 100);     // cm
    uint16_t epv = 65535;  // Unknown
    uint16_t vel = (uint16_t)(gpsData->speed * 100);    // cm/s
    uint16_t cog = (uint16_t)(gpsData->course * 100);   // cdeg
    uint8_t fixType = gpsData->fixType;
    uint8_t satsVisible = gpsData->satellites;

    // Time since boot in microseconds
    uint64_t timeUsec = millis() * 1000ULL;

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
        0,  // alt_ellipsoid (not used)
        0,  // h_acc (not used)
        0,  // v_acc (not used)
        0,  // vel_acc (not used)
        0,  // hdg_acc (not used)
        0   // yaw (not used)
    );

    uint16_t len = mavlink_msg_to_send_buffer(buf, &msg);
    MAVLINK_SERIAL.write(buf, len);
}

void MAVLinkInterface_sendHeartbeat() {
    if (!initialized) {
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

void MAVLinkInterface_sendStatus(float voltage, float current) {
    if (!initialized) {
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
    int16_t voltages[10];  // Cell voltages (not available)
    for (int i = 0; i < 10; i++) {
        voltages[i] = -1;  // Not available
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

void MAVLinkInterface_update() {
    if (!initialized) {
        return;
    }

    // Process incoming MAVLink messages from autopilot
    mavlink_message_t msg;
    mavlink_status_t status;

    while (MAVLINK_SERIAL.available()) {
        uint8_t c = MAVLINK_SERIAL.read();

        if (mavlink_parse_char(MAVLINK_COMM_0, c, &msg, &status)) {
            // Handle received message
            switch (msg.msgid) {
                case MAVLINK_MSG_ID_HEARTBEAT: {
                    // Autopilot heartbeat received
                    Serial.println(F("MAVLink: Autopilot heartbeat received"));
                    break;
                }

                case MAVLINK_MSG_ID_COMMAND_LONG: {
                    mavlink_command_long_t cmd;
                    mavlink_msg_command_long_decode(&msg, &cmd);

                    // Handle commands from autopilot if needed
                    Serial.print(F("MAVLink: Command received: "));
                    Serial.println(cmd.command);
                    break;
                }

                default:
                    break;
            }
        }
    }
}
