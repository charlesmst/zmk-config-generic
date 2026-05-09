# ESB Agent Handoff - 2026-05-09

## Workspace

- Config repo: `/home/charlesstein/personal/zmk-config-roBa`
- Patched app tree: `/home/charlesstein/personal/zmk-v030-sidechannel/app`
- West workspace: `/tmp/roba-west`

## User Requirement

After every flash, verify both of these before trusting the result:

1. No UF2 bootloader volume remains mounted.
2. The board has re-enumerated as the application again via normal CDC and/or HID.

## Files Changed

### 1. Peripheral ESB TX reliability

File:

- `/home/charlesstein/personal/zmk-v030-sidechannel/app/src/split/esb/peripheral.c`

Changes:

- Added `tx_pending` and `current_tx_packet` handling so app packets remain pending until confirmed.
- On `ESB_EVENT_TX_FAILED`, now calls `esb_pop_tx()` to remove the failed lower-level ESB FIFO entry.
- Poll packets are still dropped on failure.
- `esb_write_payload()` failure now uses delayed retry instead of immediate hot-loop retry.
- Poll timer only enqueues a poll when no real packet is pending and the app TX queue is empty.

Why:

- This ESB fork does not remove failed PTX payloads from the ESB TX FIFO automatically.
- Without `esb_pop_tx()` on `TX_FAILED`, the same packet gets duplicated into the hardware FIFO until it fills.
- That was the cause of the old `-12` write-failure storm.

### 2. Charybdis peripheral HID split injection

File:

- `/home/charlesstein/personal/zmk-config-roBa/config/boards/shields/Test/charybdis_esb_peripheral.conf`

Added:

- `CONFIG_ROBA_ESB_SANITY_INJECT_KEY=y`

Why:

- Without this, the HID test-key command only sent a local HID report and did not inject split ESB press/release events.

### 3. Central timeout-release failsafe

File:

- `/home/charlesstein/personal/zmk-v030-sidechannel/app/src/split/esb/central.c`

Changes:

- Added `release_held_position(...)`.
- In `stuck_key_work_cb()`, if a source has timed out based on `CONFIG_ZMK_SPLIT_ESB_CONNECTION_TIMEOUT_MS`, the central now synthesizes releases for any still-held positions from that source.
- Existing stale-key force-release behavior remains in place.

Why:

- Even with improved peripheral TX behavior, a dead link or lost release can still leave the central thinking a key is held.

## Build Outputs

- Peripheral UF2:
  - `/tmp/roba-zmk-build/build-charybdis_esb_peripheral-nice_nano_v2/zephyr/zmk.uf2`
- Dongle UF2:
  - `/tmp/roba-zmk-build/build-charybdis_esb_dongle-nice_nano_v2/zephyr/zmk.uf2`

## Flash Status

- Peripheral build succeeded and was flashed.
- Dongle build succeeded and was flashed.
- Dongle needed manual UF2 entry and then `--no-trigger` flashing.

Working dongle flash command:

```sh
python3 /home/charlesstein/personal/zmk-config-roBa/scripts/flash_esb_sanity.py charybdis_dongle --no-trigger --drive F --wait 120
```

## Important Verification Results

### Post-flash safety

Verified after latest flashes:

- No UF2 volume remained mounted.
- Both boards re-enumerated as application devices.
- Current serial ports were `COM10` and `COM11`.

### HID enumeration

Latest list showed:

- Vendor HID `usage 0x0001`, product `Charybdis ESB R`
- Vendor HID `usage 0x0002`, product `Charybdis ESB`

That matches:

- Peripheral control path: `--usage 0x0001`
- Dongle control path: `--usage 0x0002`

## Important Log Findings

Latest useful capture:

- `/home/charlesstein/personal/zmk-config-roBa/logs/esb-sanity/2026-05-09T002736-151Z`

In that run, the peripheral HID-triggered split path worked:

- [`combined.log:36273`](/home/charlesstein/personal/zmk-config-roBa/logs/esb-sanity/2026-05-09T002736-151Z/combined.log:36273)

Observed there:

- `roba_hid: test key command received`
- `esb: peripheral queue key source=0 position=0 pressed=1`
- `roba_hid: split position=0 pressed=1 err=0`
- `esb: peripheral queue key source=0 position=0 pressed=0`
- `roba_hid: split position=0 pressed=0 err=0`

The old catastrophic failure mode is gone in this run:

- No `ESB payload write failed: -12`
- No `esb: peripheral write failed err=-12`

## Remaining Gap

The remaining proof point is still missing:

- No confirmed dongle-side `central received` / `central deliver` lines for this same injected key event.
- `COM11` emitted very little data in the latest session, so there is not yet a complete end-to-end proof run.

So:

- Transport behavior is materially improved.
- The old FIFO-overflow failure mode appears fixed.
- The stuck-key issue is not yet proven closed end-to-end.

## Useful Commands

### HID list

```sh
script=$(wslpath -w /home/charlesstein/personal/zmk-config-roBa/tools/deskhop_sync_test.py)
powershell.exe -NoProfile -Command "& { param(\$script) py \$script --vid 0x1D50 --pid 0x615E --list }" "$script"
```

### Trigger peripheral HID test key

```sh
script=$(wslpath -w /home/charlesstein/personal/zmk-config-roBa/tools/deskhop_sync_test.py)
powershell.exe -NoProfile -Command "& { param(\$script) py \$script --vid 0x1D50 --pid 0x615E --usage 0x0001 --test-key }" "$script"
```

### Capture logs

```sh
python3 /home/charlesstein/personal/zmk-config-roBa/scripts/capture_esb_sanity_logs.py --port a=COM10 --port b=COM11 --duration 20
```

### Post-flash verification

```sh
powershell.exe -NoProfile -Command "Get-Volume | Where-Object { \$_.FileSystemLabel -match 'NICENANO|NICE_NANO|NICE!NANO|UF2' -or (\$_.DriveLetter -in @('F','G')) } | Select-Object DriveLetter,FileSystemLabel,DriveType,HealthStatus | Format-Table -AutoSize"
powershell.exe -NoProfile -Command "Get-CimInstance Win32_SerialPort | Select-Object DeviceID,Name,PNPDeviceID | Format-Table -AutoSize"
```

## Recommended Next Steps

1. Reproduce again with both boards freshly connected.
2. Capture both `COM10` and `COM11`.
3. Trigger `--usage 0x0001 --test-key`.
4. Confirm dongle-side `central received` and `central deliver` press/release lines in the same run.
5. If stuck-key behavior remains, intentionally break the link and confirm the new timeout-release path fires.
6. Keep enforcing the post-flash "not in UF2 mode" checks after every flash.
