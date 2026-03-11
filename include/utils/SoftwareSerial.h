#ifndef SOFTWARE_SERIAL_H
#define SOFTWARE_SERIAL_H

#include <Arduino.h>

class SoftwareSerial {
public:
    SoftwareSerial(uint8_t rxPin, uint8_t txPin);
    void begin(uint32_t baud);
    void end();

    // Poll RX routine callable from main loop (non-ISR)
    void poll();

    int available();
    int read();
    size_t write(uint8_t byte);
    size_t write(const uint8_t *buffer, size_t size);
    void flush();

private:
    uint8_t _rxPin;
    uint8_t _txPin;
    uint32_t _bitDelay;  // Microseconds per bit

    // Ring buffer for RX
    static const uint8_t RX_BUFFER_SIZE = 64;
    volatile uint8_t _rxBuffer[RX_BUFFER_SIZE];
    volatile uint8_t _rxHead;
    volatile uint8_t _rxTail;

    void txBit(bool level);
    void pollRX();  // Poll-based RX (no interrupts)
    static void rxHandler();  // Unused (kept for compatibility)
    static SoftwareSerial* _activeObject;
};

#endif // SOFTWARE_SERIAL_H
