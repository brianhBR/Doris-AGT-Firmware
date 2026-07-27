#ifndef RELAY_CONTROLLER_H
#define RELAY_CONTROLLER_H

#include <stdint.h>

// Initialize relay controller
void RelayController_init();

// Set power management relay (true = nonessentials ON, false = OFF)
void RelayController_setPowerManagement(bool state);

// Get power management relay state
bool RelayController_getPowerManagement();

// Trigger timed event relay for specified duration (seconds)
void RelayController_triggerTimedEvent(uint32_t durationSeconds);

// Check if timed event is currently active
bool RelayController_isTimedEventActive();

// Guarded release output. ON is latched and persisted; repeated ON requests
// are harmless. OFF is accepted only after the minimum hold and an independently
// supplied surface-safe decision.
void RelayController_requestRelease();
bool RelayController_requestReleaseOff(bool surfaceSafe);
bool RelayController_isReleaseActive();

// Update relay controller (call in main loop to handle timed events)
void RelayController_update();

// Emergency disable all relays
void RelayController_emergencyDisable();

#endif // RELAY_CONTROLLER_H
