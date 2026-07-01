#!/usr/bin/env bash
# Flash a Holyiot nRF52840 dongle build (built via ./local_build_charybdis.sh
# or ./local_build_roba_kesb.sh) using Nordic's nrfutil DFU over USB-serial.
#
# WSL has no direct USB access, so this stages the .hex in the Windows
# Downloads folder and drives nrfutil.exe through powershell.exe:
#   nrfutil pkg generate --hw-version 52 --sd-req 0x00 ...
#   nrfutil dfu usb-serial -pkg ... -p <COM_PORT> -b <BAUD> -t 60
#
# Usage:
#   ./flash_holyiot.sh [artifact-name] [COM_PORT] [BAUD]
#
# Defaults:
#   artifact-name = charybdis_esb_dongle_holyiot
#   COM_PORT      = COM16
#   BAUD          = 115200
#
# Examples:
#   ./flash_holyiot.sh
#   ./flash_holyiot.sh charybdis_esb_dongle_holyiot COM16
#   ./flash_holyiot.sh roBakesb_dongle_holyiot COM7 115200

set -euo pipefail

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FIRMWARE_DIR="$REPO_DIR/firmware"
DOWNLOADS="/mnt/c/Users/charl/Downloads"

ARTIFACT="${1:-charybdis_esb_dongle_holyiot}"
COM_PORT="${2:-COM16}"
BAUD="${3:-115200}"

HEX_SRC="$FIRMWARE_DIR/$ARTIFACT.hex"

if [[ ! -f "$HEX_SRC" ]]; then
    echo "✗ $HEX_SRC not found. Build it first, e.g.:"
    echo "    ./local_build_charybdis.sh $ARTIFACT"
    exit 1
fi

mkdir -p "$DOWNLOADS"
cp "$HEX_SRC" "$DOWNLOADS/$ARTIFACT.hex"
rm -f "$DOWNLOADS/$ARTIFACT.zip"

DOWNLOADS_WIN="$(wslpath -w "$DOWNLOADS")"

echo "========================================="
echo "  Flashing: $ARTIFACT"
echo "  Staged:   $DOWNLOADS/$ARTIFACT.hex"
echo "  Port:     $COM_PORT @ ${BAUD} baud"
echo "========================================="

set +e
powershell.exe -NoProfile -NonInteractive -Command "
\$ErrorActionPreference = 'Stop'
Set-Location '$DOWNLOADS_WIN'
nrfutil pkg generate --hw-version 52 --sd-req 0x00 --application '.\\$ARTIFACT.hex' --application-version 1 '.\\$ARTIFACT.zip'
if (\$LASTEXITCODE -ne 0) { exit \$LASTEXITCODE }
nrfutil dfu usb-serial -pkg '.\\$ARTIFACT.zip' -p '$COM_PORT' -b $BAUD -t 60
exit \$LASTEXITCODE
"
status=$?
set -e

if [[ $status -ne 0 ]]; then
    echo "✗ Flash failed (exit $status)."
    exit "$status"
fi

echo "✓ Flash complete."
