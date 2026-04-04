#ifndef DORIS_PROTOCOL_H
#define DORIS_PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

// ============================================================================
// DORIS IRIDIUM SBD BINARY PROTOCOL
// ============================================================================
// Compatible with SolarSurfer2 message framing:
//   Byte 0:    '$' (0x24) — start marker
//   Byte 1:    message ID
//   Byte 2..N: little-endian packed payload
//
// MO (Mobile Originated, device -> cloud): max 340 bytes
// MT (Mobile Terminated, cloud -> device): max 270 bytes
// ============================================================================

#define DORIS_MSG_START          '$'

// Message IDs — avoid 1-4 (used by SolarSurfer2)
#define DORIS_MSG_ID_REPORT      5    // MO: telemetry report
#define DORIS_MSG_ID_CONFIG      6    // MT: configuration update
#define DORIS_MSG_ID_COMMAND     7    // MT: immediate command
#define DORIS_MSG_ID_ACK         8    // MO: acknowledge MT message

#define DORIS_MSG_HEADER_SIZE    2

// ============================================================================
// MO: Telemetry Report (ID 5) — 38 bytes payload, 40 bytes on wire
// ============================================================================
// Sent periodically via Iridium. Contains GPS, mission, and power data.
// Backend identifies the unit via the RockBlock IMEI in the webhook payload.

enum DorisMissionState : uint8_t {
    DORIS_STATE_PRE_DIVE  = 0,
    DORIS_STATE_DIVING    = 1,
    DORIS_STATE_RECOVERY  = 2,
    DORIS_STATE_FAILSAFE  = 3,
};

#define DORIS_FAILSAFE_LOW_VOLTAGE     (1 << 0)
#define DORIS_FAILSAFE_CRITICAL_VOLTAGE (1 << 1)
#define DORIS_FAILSAFE_LEAK            (1 << 2)
#define DORIS_FAILSAFE_NO_HEARTBEAT    (1 << 3)
#define DORIS_FAILSAFE_MANUAL          (1 << 4)

#pragma pack(push, 1)
typedef struct {
    uint8_t  mission_state;     // DorisMissionState enum
    uint8_t  gps_fix_type;      // 0=no GPS, 1=no fix, 2=2D, 3=3D
    uint8_t  satellites;
    uint8_t  failsafe_flags;    // DORIS_FAILSAFE_* bitfield
    float    latitude;          // degrees
    float    longitude;         // degrees
    int16_t  altitude;          // meters (signed)
    uint16_t speed;             // cm/s
    uint16_t course;            // degrees * 100
    uint16_t hdop;              // hdop * 100
    uint16_t depth;             // decimeters (0.1m resolution, max 6553.5m)
    uint16_t max_depth;         // decimeters
    uint16_t battery_voltage;   // millivolts
    uint16_t battery_current;   // milliamps
    uint16_t bus_voltage;       // millivolts (AGT bus/supercap supply)
    uint8_t  leak_detected;     // 0 or 1
    uint8_t  reserved;          // alignment / future use
    uint32_t time_unix;         // UTC unix seconds (from GPS-synced RTC)
    uint32_t uptime_s;          // seconds since boot
} DorisReport;
#pragma pack(pop)

#define DORIS_REPORT_WIRE_SIZE (DORIS_MSG_HEADER_SIZE + sizeof(DorisReport))

// ============================================================================
// MT: Configuration Update (ID 6) — sent from cloud to device
// ============================================================================
// Zero values mean "no change" for that field.

enum DorisLedMode : uint8_t {
    DORIS_LED_NO_CHANGE = 0,
    DORIS_LED_OFF       = 1,
    DORIS_LED_NORMAL    = 2,
    DORIS_LED_STROBE    = 3,
};

#pragma pack(push, 1)
typedef struct {
    uint16_t iridium_interval_s;    // 0 = no change; seconds between reports
    uint8_t  led_mode;              // DorisLedMode
    uint8_t  neopixel_brightness;   // 0 = no change; 1-255
    uint16_t power_save_voltage_mv; // 0 = no change; millivolts
    uint16_t reserved;              // future use
} DorisConfig;
#pragma pack(pop)

