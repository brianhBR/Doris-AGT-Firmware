"""
Doris Iridium SBD Binary Protocol — Python reference implementation.

Compatible with SolarSurfer2 message framing ('$' + msg_id + packed payload).
Drop this into the SolarSurfer2-cloud backend's messages.py MESSAGES dict,
or use standalone for testing.

Wire format:
  Byte 0:    '$' (0x24) — start marker
  Byte 1:    message ID
  Bytes 2+:  little-endian packed struct

MO (device -> cloud): max 340 bytes
MT (cloud -> device): max 270 bytes
"""

from struct import pack, unpack, calcsize

START = '$'
ENDIAN = '<'

MESSAGES = {
    # ----------------------------------------------------------------
    # MO: Telemetry report (device -> cloud), ID 5
    # ----------------------------------------------------------------
    'doris_report': {
        'id': 5,
        'fields': [
            ('mission_state',    'B',  '0=pre,1=selftest,2=mission,3=recovery,4=failsafe'),
            ('gps_fix_type',     'B',  '0=none,1=nofix,2=2d,3=3d'),
            ('satellites',       'B',  'count'),
            ('failsafe_flags',   'B',  'bitfield: b0=low_v, b1=crit_v, b2=leak, b3=max_depth, b4=no_hb, b5=manual'),
            ('latitude',         'f',  'degrees'),
            ('longitude',        'f',  'degrees'),
            ('altitude',         'h',  'meters signed'),
            ('speed',            'H',  'cm/s'),
            ('course',           'H',  'degrees*100'),
            ('hdop',             'H',  'hdop*100'),
            ('depth',            'H',  'decimeters'),
            ('max_depth',        'H',  'decimeters'),
            ('battery_voltage',  'H',  'millivolts'),
            ('battery_current',  'H',  'milliamps'),
            ('bus_voltage',      'H',  'millivolts'),
            ('leak_detected',    'B',  'boolean 0/1'),
            ('reserved',         'B',  'reserved'),
            ('time_unix',        'I',  'UTC unix seconds'),
            ('uptime_s',         'I',  'seconds since boot'),
        ],
    },

    # ----------------------------------------------------------------
    # MT: Configuration update (cloud -> device), ID 6
    # Zero values mean "no change".
    # ----------------------------------------------------------------
    'doris_config': {
        'id': 6,
        'fields': [
            ('iridium_interval_s',    'H',  'seconds between reports, 0=no change'),
            ('led_mode',              'B',  '0=no change, 1=off, 2=normal, 3=strobe'),
            ('neopixel_brightness',   'B',  '0=no change, 1-255'),
            ('power_save_voltage_mv', 'H',  'millivolts, 0=no change'),
            ('reserved',              'H',  'reserved'),
        ],
    },

    # ----------------------------------------------------------------
    # MT: Immediate command (cloud -> device), ID 7
    # ----------------------------------------------------------------
    'doris_command': {
        'id': 7,
        'fields': [
            ('command',  'B',  '0=nop,1=send_report,2=release,3=reset,4=reboot,5=enable_iridium,6=disable_iridium'),
            ('param',    'I',  'command-specific parameter'),
        ],
    },

    # ----------------------------------------------------------------
    # MO: Acknowledgment (device -> cloud), ID 8
    # ----------------------------------------------------------------
    'doris_ack': {
        'id': 8,
        'fields': [
            ('acked_msg_id',  'B',  'MT message ID being acknowledged'),
            ('result',        'B',  '0=OK, nonzero=error'),
        ],
    },
}

MISSION_STATES = {
    0: 'PRE_MISSION',
    1: 'SELF_TEST',
    2: 'MISSION',
    3: 'RECOVERY',
    4: 'FAILSAFE',
}

FAILSAFE_FLAGS = {
    0: 'LOW_VOLTAGE',
    1: 'CRITICAL_VOLTAGE',
    2: 'LEAK',
    3: 'MAX_DEPTH',
    4: 'NO_HEARTBEAT',
    5: 'MANUAL',
}

COMMAND_TYPES = {
    0: 'NOP',
    1: 'SEND_REPORT',
    2: 'RELEASE',
    3: 'RESET_STATE',
    4: 'REBOOT',
    5: 'ENABLE_IRIDIUM',
    6: 'DISABLE_IRIDIUM',
}


# ============================================================================
# Serialization / deserialization (SolarSurfer2-compatible API)
# ============================================================================

def get_header(name):
    return (START + chr(MESSAGES[name]['id'])).encode('ascii')


def get_struct_format(name):
    return ENDIAN + ''.join(f[1] for f in MESSAGES[name]['fields'])


def serialize(data):
    """Serialize a message dict (must include 'name' key)."""
    assert 'name' in data, "data must include 'name' key"
    name = data['name']
    definition = MESSAGES[name]
    fmt = get_struct_format(name)
    values = [data[f[0]] for f in definition['fields']]
    return get_header(name) + pack(fmt, *values)


