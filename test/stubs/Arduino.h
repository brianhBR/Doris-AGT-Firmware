#ifndef ARDUINO_H_STUB
#define ARDUINO_H_STUB

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <string>

// Arduino type aliases
typedef uint8_t byte;
typedef bool boolean;

// Fake millis() with controllable value for tests
#ifdef __cplusplus
extern "C" {
#endif

static unsigned long _stub_millis_value = 0;

static inline unsigned long millis() { return _stub_millis_value; }
static inline void stub_set_millis(unsigned long v) { _stub_millis_value = v; }
static inline void stub_advance_millis(unsigned long delta) { _stub_millis_value += delta; }

static inline void delay(unsigned long) {}
static inline void delayMicroseconds(unsigned int) {}

static inline void pinMode(uint8_t, uint8_t) {}
static inline void digitalWrite(uint8_t, uint8_t) {}
static inline int  digitalRead(uint8_t) { return 0; }
static inline int  analogRead(uint8_t) { return 0; }
static inline void analogWrite(uint8_t, int) {}

#define HIGH 1
#define LOW  0
#define INPUT  0
#define OUTPUT 1
#define INPUT_PULLUP 2

#ifdef __cplusplus
}
#endif

// F() macro — just pass the string through on native
#define F(s) (s)

// Minimal Serial stub that compiles but discards output
class FakeSerial {
public:
    void begin(unsigned long) {}
    void print(const char*) {}
    void print(int) {}
    void print(unsigned int) {}
    void print(long) {}
    void print(unsigned long) {}
    void print(float, int = 2) {}
    void print(double v, int d = 2) { print((float)v, d); }
    void println() {}
    void println(const char*) {}
    void println(int) {}
    void println(unsigned int) {}
    void println(long) {}
    void println(unsigned long) {}
    void println(float, int = 2) {}
    void println(double v, int d = 2) { println((float)v, d); }
    void println(bool v) { println((int)v); }
    int available() { return 0; }
    int read() { return -1; }
    void flush() {}
    size_t write(uint8_t) { return 1; }
    size_t write(const uint8_t*, size_t n) { return n; }
    operator bool() const { return true; }
};

static FakeSerial Serial;
static FakeSerial Serial1;

// Minimal String class matching Arduino API surface used by firmware
class String {
    std::string _s;
public:
    String() {}
    String(const char* s) : _s(s ? s : "") {}
    String(const String& o) : _s(o._s) {}

    const char* c_str() const { return _s.c_str(); }
    size_t length() const { return _s.length(); }

    bool startsWith(const char* prefix) const {
        std::string p(prefix);
        return _s.compare(0, p.size(), p) == 0;
    }

    String substring(size_t from) const {
        if (from >= _s.size()) return String("");
        return String(_s.substr(from).c_str());
    }

    String substring(size_t from, size_t to) const {
        if (from >= _s.size()) return String("");
        return String(_s.substr(from, to - from).c_str());
    }

    int indexOf(char c, size_t from = 0) const {
        size_t pos = _s.find(c, from);
        return pos == std::string::npos ? -1 : (int)pos;
    }

    long toInt() const { return atol(_s.c_str()); }
    float toFloat() const { return (float)atof(_s.c_str()); }

    void toLowerCase() {
        for (auto& ch : _s) ch = tolower(ch);
    }

    bool operator==(const char* rhs) const { return _s == rhs; }
    bool operator==(const String& rhs) const { return _s == rhs._s; }
    String& operator=(const String& o) { _s = o._s; return *this; }
};

#endif // ARDUINO_H_STUB
