#include "modules/mission_data.h"
#include "config.h"
#include <Arduino.h>
#include <math.h>

static MissionData data;
static bool missionReady = false;

void MissionData_init(void) {
    data.depth_m = 0;
    data.max_depth_m = 0;
    data.minimum_temperature_c = 0;
    data.battery_voltage = 0;
    data.depth_valid = false;
    data.temperature_valid = false;
    data.depth_ms = 0;
    data.voltage_from_autopilot = false;
    data.voltage_ms = 0;
    data.leak_detected = false;
    data.last_heartbeat_ms = 0;
    data.heartbeat_valid = false;
    data.autopilot_state = 0;
    data.armed = false;
    data.sensor_enabled = 0;
    data.sensor_health = 0;
    data.doris_state = -1;
    data.doris_state_ms = 0;
    data.doris_state_valid = false;
    data.recovery_message_count = 0;
    data.prearm_status = -1;
    missionReady = false;
}

void MissionData_update_depth(float depth_m) {
    if (!isfinite(depth_m)) {
        return;
    }
    if (depth_m < 0) {
        depth_m = 0;
    }
    data.depth_m = depth_m;
    data.depth_valid = true;
    data.depth_ms = millis();
    if (depth_m > data.max_depth_m) {
        data.max_depth_m = depth_m;
    }
}

void MissionData_update_minimum_temperature(float temperature_c) {
    // Lua uses 999 until the pressure sensor has produced a sample.
    if (!isfinite(temperature_c) ||
        temperature_c < -100.0f ||
        temperature_c > 100.0f) {
        return;
    }
    if (!data.temperature_valid ||
        temperature_c < data.minimum_temperature_c) {
        data.minimum_temperature_c = temperature_c;
    }
    data.temperature_valid = true;
}

void MissionData_update_heartbeat(void) {
    data.last_heartbeat_ms = millis();
    data.heartbeat_valid = true;
}

void MissionData_update_voltage(float voltage) {
    data.battery_voltage = voltage;
}

void MissionData_update_autopilot_voltage(float voltage) {
    if (!isfinite(voltage) || voltage <= 0.0f) {
        return;
    }
    data.battery_voltage = voltage;
    data.voltage_from_autopilot = true;
    data.voltage_ms = millis();
}

void MissionData_set_leak(bool leak) {
    data.leak_detected = leak;
}

void MissionData_update_autopilot_state(uint8_t mav_state, uint8_t base_mode) {
    data.autopilot_state = mav_state;
    data.armed = (base_mode & 0x80) != 0;  // MAV_MODE_FLAG_SAFETY_ARMED = 128
}

bool MissionData_isArmed(void) {
    return data.armed;
}

void MissionData_update_sensor_health(uint32_t enabled, uint32_t health) {
    data.sensor_enabled = enabled;
    data.sensor_health = health;
}

bool MissionData_isAutopilotFailsafe(void) {
    // MAV_STATE_CRITICAL = 5, MAV_STATE_EMERGENCY = 6
    return data.autopilot_state >= 5;
}

bool MissionData_hasUnhealthySensors(void) {
    if (data.sensor_enabled == 0) return false;
    // Any enabled sensor that isn't healthy
    return (data.sensor_enabled & ~data.sensor_health) != 0;
}

void MissionData_get(MissionData* out) {
    if (out) *out = data;
}

bool MissionData_isPiConnected(void) {
    if (!data.heartbeat_valid) return false;
    return (millis() - data.last_heartbeat_ms) < PI_HEARTBEAT_TIMEOUT_MS;
}

bool MissionData_hasHadHeartbeat(void) {
    return data.heartbeat_valid;
}

void MissionData_setMissionReady(bool ready) {
    missionReady = ready;
}

bool MissionData_isMissionReady(void) {
    return missionReady && MissionData_isPiConnected();
}

void MissionData_update_doris_state(int state) {
    bool completedMissionReset =
        state <= 0 &&
        data.doris_state_valid &&
        data.doris_state > 0;
    if (completedMissionReset) {
        data.max_depth_m = 0.0f;
        data.minimum_temperature_c = 0.0f;
        data.temperature_valid = false;
    }

    if (state == 4) {
        bool sequenceFresh = data.doris_state_valid &&
                             data.doris_state == 4 &&
                             millis() - data.doris_state_ms <=
                                 MISSION_DATA_FRESHNESS_MS;
        if (!sequenceFresh) {
            data.recovery_message_count = 1;
        } else if (data.recovery_message_count < UINT8_MAX) {
            data.recovery_message_count++;
        }
    } else {
        data.recovery_message_count = 0;
    }
    data.doris_state = state;
    data.doris_state_ms = millis();
    data.doris_state_valid = true;
}

int MissionData_getDorisState(void) {
    return data.doris_state;
}

bool MissionData_hasDorisState(void) {
    return data.doris_state_valid;
}

bool MissionData_isDepthFresh(void) {
    return data.depth_valid &&
           (millis() - data.depth_ms) <= MISSION_DATA_FRESHNESS_MS;
}

bool MissionData_isDorisStateFresh(void) {
    return data.doris_state_valid &&
           (millis() - data.doris_state_ms) <= MISSION_DATA_FRESHNESS_MS;
}

bool MissionData_isAutopilotVoltageFresh(void) {
    return data.voltage_from_autopilot &&
           (millis() - data.voltage_ms) <= MISSION_DATA_FRESHNESS_MS;
}

uint8_t MissionData_getRecoveryMessageCount(void) {
    return data.recovery_message_count;
}

void MissionData_update_prearm_status(int status) {
    data.prearm_status = status;
}

int MissionData_getPrearmStatus(void) {
    return data.prearm_status;
}
