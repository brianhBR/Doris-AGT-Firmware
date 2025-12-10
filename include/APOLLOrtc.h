#ifndef APOLLORTC_H
#define APOLLORTC_H

#include <Arduino.h>
#include <stdint.h>

class APOLLOrtc {
public:
    APOLLOrtc();
    void setTime(uint8_t hour, uint8_t minute, uint8_t second, uint8_t unused, uint8_t day, uint8_t month, uint16_t year);
    uint32_t getEpoch();

private:
    // Fallback storage if HAL is not available
    uint32_t epochOffset;
    unsigned long setMillis;
    static int64_t dateToEpoch(int year, int month, int day, int hour, int minute, int second);
};

#endif // APOLLORTC_H
#ifndef APOLLORTC_H
#define APOLLORTC_H

#include <Arduino.h>
#include <stdint.h>

class APOLLOrtc {
public:
    APOLLOrtc();
    void setTime(uint8_t hour, uint8_t minute, uint8_t second, uint8_t unused, uint8_t day, uint8_t month, uint16_t year);
    uint32_t getEpoch();

private:
    uint32_t epochOffset;
    unsigned long setMillis;
    static int64_t dateToEpoch(int year, int month, int day, int hour, int minute, int second);
};

#endif // APOLLORTC_H
