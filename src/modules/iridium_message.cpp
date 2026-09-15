#include "modules/iridium_message.h"

#include <math.h>
#include <stdint.h>

namespace {

const unsigned long COORDINATE_SCALE = 100000UL;
const unsigned long MAX_LATITUDE_SCALED = 90UL * COORDINATE_SCALE;
const unsigned long MAX_LONGITUDE_SCALED = 180UL * COORDINATE_SCALE;
const unsigned long MAX_SPEED_DECIMETERS_PER_SECOND = 99UL;
const unsigned long MAX_DEPTH_METERS = 9999UL;
const long MAX_BATTERY_TENTHS = 999L;

struct BufferWriter {
    uint8_t* destination;
    size_t capacity;
    size_t length;
    bool valid;
};

void appendByte(BufferWriter& writer, uint8_t value) {
    if (!writer.valid || writer.length >= writer.capacity) {
        writer.valid = false;
        return;
    }
    writer.destination[writer.length++] = value;
}

void appendCharacter(BufferWriter& writer, char value) {
    appendByte(writer, static_cast<uint8_t>(value));
}

void appendLiteral(BufferWriter& writer, const char* value) {
    while (*value != '\0') {
        appendCharacter(writer, *value++);
    }
}

void appendPaddedUnsigned(
    BufferWriter& writer,
    unsigned long value,
    uint8_t width) {
    unsigned long divisor = 1;
    for (uint8_t index = 1; index < width; index++) {
        divisor *= 10UL;
    }
    while (divisor > 0) {
        appendCharacter(
            writer,
            static_cast<char>('0' + ((value / divisor) % 10UL)));
        divisor /= 10UL;
    }
}

double clampDouble(double value, double minimum, double maximum) {
    if (value < minimum) {
        return minimum;
    }
    if (value > maximum) {
        return maximum;
    }
    return value;
}

unsigned long roundedPositive(double value) {
    return static_cast<unsigned long>(floor(value + 0.5));
}

void appendCoordinate(
    BufferWriter& writer,
    double coordinateDegrees,
    unsigned long maximumScaled) {
    double magnitude = fabs(coordinateDegrees);
    unsigned long scaled = roundedPositive(magnitude * COORDINATE_SCALE);
    if (scaled > maximumScaled) {
        scaled = maximumScaled;
    }

    appendCharacter(writer, coordinateDegrees < 0.0 ? '-' : '+');
    appendPaddedUnsigned(writer, scaled / COORDINATE_SCALE, 3);
    appendCharacter(writer, '.');
    appendPaddedUnsigned(writer, scaled % COORDINATE_SCALE, 5);
}

void appendBatteryVoltage(BufferWriter& writer, double voltage) {
    double clamped = clampDouble(voltage, 0.0, 99.9);
    long tenths = static_cast<long>(floor(clamped * 10.0 + 0.5));
    if (tenths > MAX_BATTERY_TENTHS) {
        tenths = MAX_BATTERY_TENTHS;
    } else if (tenths < 0) {
        tenths = 0;
    }

    unsigned long magnitude = static_cast<unsigned long>(tenths);
    appendPaddedUnsigned(writer, magnitude / 10UL, 2);
    appendCharacter(writer, '.');
    appendPaddedUnsigned(writer, magnitude % 10UL, 1);
}

unsigned long normalizedCourse(float courseDegrees) {
    if (!isfinite(courseDegrees)) {
        return 0;
    }
    double normalized = fmod(static_cast<double>(courseDegrees), 360.0);
    if (normalized < 0.0) {
        normalized += 360.0;
    }
    unsigned long rounded = roundedPositive(normalized);
    return rounded == 360UL ? 0UL : rounded;
}

} // namespace

size_t IridiumMessage_formatP1(
    uint8_t* destination,
    size_t destinationSize,
    const IridiumP1Fields& fields) {
    if (destination == nullptr || destinationSize < IRIDIUM_P1_PAYLOAD_SIZE) {
        return 0;
    }

    BufferWriter writer = {destination, destinationSize, 0, true};

    bool coordinatesValid =
        fields.gpsValid &&
        isfinite(fields.latitudeDegrees) &&
        isfinite(fields.longitudeDegrees);
    double latitude = coordinatesValid
                          ? clampDouble(fields.latitudeDegrees, -90.0, 90.0)
                          : 0.0;
    double longitude = coordinatesValid
                           ? clampDouble(fields.longitudeDegrees, -180.0, 180.0)
                           : 0.0;
    double speed = coordinatesValid &&
                           isfinite(fields.groundSpeedMetersPerSecond)
                       ? fields.groundSpeedMetersPerSecond
                       : 0.0;
    unsigned long speedDecimetersPerSecond =
        roundedPositive(clampDouble(speed * 10.0, 0.0, 99.0));

    double depth = isfinite(fields.maximumDepthMeters)
                       ? fields.maximumDepthMeters
                       : 0.0;
    unsigned long depthMeters = roundedPositive(
        clampDouble(depth, 0.0, static_cast<double>(MAX_DEPTH_METERS)));
    double voltage = isfinite(fields.batteryVoltage)
                         ? fields.batteryVoltage
                         : 0.0;

    appendLiteral(writer, "P,1,");
    appendCoordinate(writer, latitude, MAX_LATITUDE_SCALED);
    appendCharacter(writer, ',');
    appendCoordinate(writer, longitude, MAX_LONGITUDE_SCALED);
    appendCharacter(writer, ',');
    appendPaddedUnsigned(
        writer,
        speedDecimetersPerSecond > MAX_SPEED_DECIMETERS_PER_SECOND
            ? MAX_SPEED_DECIMETERS_PER_SECOND
            : speedDecimetersPerSecond,
        2);
    appendCharacter(writer, ',');
    appendPaddedUnsigned(
        writer,
        coordinatesValid ? normalizedCourse(fields.courseDegrees) : 0UL,
        3);
    appendCharacter(writer, ',');
    appendPaddedUnsigned(writer, depthMeters, 4);
    appendCharacter(writer, ',');
    appendBatteryVoltage(writer, voltage);
    appendCharacter(writer, ',');
    appendByte(writer, 0x00);
    appendByte(writer, 0x00);

    if (!writer.valid || writer.length != IRIDIUM_P1_PAYLOAD_SIZE) {
        return 0;
    }
    return writer.length;
}
