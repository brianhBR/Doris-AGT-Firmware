#include "modules/iridium_message.h"

#include <math.h>
#include <stdint.h>

namespace {

const unsigned long COORDINATE_SCALE = 100000UL;
const unsigned long MAX_LATITUDE_SCALED = 90UL * COORDINATE_SCALE;
const unsigned long MAX_LONGITUDE_SCALED = 180UL * COORDINATE_SCALE;
const unsigned long MAX_SPEED_DECIMETERS_PER_SECOND = 99UL;
const unsigned long MAX_DEPTH_METERS = 9999UL;
const long MAX_ONE_DECIMAL_VALUE = 999L;

struct BufferWriter {
    char* destination;
    size_t capacity;
    size_t length;
    bool valid;
};

void appendCharacter(BufferWriter& writer, char value) {
    if (!writer.valid || writer.length + 1 >= writer.capacity) {
        writer.valid = false;
        return;
    }
    writer.destination[writer.length++] = value;
    writer.destination[writer.length] = '\0';
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

void appendOneDecimal(BufferWriter& writer, double value, bool allowNegative) {
    double minimum = allowNegative ? -99.9 : 0.0;
    double clamped = clampDouble(value, minimum, 99.9);
    long tenths = static_cast<long>(
        clamped < 0.0 ? ceil(clamped * 10.0 - 0.5)
                      : floor(clamped * 10.0 + 0.5));
    if (tenths > MAX_ONE_DECIMAL_VALUE) {
        tenths = MAX_ONE_DECIMAL_VALUE;
    } else if (tenths < -MAX_ONE_DECIMAL_VALUE) {
        tenths = -MAX_ONE_DECIMAL_VALUE;
    }

    if (tenths < 0) {
        appendCharacter(writer, '-');
        tenths = -tenths;
    }

    unsigned long magnitude = static_cast<unsigned long>(tenths);
    unsigned long integerPart = magnitude / 10UL;
    if (integerPart >= 10UL) {
        appendPaddedUnsigned(writer, integerPart, 2);
    } else {
        appendPaddedUnsigned(writer, integerPart, 1);
    }
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

bool IridiumMessage_formatProtocolB(
    char* destination,
    size_t destinationSize,
    const IridiumProtocolBFields& fields) {
    if (destination == nullptr || destinationSize == 0) {
        return false;
    }

    BufferWriter writer = {destination, destinationSize, 0, true};
    destination[0] = '\0';

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
    double temperature =
        fields.temperatureValid && isfinite(fields.minimumTemperatureCelsius)
            ? fields.minimumTemperatureCelsius
            : 0.0;

    appendLiteral(writer, "B,");
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
    appendOneDecimal(writer, voltage, false);
    appendCharacter(writer, ',');
    appendOneDecimal(writer, temperature, true);
    appendLiteral(writer, ",00");

    return writer.valid;
}
