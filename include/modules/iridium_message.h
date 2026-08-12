#ifndef IRIDIUM_MESSAGE_H
#define IRIDIUM_MESSAGE_H

#include <stddef.h>

/**
 * Values encoded by the DORIS Iridium protocol B text payload.
 */
struct IridiumProtocolBFields {
    bool gpsValid;
    double latitudeDegrees;
    double longitudeDegrees;
    float groundSpeedMetersPerSecond;
    float courseDegrees;
    float maximumDepthMeters;
    float batteryVoltage;
    bool temperatureValid;
    float minimumTemperatureCelsius;
};

/**
 * Format one comma-separated protocol B payload.
 *
 * The status byte is reserved as 00 until its bit assignments are defined.
 * Returns false if the destination buffer is null or too small.
 */
bool IridiumMessage_formatProtocolB(
    char* destination,
    size_t destinationSize,
    const IridiumProtocolBFields& fields);

#endif // IRIDIUM_MESSAGE_H
