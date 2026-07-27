# Doris-AGT-Firmware

**Oceanographic Drop Camera — Subordinate Safety Monitor & Comms Relay**

Firmware for the SparkFun Artemis Global Tracker (AGT) used on the Doris deep-sea
drop-camera platform. The AGT does **not** run the dive. It drives a GPIO35
release output that mirrors the Navigator's own release relay, and provides
independently guarded release and surface-power safety fallbacks alongside GPS,
Iridium, Meshtastic, and status LEDs.

## Mission Profile

1. **Pre-dive (surface)** — GPS lock, MAVLink up, optional Iridium test, LEDs
   indicate armed/not-armed, Meshtastic NMEA relay active.
2. **Dive (underwater)** — Lua script on ArduSub controls descent / on-bottom /
   ascent. AGT silently watches for failsafe conditions.
3. **Recovery (surfaced)** — Iridium position reports resume and the white
   strobe activates; Relay 1 cuts power only after the qualified BlueOS
   shutdown handshake.

## Architecture

### System States

The state machine has three states and **follows** the autopilot — it does not
drive the dive.

| State      | Trigger                                                                | AGT behavior                                                          |
|------------|------------------------------------------------------------------------|-----------------------------------------------------------------------|
| `PRE_DIVE` | Boot / `reset` command / Lua state ≤ 0 (`CONFIG` / `MISSION_START`)     | GPS to MAVLink + NMEA, Iridium test on demand, armed/not-armed LEDs   |
| `DIVING`   | Lua state 1–3 (`DESCENT` / `ON_BOTTOM` / `ASCENT`), or depth > 2 m     | LEDs off (or Lua-commanded), no Iridium TX                            |
| `RECOVERY` | Lua state 4, or independently when ascending + shallow + GPS fix       | White strobe and Iridium resume; Pi remains powered pending qualification/handshake |

State transitions are driven by `NAMED_VALUE_FLOAT "STATE"` from the Lua script
(`-1=CONFIG`, `0=MISSION_START`, `1=DESCENT`, `2=ON_BOTTOM`, `3=ASCENT`,
`4=RECOVERY`), with an independent depth+GPS surface detector as a backup so the
AGT can transition to `RECOVERY` even if the Lua script has crashed.

### Release and safe surface power

ArduSub sends `NAMED_VALUE_FLOAT RELAY` from source `1/1`. Only finite values
near 0 or 1 are accepted. `RELAY=1` latches GPIO35 ON, tolerates repeats, and
persists the active marker in EEPROM so an AGT reboot reasserts the output.
`RELAY=0` is accepted only after `RELEASE_MIN_HOLD_SEC` (1500 s) and independent
surface qualification. The AGT reports the output repeatedly as `REL_STAT`.

Manual `release_now`, valid Iridium `DORIS_CMD_RELEASE`, and guarded
leak/critical-voltage/heartbeat failsafes while `DIVING` share this controller.
Release never requests or implies Pi power cutoff.

Pi power uses a separate fail-closed protocol. Repeated fresh Lua `STATE=4`,
fresh shallow autopilot depth, and the AGT's own GPS fix must remain true for
`SURFACE_QUALIFY_MS`. AGT then repeats `PWR_SHDN=1`; BlueOS acknowledges with
`PWR_ACK=1` from `1/191`. GPIO4 cuts power only after that ACK and
`POWER_SHUTDOWN_FINAL_GRACE_MS`. Missing/stale data or no ACK leaves Pi power on.
Every AGT boot/reset restores Pi power.

AGT also repeats `AGT_CAP`: bit 0 declares AGT release ownership and bit 1
declares the safe surface power handshake. BlueOS must require both bits before
enabling v0.3 safety integration.

### Comms

- **MAVLink (USB, 57600 baud)** — `GPS_INPUT` to ArduSub for navigation,
  `SYSTEM_TIME` once the RTC has been synced from a valid GPS fix, periodic
  heartbeats. Component ID 192 (`MAV_COMP_ID_ONBOARD_COMPUTER2`).
- **Iridium 9603N** — Doris binary SBD telemetry (SolarSurfer2-compatible
  framing) plus MT command support. Transmits only in `RECOVERY` (or on
  demand via `iridium_test`).
- **Meshtastic RAK4603** — NMEA 0183 (`GPGGA` + `GPRMC`) via SoftwareSerial on
  J10 (D39/D40, 9600 baud). RAK4603 uses the AGT as an external GPS source.
