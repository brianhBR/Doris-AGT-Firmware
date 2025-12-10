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

// Process incoming MAVLink messages
void MAVLinkInterface_update();

#endif // MAVLINK_INTERFACE_H
