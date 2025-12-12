# Serial Port Configuration - Artemis Global Tracker

## Overview

The Artemis Global Tracker uses **separate UART instances** for Iridium and Meshtastic:
- **Serial1 (UART1)** - Iridium 9603N on default pins D24/D25
- **MeshtasticSerial (UART0)** - Meshtastic RAK4603 on pins D39/D40 via J10 connector

**Key Point**: Each module has its own dedicated UART instance, allowing both to operate simultaneously without conflicts. No pin remapping or shared resources.

## Pin Assignments

### Iridium 9603N
- **Serial Port**: Serial1 (UART1 hardware instance)
- **Physical Pins**: D24/D25 (default Serial1 pins)
  - **D24** - TX1 → Iridium RX(OUT) pin 7
  - **D25** - RX1 ← Iridium TX(IN) pin 6
- **Baud Rate**: 19200
- **Location**: Onboard module (hardwired)

### Meshtastic RAK4603
- **Serial Port**: MeshtasticSerial (UART0 hardware instance)
- **Physical Pins**: D39/D40 (on J10 Qwiic connector)
  - **D39 (SCL4)** - TX → RAK4603 RX
  - **D40 (SDA4)** - RX ← RAK4603 TX
- **Baud Rate**: 115200
- **Location**: J10 (Qwiic/I2C Port 4 connector)

## How It Works

1. **Iridium Initialization** (in [iridium_manager.cpp](src/modules/iridium_manager.cpp:42)):
   - Serial1 uses **UART1 instance on default pins D24/D25** (hardwired)
   - Iridium modem initialized: `IridiumSBD modem(Serial1, ...)`
   - No pin configuration needed (uses default)

2. **Meshtastic Initialization** (in [meshtastic_interface.cpp](src/modules/meshtastic_interface.cpp:13)):
   - Custom `UART` object created: `UART MeshtasticSerial(0, 39, 40)`
     - UART instance: 0
     - TX pin: 39 (D39/SCL4 on J10)
     - RX pin: 40 (D40/SDA4 on J10)
   - MeshtasticSerial.begin() activates UART0 on these pins

3. **Why This Works**:
   - Each module has a dedicated UART instance
   - Iridium: Serial1 → UART1 → D24/D25
   - Meshtastic: MeshtasticSerial → UART0 → D39/D40
   - Both UARTs operate independently and simultaneously

## J10 Connector Pinout

The J10 Qwiic connector provides these signals:

| Pin | Default Function | UART0 Alternate | Meshtastic (MeshtasticSerial) |
|-----|------------------|-----------------|-------------------------------|
| 1   | SCL4 (I2C Clock) | **UART0 TX**    | **TX → RAK4603 RX** |
| 2   | SDA4 (I2C Data)  | **UART0 RX**    | **RX ← RAK4603 TX** |
| 3   | VCC (3.3V)       | -               | Power |
| 4   | GND              | -               | Ground |

## Wiring Instructions

### Meshtastic RAK4603 to J10 Connector

Connect the following:
- **J10 Pin 1 (D39/TX1)** → RAK4603 **RX**
- **J10 Pin 2 (D40/RX1)** → RAK4603 **TX**
- **J10 Pin 3 (3.3V)**    → RAK4603 **VCC**
- **J10 Pin 4 (GND)**     → RAK4603 **GND**

**IMPORTANT**: These are 3.3V logic levels. Do not connect 5V devices.

## Code References

### Configuration ([config.h](include/config.h))
```cpp
#define MESHTASTIC_TX_PIN    39  // D39 (SCL4) on J10
#define MESHTASTIC_RX_PIN    40  // D40 (SDA4) on J10

#define IRIDIUM_SERIAL       Serial1  // UART1 on default pins D24/D25
// MeshtasticSerial is defined in meshtastic_interface.cpp
```

### Iridium Manager ([iridium_manager.cpp](src/modules/iridium_manager.cpp:42))
```cpp
// Initialize Serial1 on default pins (D24=TX, D25=RX) for Iridium
// Uses UART1 hardware instance
IRIDIUM_SERIAL.begin(IRIDIUM_BAUD);
```

### Meshtastic Interface ([meshtastic_interface.cpp](src/modules/meshtastic_interface.cpp:13))
```cpp
// Create a dedicated UART instance for Meshtastic on UART0
UART MeshtasticSerial(0, MESHTASTIC_TX_PIN, MESHTASTIC_RX_PIN);

// In init function:
MeshtasticSerial.begin(MESHTASTIC_BAUD);
```

## Testing

1. **Verify Iridium Communication**:
   - Check for supercap charge message
   - Verify modem initialization success

2. **Verify Meshtastic Communication**:
   - Check for "Interface initialized" message
   - Verify pin configuration to D39/D40
   - Test GPS position transmission

3. **Check for Conflicts**:
   - Ensure Iridium and Meshtastic don't transmit simultaneously
   - Monitor for serial port contention

## Troubleshooting

### Iridium Not Working
- Check supercap PGOOD signal
- Verify Serial1 is on default pins (D24/D25)
- Check baud rate is 19200

### Meshtastic Not Working
- Verify RAK4603 is in PROTO mode (`meshtastic --set serial.mode PROTO`)
- Check J10 wiring (TX/RX may be swapped)
- Verify pin remapping in initialization
- Check baud rate is 115200

### Both Not Working
- Check if Serial1 initialization is conflicting
- Verify timing (Iridium init before Meshtastic init)
- Check power supply to both modules
