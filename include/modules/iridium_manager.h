#ifndef IRIDIUM_MANAGER_H
#define IRIDIUM_MANAGER_H

#include <IridiumSBD.h>
#include "gps_manager.h"
#include "psm_interface.h"
#include "mission_data.h"
#include "doris_protocol.h"

// Configure Iridium pins and serial (does NOT power on modem)
void IridiumManager_configure(IridiumSBD* modem);

// Full init with power-on test (handles antenna switch)
bool IridiumManager_init(IridiumSBD* modem);

// Send position report (legacy text format)
bool IridiumManager_sendPosition(GPSData* gpsData, BatteryData* battData);

// Send position + mission stats (legacy text format)
bool IridiumManager_sendMissionReport(GPSData* gpsData, MissionData* mission);

// Send Doris binary telemetry report and check for MT commands.
// Populates mtMsgId, mtConfig, and mtCommand if an MT message was received.
// Returns true if the MO report was sent successfully.
bool IridiumManager_sendDorisReport(const DorisReport* report,
                                     uint8_t* mtMsgId,
                                     DorisConfig* mtConfig,
                                     DorisCommand* mtCommand);

// Send custom message
bool IridiumManager_sendMessage(const char* message);

// Send binary message
bool IridiumManager_sendBinary(uint8_t* data, size_t length);

// Check for incoming MT messages (mailbox check without sending MO)
bool IridiumManager_checkMT(uint8_t* mtMsgId,
                             DorisConfig* mtConfig,
                             DorisCommand* mtCommand);

// Get signal quality (0-5)
int IridiumManager_getSignalQuality();

// Power management
void IridiumManager_sleep();
void IridiumManager_wake();

// Check if modem is ready
bool IridiumManager_isReady();

#endif // IRIDIUM_MANAGER_H
