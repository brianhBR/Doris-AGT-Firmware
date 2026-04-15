"""Monitor MAVLink messages from AGT on a serial port.

Uses raw pyserial + pymavlink parser to avoid the 1200-baud touch
that mavutil.mavlink_connection does (which can reset Apollo3 boards).
"""
import sys
import time
import serial
from pymavlink.dialects.v20 import common as mavlink2

PORT = sys.argv[1] if len(sys.argv) > 1 else "COM10"
BAUD = int(sys.argv[2]) if len(sys.argv) > 2 else 57600

print(f"Connecting to {PORT} @ {BAUD}...")
ser = serial.Serial(PORT, BAUD, timeout=1, dsrdtr=False, rtscts=False)
print("Listening for MAVLink messages (Ctrl+C to stop)\n")

mav = mavlink2.MAVLink(None)
mav.robust_parsing = True

msg_counts = {}
start = time.time()
bytes_total = 0

try:
    while True:
        chunk = ser.read(256)
        if not chunk:
            elapsed = time.time() - start
            if bytes_total == 0:
                print(f"[{elapsed:.1f}s] No data received yet...")
            continue

        bytes_total += len(chunk)

        try:
            msgs = mav.parse_buffer(chunk)
        except Exception:
            msgs = None

        if not msgs:
            continue

        for msg in msgs:
            msg_type = msg.get_type()
            if msg_type == "BAD_DATA":
                continue
            msg_counts[msg_type] = msg_counts.get(msg_type, 0) + 1
            elapsed = time.time() - start

            if msg_type == "HEARTBEAT":
                print(f"[{elapsed:.1f}s] HEARTBEAT  type={msg.type} autopilot={msg.autopilot} "
                      f"base_mode={msg.base_mode} custom_mode={msg.custom_mode} "
                      f"system_status={msg.system_status}")

            elif msg_type == "GPS_RAW_INT":
                lat = msg.lat / 1e7
                lon = msg.lon / 1e7
                alt = msg.alt / 1000.0
                print(f"[{elapsed:.1f}s] GPS_RAW_INT  fix={msg.fix_type} sats={msg.satellites_visible} "
                      f"lat={lat:.7f} lon={lon:.7f} alt={alt:.1f}m "
                      f"eph={msg.eph} epv={msg.epv}")

            elif msg_type == "GPS_INPUT":
                lat = msg.lat / 1e7
                lon = msg.lon / 1e7
                alt = msg.alt
                print(f"[{elapsed:.1f}s] GPS_INPUT    fix={msg.fix_type} sats={msg.satellites_visible} "
                      f"lat={lat:.7f} lon={lon:.7f} alt={alt:.1f}m "
                      f"hdop={msg.hdop:.2f} vdop={msg.vdop:.2f} "
                      f"horiz_accuracy={msg.horiz_accuracy:.2f} vert_accuracy={msg.vert_accuracy:.2f}")

            elif msg_type == "STATUSTEXT":
                print(f"[{elapsed:.1f}s] STATUSTEXT [{msg.severity}] {msg.text}")

            else:
                print(f"[{elapsed:.1f}s] {msg_type}  (#{msg_counts[msg_type]})")

except KeyboardInterrupt:
    pass
finally:
    ser.close()
    print(f"\n--- Summary ({time.time() - start:.1f}s, {bytes_total} bytes) ---")
    for t, c in sorted(msg_counts.items(), key=lambda x: -x[1]):
        print(f"  {t}: {c}")
