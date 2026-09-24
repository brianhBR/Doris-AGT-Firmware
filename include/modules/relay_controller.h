#ifndef RELAY_CONTROLLER_H
#define RELAY_CONTROLLER_H

// Initialize payload power and command it on.
// Default build: GPIO4 NC relay. PAYLOAD_POWER_POLULU: pulse Pololu ON.
void RelayController_init();

// true = nonessentials powered, false = nonessentials off.
// Pololu mode pulses ON or OFF once per logical change. There is no
// hardware readback; the getter is the last commanded state.
void RelayController_setPowerManagement(bool state);

// Last commanded payload-power state.
bool RelayController_getPowerManagement();

#endif // RELAY_CONTROLLER_H
