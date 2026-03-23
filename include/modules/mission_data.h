#ifndef MISSION_DATA_H
#define MISSION_DATA_H

#include <stdint.h>
#include <stdbool.h>

// Mission data from MAVLink / sensors for Iridium and failsafe
struct MissionData {
    float depth_m;           // Current depth (m), positive down
    float max_depth_m;       // Max depth this mission
    float battery_voltage;   // Primary: autopilot via MAVLink, fallback: PSM
    bool  depth_valid;       // True once we've received at least one depth reading from autopilot
    bool  voltage_from_autopilot;  // True if battery_voltage came from autopilot via MAVLink
    bool  leak_detected;     // From MAVLink SYS_STATUS or sensor
    unsigned long last_heartbeat_ms;  // Last autopilot heartbeat (millis)
    bool  heartbeat_valid;   // Have we ever received a heartbeat?
};

void MissionData_init(void);
void MissionData_update_depth(float depth_m);
void MissionData_update_heartbeat(void);
void MissionData_update_voltage(float voltage);
void MissionData_update_autopilot_voltage(float voltage);
void MissionData_set_leak(bool leak);
void MissionData_get(MissionData* out);

// True when Pi/Navigator heartbeat has been received recently
bool MissionData_isPiConnected(void);

#endif // MISSION_DATA_H
