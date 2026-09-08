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

// Repeated named-float status visible to BlueOS:
// AGT_CAP (capability mask) and PWR_SHDN (graceful shutdown request).
void MAVLinkInterface_sendSafetyStatus();

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

// Dump AGT debug info as STATUSTEXT: firmware version, RockBLOCK IMEI,
// Iridium enable/interval, and GPS diagnostics. Triggered on demand via
// MAVLINK_CMD_AGT_DEBUG. Do not call from ISBDCallback (GPS I2C / antenna).
void MAVLinkInterface_sendDebug();

// Service the MAVLink/USB link during a long blocking operation (Iridium SBD).
// Parses inbound MAVLink so PWR_ACK/STATE can complete the shutdown handshake,
// emits heartbeat + PWR_SHDN (existing 1 s cadence), and advances payload-power
// timers. COMMAND_LONG is denied except LED_CONTROL until the session ends.
void MAVLinkInterface_serviceLink();

// Blocking delay of `ms` milliseconds that keeps the link serviced throughout
// (calls MAVLinkInterface_serviceLink() every few ms). Use in place of delay()
// inside long Iridium/GPS operations.
void MAVLinkInterface_serviceDelay(unsigned long ms);

#endif // MAVLINK_INTERFACE_H