- **u-blox ZOE-M8Q GPS** — 6 Hz, BBR-backed for fast warm starts, V_BCKP coin
  cell kept charged so a real UTC time is available within ~1 s of power-on.

### Lua → AGT control (MAVLink `COMMAND_LONG`)

| Command ID | Name              | Purpose                                  |
|------------|-------------------|------------------------------------------|
| 31010      | `LED_CONTROL`     | Lua-commanded LED pattern / colour       |
| 31011      | `MISSION_STATUS`  | Mission ready / status updates           |
| 31012      | `AGT_DEBUG`       | Dump firmware version, RockBLOCK IMEI, GPS diag |
| 31013      | `IRIDIUM_TEST`    | Send a one-off Iridium test message      |
| 31014      | `REBOOT`          | Soft reboot the AGT                      |

If the Lua script stops sending LED commands for `LUA_COMMAND_TIMEOUT_MS`
(10 s), the AGT reclaims LED authority and falls back to the state-driven
pattern.

## Quick Links

- **[Pinout Guide](docs/PINOUT_GUIDE.md)** — START HERE — visual connection guide
- **[State Machine](docs/STATE_MACHINE.md)** — control flow and transitions
- **[Mission Profile](docs/MISSION_PROFILE.md)** — deployment phases
- **[BlueOS Integration](docs/BLUEOS_INTEGRATION.md)** — Navigator / ArduSub interface
- **[Wiring Diagram](docs/WIRING_DIAGRAM.md)** — hardware connections
- **[Meshtastic Protocol](docs/MESHTASTIC_PROTOCOL.md)** — NMEA over J10
- **[Quick Start](docs/QUICK_START.md)** — get up and running fast
- **[Full Firmware Reference](README_FIRMWARE.md)** — complete feature reference
- **[Changelog](CHANGELOG.md)** — version history

## Hardware

### Core
- SparkFun Artemis Global Tracker (Apollo3, 48 MHz)
- Blue Robotics Navigator + Raspberry Pi (BlueOS / ArduSub)
- u-blox ZOE-M8Q GPS (on-board, with V_BCKP coin cell)
- Iridium 9603N satellite modem (on-board)
- Meshtastic RAK4603 on J10 Qwiic (D39/D40, 9600 baud NMEA in)
- WS2812B LED strip — 30 LEDs, RGBW, external 5 V

### Relays
- **Relay 1 — Power management** (GPIO4) — controls Navigator/Pi, camera, lights.
  Wired through **NC**: coil OFF = devices powered (safe default through MCU
  resets). Coil energizes only after qualified surface shutdown + BlueOS ACK.
- **Relay 2 — Electrolytic release** (GPIO35) — wired through **NO**, active
  HIGH. ON is persisted and latched until an explicit, guarded Lua OFF after the
  minimum hold. Lua mirrors the same request to a Navigator relay, so both
  controllers command a release; wire the actuator to exactly one of the two
  outputs. Power management (Relay 1) may only be used when the actuator is
  wired here, because cutting Pi power disables the Navigator output.

### Battery
4S LiPo or equivalent marine battery, sized for seafloor recording + multi-day
surface wait. Battery voltage is read from the autopilot over MAVLink; the
on-board PSM analog interface exists but is disabled by default.

```
┌─────────────────────────────────────────────┐
│              Doris drop camera              │
│                                             │
│  ┌──────────────┐         ┌──────────────┐  │
│  │ Navigator/Pi │◄──USB───┤     AGT      │  │
│  │   (ArduSub   │ MAVLink │ (this repo)  │  │
│  │  + Lua dive  │  57600  │              │  │
│  │   script)    │         └──┬────────┬──┘  │
│  └──────┬───────┘            │J10     │     │
│         │                NMEA│9600    │     │
│    ┌────▼─────┐         ┌────▼────┐ ┌─▼───┐ │
│    │ Camera / │         │Meshtastic│ │Irid.│ │
│    │ Lights   │         │ RAK4603  │ │9603N│ │
│    └──────────┘         └──────────┘ └─────┘ │
│         ▲                                    │
│   Relay 1 (NC, qualified + ACK power cutoff) │
│                                              │
│   Relay 2 (NO) ──► Electrolytic release      │
└─────────────────────────────────────────────┘
```

## Build & Upload

