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

    // Autopilot health (from MAVLink HEARTBEAT + SYS_STATUS)
    uint8_t  autopilot_state;       // MAV_STATE from heartbeat (0 = not yet received)
    uint32_t sensor_enabled;        // SYS_STATUS onboard_control_sensors_enabled
    uint32_t sensor_health;         // SYS_STATUS onboard_control_sensors_health

    // Lua mission state from NAMED_VALUE_FLOAT "STATE"
    // -1=CONFIG, 0=MISSION_START, 1=DESCENT, 2=ON_BOTTOM, 3=ASCENT, 4=RECOVERY
    int      doris_state;
    unsigned long doris_state_ms;   // millis() when last received
    bool     doris_state_valid;     // true once we've received at least one update
};

void MissionData_init(void);
void MissionData_update_depth(float depth_m);
void MissionData_update_heartbeat(void);
void MissionData_update_voltage(float voltage);
void MissionData_update_autopilot_voltage(float voltage);
void MissionData_set_leak(bool leak);
void MissionData_update_autopilot_state(uint8_t mav_state);
void MissionData_update_sensor_health(uint32_t enabled, uint32_t health);
void MissionData_get(MissionData* out);

// True if autopilot is in CRITICAL or EMERGENCY state
bool MissionData_isAutopilotFailsafe(void);

// True if any enabled sensor is unhealthy or pre-arm checks are failing
bool MissionData_hasUnhealthySensors(void);

// True when Pi/Navigator heartbeat has been received recently
bool MissionData_isPiConnected(void);

// True if we've ever received a heartbeat (distinguishes "booting" from "lost connection")
bool MissionData_hasHadHeartbeat(void);

// Lua mission-ready flag (set via MAVLink COMMAND_LONG from Lua script)
void MissionData_setMissionReady(bool ready);
bool MissionData_isMissionReady(void);

// Lua mission state from NAMED_VALUE_FLOAT "STATE"
void MissionData_update_doris_state(int state);
int  MissionData_getDorisState(void);
bool MissionData_hasDorisState(void);

#endif // MISSION_DATA_H