def deserialize(data):
    """Deserialize raw bytes into a message dict (includes 'name' key)."""
    assert len(data) >= 2, "Data too short"
    assert chr(data[0]) == START, f"Bad start byte: 0x{data[0]:02x}"

    ids_to_name = {MESSAGES[n]['id']: n for n in MESSAGES}
    msg_id = data[1]
    assert msg_id in ids_to_name, f"Unknown message ID: {msg_id}"

    name = ids_to_name[msg_id]
    fmt = get_struct_format(name)
    payload = data[2:]
    expected_size = calcsize(fmt)
    assert len(payload) >= expected_size, \
        f"{name}: expected {expected_size} bytes, got {len(payload)}"

    names = [f[0] for f in MESSAGES[name]['fields']]
    values = unpack(fmt, payload[:expected_size])
    result = dict(zip(names, values))
    result['name'] = name
    return result


def format_report(report):
    """Pretty-print a deserialized doris_report."""
    lines = []
    lines.append(f"State:    {MISSION_STATES.get(report['mission_state'], '?')}")
    lines.append(f"GPS:      fix={report['gps_fix_type']} sats={report['satellites']}")
    lines.append(f"Position: {report['latitude']:.6f}, {report['longitude']:.6f}")
    lines.append(f"Alt:      {report['altitude']}m  Speed: {report['speed']/100:.2f}m/s  "
                 f"Course: {report['course']/100:.1f}°")
    lines.append(f"HDOP:     {report['hdop']/100:.2f}")
    lines.append(f"Depth:    {report['depth']/10:.1f}m  Max: {report['max_depth']/10:.1f}m")
    lines.append(f"Battery:  {report['battery_voltage']/1000:.3f}V  "
                 f"{report['battery_current']/1000:.3f}A")
    lines.append(f"Bus:      {report['bus_voltage']/1000:.3f}V")
    lines.append(f"Leak:     {'YES' if report['leak_detected'] else 'no'}")

    flags = []
    for bit, label in FAILSAFE_FLAGS.items():
        if report['failsafe_flags'] & (1 << bit):
            flags.append(label)
    lines.append(f"Failsafe: {', '.join(flags) if flags else 'none'}")
    lines.append(f"Time:     unix={report['time_unix']}  uptime={report['uptime_s']}s")
    return '\n'.join(lines)


# ============================================================================
# Self-test
# ============================================================================

if __name__ == "__main__":
    print(f"doris_report payload size: {calcsize(get_struct_format('doris_report'))} bytes "
          f"(wire: {calcsize(get_struct_format('doris_report')) + 2})")
    print(f"doris_config  payload size: {calcsize(get_struct_format('doris_config'))} bytes")
    print(f"doris_command payload size: {calcsize(get_struct_format('doris_command'))} bytes")
    print(f"doris_ack     payload size: {calcsize(get_struct_format('doris_ack'))} bytes")
    print()

    # Round-trip test: report
    test_report = {
        'name': 'doris_report',
        'mission_state': 3,
        'gps_fix_type': 3,
        'satellites': 12,
        'failsafe_flags': 0,
        'latitude': 37.702400,
        'longitude': -122.084100,
        'altitude': 0,
        'speed': 15,
        'course': 18000,
        'hdop': 120,
        'depth': 0,
        'max_depth': 452,
        'battery_voltage': 12400,
        'battery_current': 850,
        'bus_voltage': 5100,
        'leak_detected': 0,
        'reserved': 0,
        'time_unix': 1742400000,
        'uptime_s': 7200,
    }
    raw = serialize(test_report)
    print(f"Serialized report: {raw.hex()} ({len(raw)} bytes)")
    decoded = deserialize(raw)
    print(f"Round-trip OK: {decoded['latitude']:.6f}, {decoded['longitude']:.6f}")
    print()
    print(format_report(decoded))
    print()

    # Round-trip test: config
    test_config = {
        'name': 'doris_config',
        'iridium_interval_s': 300,
        'led_mode': 2,
        'neopixel_brightness': 128,
        'power_save_voltage_mv': 11500,
        'reserved': 0,
    }
    raw = serialize(test_config)
    print(f"Serialized config: {raw.hex()} ({len(raw)} bytes)")
    decoded = deserialize(raw)
    print(f"Config round-trip: interval={decoded['iridium_interval_s']}s "
          f"led={decoded['led_mode']} brightness={decoded['neopixel_brightness']}")
    print()

    # Round-trip test: command
    test_cmd = {
        'name': 'doris_command',
        'command': 1,
        'param': 0,
    }
    raw = serialize(test_cmd)
    print(f"Serialized command: {raw.hex()} ({len(raw)} bytes)")
    decoded = deserialize(raw)
    print(f"Command round-trip: cmd={COMMAND_TYPES[decoded['command']]}")
