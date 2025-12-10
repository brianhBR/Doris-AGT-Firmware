#ifndef PSM_INTERFACE_H
#define PSM_INTERFACE_H

// Battery data structure
struct BatteryData {
    float voltage;      // Volts
    float current;      // Amps
    float power;        // Watts
    float energy;       // Watt-hours (accumulated)
    float capacity;     // Amp-hours (accumulated)
    bool valid;
};

// Initialize Blue Robotics PSM interface
bool PSMInterface_init();

// Update battery readings (call in main loop)
void PSMInterface_update();

// Get current battery data
BatteryData PSMInterface_getData();

// Reset accumulated energy/capacity counters
void PSMInterface_resetCounters();

// Get battery state of charge percentage (0-100)
float PSMInterface_getSOC();

#endif // PSM_INTERFACE_H
