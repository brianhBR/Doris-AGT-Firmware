#ifndef WIRE_H_STUB
#define WIRE_H_STUB

#include <stdint.h>

class FakeWire {
public:
    void begin() {}
    void beginTransmission(uint8_t) {}
    uint8_t endTransmission(bool = true) { return 0; }
    uint8_t requestFrom(uint8_t, uint8_t) { return 0; }
    int available() { return 0; }
    int read() { return -1; }
    size_t write(uint8_t) { return 1; }
    size_t write(const uint8_t*, size_t n) { return n; }
    void setClock(uint32_t) {}
};

static FakeWire Wire;

#endif // WIRE_H_STUB
