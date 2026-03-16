#include "utils/SoftwareSerial.h"

SoftwareSerial* SoftwareSerial::_activeObject = nullptr;

// ARM DWT cycle counter — runs at CPU clock (48 MHz on Apollo3),
// completely independent of interrupts or RTOS tick.
static inline void dwtInit() {
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

static inline uint32_t dwtCycles() {
    return DWT->CYCCNT;
}

static inline void dwtWaitUntil(uint32_t target) {
    while ((int32_t)(target - dwtCycles()) > 0);
}

SoftwareSerial::SoftwareSerial(uint8_t rxPin, uint8_t txPin)
    : _rxPin(rxPin), _txPin(txPin), _bitDelay(0), _bitCycles(0),
      _rxHead(0), _rxTail(0) {
}

void SoftwareSerial::begin(uint32_t baud) {
    _bitDelay = 1000000 / baud;
    _bitCycles = 48000000 / baud;  // 48 MHz / baud = cycles per bit

    dwtInit();

    pinMode(_txPin, OUTPUT);
    digitalWrite(_txPin, HIGH);

    pinMode(_rxPin, INPUT_PULLUP);

    _activeObject = this;
}

void SoftwareSerial::end() {
    _activeObject = nullptr;
}

void SoftwareSerial::pollRX() {
    if (digitalRead(_rxPin) == HIGH) {
        return;
    }

    delayMicroseconds(_bitDelay / 2);
    if (digitalRead(_rxPin) != LOW) {
        return;
    }

    uint8_t byte = 0;
    for (uint8_t i = 0; i < 8; i++) {
        delayMicroseconds(_bitDelay);
        if (digitalRead(_rxPin)) {
            byte |= (1 << i);
        }
    }

    delayMicroseconds(_bitDelay);

    uint8_t nextHead = (_rxHead + 1) & (RX_BUFFER_SIZE - 1);
    if (nextHead != _rxTail) {
        _rxBuffer[_rxHead] = byte;
        _rxHead = nextHead;
    }
}

int SoftwareSerial::available() {
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
    uint32_t savedPrimask = __get_PRIMASK();
    __disable_irq();

    uint32_t next = dwtCycles();

    // Start bit
    digitalWrite(_txPin, LOW);
    next += _bitCycles;
    dwtWaitUntil(next);

    // 8 data bits (LSB first)
    for (uint8_t i = 0; i < 8; i++) {
        digitalWrite(_txPin, (byte & 0x01) ? HIGH : LOW);
        byte >>= 1;
        next += _bitCycles;
        dwtWaitUntil(next);
    }

    // Stop bit
    digitalWrite(_txPin, HIGH);
    next += _bitCycles;
    dwtWaitUntil(next);

    __set_PRIMASK(savedPrimask);

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
}

void SoftwareSerial::rxHandler() {
}

void SoftwareSerial::poll() {
    pollRX();
}
