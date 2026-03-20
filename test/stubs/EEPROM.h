#ifndef EEPROM_H_STUB
#define EEPROM_H_STUB

#include <stdint.h>
#include <string.h>

#define EEPROM_STUB_SIZE 4096

class FakeEEPROM {
    uint8_t _data[EEPROM_STUB_SIZE];
public:
    FakeEEPROM() { memset(_data, 0xFF, sizeof(_data)); }

    uint8_t read(int addr) {
        if (addr < 0 || addr >= EEPROM_STUB_SIZE) return 0xFF;
        return _data[addr];
    }

    void write(int addr, uint8_t val) {
        if (addr >= 0 && addr < EEPROM_STUB_SIZE)
            _data[addr] = val;
    }

    template <typename T>
    T& get(int addr, T& val) {
        if (addr >= 0 && (addr + (int)sizeof(T)) <= EEPROM_STUB_SIZE)
            memcpy(&val, _data + addr, sizeof(T));
        return val;
    }

    template <typename T>
    const T& put(int addr, const T& val) {
        if (addr >= 0 && (addr + (int)sizeof(T)) <= EEPROM_STUB_SIZE)
            memcpy(_data + addr, &val, sizeof(T));
        return val;
    }

    void clear() { memset(_data, 0xFF, sizeof(_data)); }
};

static FakeEEPROM EEPROM;

#endif // EEPROM_H_STUB
