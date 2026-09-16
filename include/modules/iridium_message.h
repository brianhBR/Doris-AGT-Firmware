#ifndef IRIDIUM_MESSAGE_H
#define IRIDIUM_MESSAGE_H

#include <stddef.h>
#include <stdint.h>

/** Canonical P/1 SBD payload size: ASCII body + trailing comma + two flag bytes. */
static const size_t IRIDIUM_P1_PAYLOAD_SIZE = 45;

/** RockBLOCK / Iridium SBD MO budget. */
static const size_t IRIDIUM_SBD_MO_SIZE = 50;

/**
 * Values encoded by the DORIS Iridium P/1 position payload.
 */
struct IridiumP1Fields {
    bool gpsValid;
    double latitudeDegrees;
    double longitudeDegrees;
    float groundSpeedMetersPerSecond;
    float courseDegrees;
    float maximumDepthMeters;
    float batteryVoltage;
};

/**
 * Format one P/1 SBD payload.
 *
 * Layout is ASCII fields with no spaces, a trailing comma, then two raw
 * status-flag bytes (currently 0x00 0x00). The buffer is not a C string:
 * do not strlen() it, and do not send it with the text SBD API.
 *
 * Returns the number of bytes written (45 for in-range values), or 0 if
 * the destination is null or too small.
 */
size_t IridiumMessage_formatP1(
    uint8_t* destination,
    size_t destinationSize,
    const IridiumP1Fields& fields);

#endif // IRIDIUM_MESSAGE_H
