#include "utils/SoftwareSerial.h"

SoftwareSerial* SoftwareSerial::_activeObject = nullptr;

SoftwareSerial::SoftwareSerial(uint8_t rxPin, uint8_t txPin)
    : _rxPin(rxPin), _txPin(txPin), _bitDelay(0), _rxHead(0), _rxTail(0) {
}

void SoftwareSerial::begin(uint32_t baud) {
    // Calculate bit delay in microseconds
    _bitDelay = 1000000 / baud;

    // Configure TX pin as output, idle high
    pinMode(_txPin, OUTPUT);
    digitalWrite(_txPin, HIGH);

    // Configure RX pin as input with pullup
    pinMode(_rxPin, INPUT_PULLUP);

    // Use polling for RX instead of interrupts (interrupt-free implementation)
    _activeObject = this;
}

void SoftwareSerial::end() {
    _activeObject = nullptr;
}

// Poll for incoming byte (called from available/read)
void SoftwareSerial::pollRX() {
    // Poll-based RX implementation (runs in main context, no interrupts)
    // Check for start bit (LOW)
    if (digitalRead(_rxPin) == HIGH) {
        return;  // Line idle
    }

    // Wait for middle of start bit
    delayMicroseconds(_bitDelay / 2);
    if (digitalRead(_rxPin) != LOW) {
        return;  // False start
    }

    // Read 8 data bits
    uint8_t byte = 0;
    for (uint8_t i = 0; i < 8; i++) {
        delayMicroseconds(_bitDelay);
        if (digitalRead(_rxPin)) {
            byte |= (1 << i);
        }
    }

    // Wait for stop bit
    delayMicroseconds(_bitDelay);

    // Store in buffer
    uint8_t nextHead = (_rxHead + 1) & (RX_BUFFER_SIZE - 1);
    if (nextHead != _rxTail) {
        _rxBuffer[_rxHead] = byte;
        _rxHead = nextHead;
    }
}

int SoftwareSerial::available() {
    // Poll for new data before checking buffer
    pollRX();
    return (_rxHead - _rxTail) & (RX_BUFFER_SIZE - 1);
}

int SoftwareSerial::read() {
    if ((_rxHead - _rxTail) & (RX_BUFFER_SIZE - 1)) {
        uint8_t byte = _rxBuffer[_rxTail];
        _rxTail = (_rxTail + 1) & (RX_BUFFER_SIZE - 1);
        return byte;
    }
    return -1;
}

void SoftwareSerial::txBit(bool level) {
    digitalWrite(_txPin, level ? HIGH : LOW);
    delayMicroseconds(_bitDelay);
}

size_t SoftwareSerial::write(uint8_t byte) {
    // TX at low baud rate (9600) - no interrupt disable needed

    // Start bit (LOW)
    txBit(false);

    // Data bits (LSB first)
    for (uint8_t i = 0; i < 8; i++) {
        txBit(byte & 0x01);
        byte >>= 1;
    }

    // Stop bit (HIGH)
    txBit(true);

    return 1;
}

size_t SoftwareSerial::write(const uint8_t *buffer, size_t size) {
    size_t written = 0;
    for (size_t i = 0; i < size; i++) {
        written += write(buffer[i]);
    }
    return written;
}

void SoftwareSerial::flush() {
    // Wait for TX to complete (already blocking)
}

void SoftwareSerial::rxHandler() {
    // RX interrupt disabled for now - polling only
    // Interrupt-based RX causes RTOS issues with delayMicroseconds()
}

void SoftwareSerial::poll() {
    // Poll for incoming data
    // At 4800 baud, this should be more reliable than at 9600 baud
    pollRX();
}
