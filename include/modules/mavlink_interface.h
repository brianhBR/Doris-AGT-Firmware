#ifndef MAVLINK_INTERFACE_H
#define MAVLINK_INTERFACE_H

#include "gps_manager.h"

// Initialize MAVLink interface
void MAVLinkInterface_init();

// Send GPS position to autopilot
void MAVLinkInterface_sendGPS(GPSData* gpsData);

// Send heartbeat
void MAVLinkInterface_sendHeartbeat();

// Send system status
void MAVLinkInterface_sendStatus(float voltage, float current);

// Process incoming MAVLink messages (reads from MAVLINK_SERIAL)
void MAVLinkInterface_update();

// Handle a single already-parsed MAVLink message (used by unified serial router)
void MAVLinkInterface_handleMessage(void* msg);

// Send SYSTEM_TIME so ArduSub can set its clock from Artemis RTC (set BRD_RTC_TYPES=2 on autopilot)
void MAVLinkInterface_sendSystemTime(uint64_t time_unix_usec);

#endif // MAVLINK_INTERFACE_H