#define DORIS_CONFIG_WIRE_SIZE (DORIS_MSG_HEADER_SIZE + sizeof(DorisConfig))

// ============================================================================
// MT: Immediate Command (ID 7) — sent from cloud to device
// ============================================================================

enum DorisCommandType : uint8_t {
    DORIS_CMD_NOP            = 0,
    DORIS_CMD_SEND_REPORT    = 1,   // trigger an immediate telemetry report
    DORIS_CMD_RELEASE        = 2,   // fire the release relay (failsafe)
    DORIS_CMD_RESET_STATE    = 3,   // reset state machine to PRE_MISSION
    DORIS_CMD_REBOOT         = 4,   // soft reboot
    DORIS_CMD_ENABLE_IRIDIUM = 5,   // enable Iridium reporting
    DORIS_CMD_DISABLE_IRIDIUM = 6,  // disable Iridium reporting
};

#pragma pack(push, 1)
typedef struct {
    uint8_t  command;           // DorisCommandType
    uint32_t param;             // command-specific parameter
} DorisCommand;
#pragma pack(pop)

#define DORIS_COMMAND_WIRE_SIZE (DORIS_MSG_HEADER_SIZE + sizeof(DorisCommand))

// ============================================================================
// MO: Acknowledgment (ID 8) — confirms receipt of MT message
// ============================================================================

#pragma pack(push, 1)
typedef struct {
    uint8_t  acked_msg_id;      // which MT message ID we're acking
    uint8_t  result;            // 0 = OK, nonzero = error code
} DorisAck;
#pragma pack(pop)

#define DORIS_ACK_WIRE_SIZE (DORIS_MSG_HEADER_SIZE + sizeof(DorisAck))

// ============================================================================
// Serialization helpers
// ============================================================================

static inline size_t DorisProtocol_serializeReport(const DorisReport* report, uint8_t* buf, size_t bufLen) {
    if (bufLen < DORIS_REPORT_WIRE_SIZE) return 0;
    buf[0] = DORIS_MSG_START;
    buf[1] = DORIS_MSG_ID_REPORT;
    memcpy(buf + DORIS_MSG_HEADER_SIZE, report, sizeof(DorisReport));
    return DORIS_REPORT_WIRE_SIZE;
}

static inline size_t DorisProtocol_serializeAck(const DorisAck* ack, uint8_t* buf, size_t bufLen) {
    if (bufLen < DORIS_ACK_WIRE_SIZE) return 0;
    buf[0] = DORIS_MSG_START;
    buf[1] = DORIS_MSG_ID_ACK;
    memcpy(buf + DORIS_MSG_HEADER_SIZE, ack, sizeof(DorisAck));
    return DORIS_ACK_WIRE_SIZE;
}

// Parse an incoming MT message. Returns the message ID, or 0 on failure.
// On success, copies the payload into the appropriate output struct.
static inline uint8_t DorisProtocol_parseMT(const uint8_t* data, size_t len,
                                             DorisConfig* configOut,
                                             DorisCommand* commandOut) {
    if (len < DORIS_MSG_HEADER_SIZE) return 0;
    if (data[0] != DORIS_MSG_START) return 0;

    uint8_t msgId = data[1];
    const uint8_t* payload = data + DORIS_MSG_HEADER_SIZE;
    size_t payloadLen = len - DORIS_MSG_HEADER_SIZE;

    switch (msgId) {
        case DORIS_MSG_ID_CONFIG:
            if (payloadLen < sizeof(DorisConfig)) return 0;
            if (configOut) memcpy(configOut, payload, sizeof(DorisConfig));
            return msgId;

        case DORIS_MSG_ID_COMMAND:
            if (payloadLen < sizeof(DorisCommand)) return 0;
            if (commandOut) memcpy(commandOut, payload, sizeof(DorisCommand));
            return msgId;

        default:
            return 0;
    }
}

#endif // DORIS_PROTOCOL_H
