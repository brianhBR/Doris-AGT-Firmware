# Doris AGT - Build & Upload Commands

## Build Commands

### Build the firmware (compile only)
```bash
C:\Users\brian\.platformio\penv\Scripts\platformio.exe run
```

### Upload firmware to AGT
**IMPORTANT: Close the serial monitor first (Ctrl+C) before uploading!**

```bash
C:\Users\brian\.platformio\penv\Scripts\platformio.exe run --target upload
```

### Monitor serial output
```bash
C:\Users\brian\.platformio\penv\Scripts\platformio.exe device monitor --port COM7
```

### Build, Upload, and Monitor (all in one)
**IMPORTANT: Only works if serial monitor is not already open!**

```bash
C:\Users\brian\.platformio\penv\Scripts\platformio.exe run --target upload && C:\Users\brian\.platformio\penv\Scripts\platformio.exe device monitor --port COM7
```

## Quick Workflow

1. **Stop serial monitor** - Press `Ctrl+C` in the terminal running the monitor
2. **Build and upload** - Run: `C:\Users\brian\.platformio\penv\Scripts\platformio.exe run --target upload`
3. **Start serial monitor** - Run: `C:\Users\brian\.platformio\penv\Scripts\platformio.exe device monitor --port COM7`

## After Upload

Once the new firmware is uploaded and the serial monitor is open, configure the device:

```
config_reset
```

Then reboot the device (press reset button or power cycle).

You should now see:
- Meshtastic initialization messages
- MAVLink interval set to 1000ms
- All features enabled
