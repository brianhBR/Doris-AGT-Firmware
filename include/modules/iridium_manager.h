#ifndef IRIDIUM_MANAGER_H
#define IRIDIUM_MANAGER_H

#include <IridiumSBD.h>
#include "gps_manager.h"
#include "psm_interface.h"

// Initialize Iridium modem
bool IridiumManager_init(IridiumSBD* modem);

// Send position report via Iridium
bool IridiumManager_sendPosition(GPSData* gpsData, BatteryData* battData);

// Send custom message
bool IridiumManager_sendMessage(const char* message);

// Send binary message
bool IridiumManager_sendBinary(uint8_t* data, size_t length);

// Check for incoming messages
bool IridiumManager_checkMessages(char* buffer, size_t* bufferSize);

// Get signal quality (0-5)
int IridiumManager_getSignalQuality();

// Power management
void IridiumManager_sleep();
void IridiumManager_wake();

// Check if modem is ready
bool IridiumManager_isReady();

#endif // IRIDIUM_MANAGER_H
