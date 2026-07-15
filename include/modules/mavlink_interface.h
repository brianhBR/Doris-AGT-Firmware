#ifndef MAVLINK_INTERFACE_H
#define MAVLINK_INTERFACE_H

#include "gps_manager.h"

// Initialize MAVLink interface
void MAVLinkInterface_init();

// Send GPS position to autopilot
void MAVLinkInterface_sendGPS(GPSData* gpsData, uint64_t rtcTimeUsec);

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

// Send STATUSTEXT visible in BlueOS/QGC message panel (max 50 chars)
// severity: 0=EMERGENCY..6=INFO..7=DEBUG
void MAVLinkInterface_sendStatusText(uint8_t severity, const char* text);

// Send the firmware version (FIRMWARE_VERSION) as a STATUSTEXT so it lands in
// the MAVLink telemetry logs. Also emits the RockBLOCK IMEI on a second line
// once it has been cached. Called at boot and by MAVLinkInterface_sendDebug().
void MAVLinkInterface_sendVersion();

// Dump AGT debug info as STATUSTEXT: firmware version, RockBLOCK IMEI, and the
// GPS diagnostics. Triggered on demand via MAVLINK_CMD_AGT_DEBUG.
void MAVLinkInterface_sendDebug();

// Service the MAVLink/USB link during a long blocking operation. Drains (and
// discards) inbound bytes so the UART RX buffer can't overflow and wedge the
// receiver, and emits a throttled heartbeat. Commands are intentionally NOT
// dispatched here (see note in the .cpp). Call this frequently (<~50 ms apart)
// from any code that blocks the main loop, e.g. the Iridium SBD path.
void MAVLinkInterface_serviceLink();

// Blocking delay of `ms` milliseconds that keeps the link serviced throughout
// (calls MAVLinkInterface_serviceLink() every few ms). Use in place of delay()
// inside long Iridium/GPS operations.
void MAVLinkInterface_serviceDelay(unsigned long ms);

#endif // MAVLINK_INTERFACE_H
