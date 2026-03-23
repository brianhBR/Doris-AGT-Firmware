# Remote flash script: build locally, upload to Pi, flash Artemis via SVL bootloader
# Requires: SSH key auth to the Pi (ssh-copy-id pi@<host>), Python 3 + pyserial on Pi

# ── Configuration ──────────────────────────────────────────────────────────────
$PI_HOST      = "192.168.2.2"
$PI_USER      = "pi"
$PI_SERIAL    = "/dev/ttyUSB0"        # Artemis USB serial on the Pi (check with: ls /dev/ttyUSB* /dev/ttyACM*)
$SVL_BAUD     = 115200
$REMOTE_DIR   = "/tmp/agt-flash"

$LOCAL_BIN    = ".pio\build\SparkFun_RedBoard_Artemis_ATP\firmware.bin"
$LOCAL_SVL    = "$env:USERPROFILE\.platformio\packages\framework-arduinoapollo3\tools\uploaders\svl\svl.py"
# ───────────────────────────────────────────────────────────────────────────────

$ErrorActionPreference = "Stop"

Write-Host "`n=== AGT Remote Flash ===" -ForegroundColor Cyan

# 1. Build
Write-Host "`n[1/4] Building firmware..." -ForegroundColor Yellow
pio run
if ($LASTEXITCODE -ne 0) { Write-Host "Build failed." -ForegroundColor Red; exit 1 }

# 2. Copy files to Pi
Write-Host "`n[2/4] Copying firmware + SVL uploader to Pi..." -ForegroundColor Yellow
ssh "${PI_USER}@${PI_HOST}" "mkdir -p ${REMOTE_DIR}"
scp $LOCAL_BIN "${PI_USER}@${PI_HOST}:${REMOTE_DIR}/firmware.bin"
scp $LOCAL_SVL "${PI_USER}@${PI_HOST}:${REMOTE_DIR}/svl.py"
if ($LASTEXITCODE -ne 0) { Write-Host "SCP failed." -ForegroundColor Red; exit 1 }

# 3. Flash on Pi (stop serial consumers, run SVL, restart)
Write-Host "`n[3/4] Flashing Artemis on Pi..." -ForegroundColor Yellow
$flashScript = @"
set -e
echo '--- Ensuring pyserial is installed ---'
pip3 install --quiet pyserial 2>/dev/null || python3 -m pip install --quiet pyserial

echo '--- Stopping processes using ${PI_SERIAL} ---'
# Kill anything holding the serial port (mavlink-router, mavproxy, screen, etc.)
sudo fuser -k ${PI_SERIAL} 2>/dev/null || true
sleep 1

echo '--- Flashing Artemis via SVL ---'
python3 ${REMOTE_DIR}/svl.py ${PI_SERIAL} -b ${SVL_BAUD} -f ${REMOTE_DIR}/firmware.bin -v

echo '--- Flash complete ---'
"@

ssh "${PI_USER}@${PI_HOST}" $flashScript
if ($LASTEXITCODE -ne 0) { Write-Host "Flash failed." -ForegroundColor Red; exit 1 }

# 4. Done
Write-Host "`n[4/4] Cleaning up..." -ForegroundColor Yellow
ssh "${PI_USER}@${PI_HOST}" "rm -rf ${REMOTE_DIR}"

Write-Host "`n=== Flash complete! ===" -ForegroundColor Green
Write-Host "The Artemis has rebooted with new firmware."
Write-Host "Any BlueOS serial endpoints using ${PI_SERIAL} should reconnect automatically.`n"
