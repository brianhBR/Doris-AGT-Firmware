#ifndef RELAY_CONTROLLER_H
#define RELAY_CONTROLLER_H

// Initialize the GPIO4 payload-power relay controller.
void RelayController_init();

// Set power management relay (true = nonessentials ON, false = OFF)
void RelayController_setPowerManagement(bool state);

// Get power management relay state
bool RelayController_getPowerManagement();

#endif // RELAY_CONTROLLER_H
