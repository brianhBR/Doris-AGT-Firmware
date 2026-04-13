#include "modules/mission_data.h"
#include "config.h"
#include <Arduino.h>

static MissionData data;
static bool missionReady = false;

void MissionData_init(void) {
    data.depth_m = 0;
    data.max_depth_m = 0;
    data.battery_voltage = 0;
    data.depth_valid = false;
    data.voltage_from_autopilot = false;
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
    missionReady = false;
}

void MissionData_update_depth(float depth_m) {
    if (depth_m < 0) depth_m = 0;
    data.depth_m = depth_m;
    data.depth_valid = true;
    if (depth_m > data.max_depth_m) {
        data.max_depth_m = depth_m;
    }
}

void MissionData_update_heartbeat(void) {
    data.last_heartbeat_ms = millis();
    data.heartbeat_valid = true;
}

void MissionData_update_voltage(float voltage) {
    data.battery_voltage = voltage;
}

void MissionData_update_autopilot_voltage(float voltage) {
    data.battery_voltage = voltage;
    data.voltage_from_autopilot = true;
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
