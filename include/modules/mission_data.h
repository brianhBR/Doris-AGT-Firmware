#ifndef MISSION_DATA_H
#define MISSION_DATA_H

#include <stdint.h>
#include <stdbool.h>

// Mission data from MAVLink / sensors for Iridium and failsafe
struct MissionData {
    float depth_m;           // Current depth (m), positive down
    float max_depth_m;       // Max depth this mission
    float battery_voltage;   // From MAVLink or PSM
    bool  leak_detected;     // From MAVLink SYS_STATUS or sensor
    unsigned long last_heartbeat_ms;  // Last autopilot heartbeat (millis)
    bool  heartbeat_valid;   // Have we ever received a heartbeat?
};

void MissionData_init(void);
void MissionData_update_depth(float depth_m);
void MissionData_update_heartbeat(void);
void MissionData_update_voltage(float voltage);
void MissionData_set_leak(bool leak);
void MissionData_get(MissionData* out);

#endif // MISSION_DATA_H
