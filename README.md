# zmk-config-roBa-charybdis-esb

## Flashing the Holyiot nRF52840 dongle from WSL

The Holyiot nRF52840 USB dongle does not flash like a UF2 board. The working path is:

1. Build the dongle firmware in WSL and produce a `.hex`.
2. Use the Windows `nrfutil` binary to generate a Nordic Secure DFU `.zip`.
3. Manually put the dongle into DFU mode with the onboard magnetic reset.
4. Flash the DFU package over the COM port that appears in DFU mode.

### Prerequisites

- WSL repo path:
  - `/home/charlesstein/personal/zmk-config-roBa-charybdis-esb`
- Windows `nrfutil` installed with `winget`:

```powershell
winget install --id NordicSemiconductor.nrfutil --exact --accept-package-agreements --accept-source-agreements
nrfutil install device
```

- The legacy `nrf5sdk-tools` plugin is also needed for `pkg` and `dfu usb-serial`. On this machine it was already present:

```powershell
nrfutil list
```

### Artifact

The firmware flashed in this session was:

```text
C:\Users\charl\Downloads\charybdis_esb_dongle_holyiot.hex
```

### 1. Generate the DFU package

Run from Windows PowerShell:

```powershell
cd C:\Users\charl\Downloads
nrfutil pkg generate --hw-version 52 --sd-req 0x00 --application .\charybdis_esb_dongle_holyiot.hex --application-version 1 .\charybdis_esb_dongle_holyiot.zip
```

This produces:

```text
C:\Users\charl\Downloads\charybdis_esb_dongle_holyiot.zip
```

### 2. Put the dongle into DFU mode

- Plug in the Holyiot dongle.
- Bring a magnet close to the reset area on the board.
- Confirm the red LED shows the breathing/fade pattern.

In normal application mode, the board may enumerate as `nRF52 Connectivity`. In DFU mode, it re-enumerates as `Open DFU Bootloader`, and the COM port can change.

### 3. Find the DFU COM port

Run:

```powershell
nrfutil device list --traits nordicDfu
```

Expected output shape:

```text
FDF381B1AD29
Product         Open DFU Bootloader
Ports           COM16
Traits          nordicDfu, nordicUsb, serialPorts, usb
```

The important value is the DFU-mode COM port. In this successful flash it was `COM16`.

### 4. Flash the package

Run:

```powershell
nrfutil dfu usb-serial -pkg .\charybdis_esb_dongle_holyiot.zip -p COM16 -b 115200 -t 60
```

Successful result:

```text
Device programmed.
```

### Notes

- Do not reuse the COM port from normal app mode. In this session the board first appeared on `COM26` as `nRF52 Connectivity`, then changed to `COM16` in DFU mode.
- `nrfutil device program --firmware ...` saw the board but timed out on this setup. `nrfutil dfu usb-serial` against the DFU COM port worked reliably.
- If the board does not answer DFU commands, it is usually not actually in DFU mode yet.
- If USB DFU stops working entirely, the fallback is SWD flashing with an external debugger.
