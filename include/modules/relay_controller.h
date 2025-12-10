#ifndef RELAY_CONTROLLER_H
#define RELAY_CONTROLLER_H

#include <stdint.h>

// Initialize relay controller
void RelayController_init();

// Set power management relay (true = nonessentials ON, false = OFF)
void RelayController_setPowerManagement(bool state);

// Get power management relay state
bool RelayController_getPowerManagement();

// Trigger timed event relay for specified duration (milliseconds)
void RelayController_triggerTimedEvent(uint16_t durationMs);

// Check if timed event is currently active
bool RelayController_isTimedEventActive();

// Update relay controller (call in main loop to handle timed events)
void RelayController_update();

// Emergency disable all relays
void RelayController_emergencyDisable();

#endif // RELAY_CONTROLLER_H
