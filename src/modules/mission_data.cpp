#include "modules/mission_data.h"
#include "config.h"
#include <Arduino.h>

static MissionData data;

void MissionData_init(void) {
    data.depth_m = 0;
    data.max_depth_m = 0;
    data.battery_voltage = 0;
    data.leak_detected = false;
    data.last_heartbeat_ms = 0;
    data.heartbeat_valid = false;
}

void MissionData_update_depth(float depth_m) {
    if (depth_m < 0) depth_m = 0;
    data.depth_m = depth_m;
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

void MissionData_set_leak(bool leak) {
    data.leak_detected = leak;
}

void MissionData_get(MissionData* out) {
    if (out) *out = data;
}