### Prerequisites
- [PlatformIO](https://platformio.org/) (CLI or VS Code extension)
- Apollo3 Arduino core is pinned in `platformio.ini` (pulled directly from
  SparkFun's repo at v2.2.1 — no extra setup needed)

### Environments

| Env                              | Use                                                                   |
|----------------------------------|-----------------------------------------------------------------------|
| `SparkFun_RedBoard_Artemis_ATP`  | Default firmware (`pio run`)                                          |
| `no-relays`                      | Same firmware with all relay drives no-op'd (bench testing)           |
| `selftest`                       | Standalone GPS + Iridium + NeoPixel verification image                |
| `native`                         | Host-side unit tests (`pio test -e native`) — no hardware required    |

### Common commands

```bash
git clone https://github.com/brianhbr/doris-agt-firmware.git
cd doris-agt-firmware
git submodule update --init --recursive

# Build + upload default firmware
pio run -t upload

# Build the no-relay variant
pio run -e no-relays -t upload

# Self-test image
pio run -e selftest -t upload

# Host-side tests (config_manager, doris_protocol, mission_data, state_machine)
pio test -e native

# Serial monitor (USB shares debug + MAVLink at 57600 baud)
pio device monitor -b 57600
```

CI builds the default, `no-relays`, and `selftest` images and publishes their
`.bin` files as workflow artifacts on every push. Pushing a version tag
(`vX.Y.Z`) additionally cuts a **GitHub Release** with the default and
`no-relays` `.bin` files attached (named `doris-agt-vX.Y.Z.bin` /
`doris-agt-no-relays-vX.Y.Z.bin`). The tag is embedded as `FIRMWARE_VERSION`
and reported over MAVLink.

```bash
# Cut a release: tag and push
git tag v0.1.0
git push origin v0.1.0
```

## Serial Commands

The USB serial port is shared between debug text and MAVLink. Text commands
work whenever the MAVLink parser is idle.

```
help                  Show available commands
status                Print state machine + failsafe status
gps                   Print last GPS fix
debug                 Firmware version, RockBLOCK IMEI, and GPS diagnostics
iridium_test          Queue a one-off Iridium test transmission
reset                 Force state machine back to PRE_DIVE
reboot                Soft reboot the AGT
release_now           Latch release ON through the guarded manual path
set_leak <0|1>        Force the leak flag (failsafe testing)
mesh_test             Send a text message over Meshtastic
mesh_test_gps         Send hardcoded NMEA over Meshtastic (link test)
mesh_send <text>      Send arbitrary text over Meshtastic

config                Print current configuration
save                  Persist configuration to EEPROM
set_iridium_interval <seconds>
set_meshtastic_interval <seconds>
set_mavlink_interval <ms>
set_timed_event <gmt|delay> <time> <duration_seconds>
set_power_save_voltage <volts>
enable_<feature> / disable_<feature>   (iridium, meshtastic, mavlink, psm, neopixels)
```

Defaults: Iridium 5 min, Meshtastic 10 s, MAVLink 200 ms (5 Hz), release minimum
hold 1500 s, Lua compatibility hold 7200 s, NeoPixels enabled, PSM disabled.

## Status LEDs

The 30-pixel WS2812B strip indicates state. The Lua script can override the
pattern during a dive via the `LED_CONTROL` MAVLink command; if it goes silent
for 10 s the AGT reclaims LED authority.

| Mode       | Pattern                | When                                            |
|------------|------------------------|-------------------------------------------------|
| `STANDBY`  | Slow spinning white    | Booting / waiting for systems                   |
| `READY`    | Spinning green         | `PRE_DIVE`, mission armed (GPS + autopilot OK)  |
| `ERROR`    | Pulsing red            | `PRE_DIVE`, not armed — do not deploy           |
| `DIVING`   | Off                    | Underwater, save power                          |
| `LUA`      | Lua-commanded          | Lua override during dive                        |
| `RECOVERY` | Flashing white beacon  | Surface recovery strobe                         |

Lua patterns: `OFF`, `SOLID`, `PULSE`, `CHASE`, `STROBE`, `RAINBOW`.

## Configuration

Most operational parameters live in `include/config.h` (depth thresholds, dive
duration grace, failsafe timeouts, baud rates, default intervals). Runtime-tunable
parameters (intervals, feature enables, timed-event, power-save voltage) are
stored in EEPROM and can be edited via the serial commands above or pushed
remotely via the Doris binary MT-config message over Iridium.

## License

See [LICENSE](LICENSE).

## Acknowledgments

- SparkFun for the Artemis Global Tracker hardware and Apollo3 Arduino core
- Blue Robotics for the Navigator and ArduSub stack
- Meshtastic and ArduPilot projects
