# ESB HID relay topology (right half as central, dongle as relay)

An alternative wiring of the roBa KESB split that drops the dongle from the
critical path and uses it only as a second USB HID endpoint.

| Device | Shield | Board | ESB role | Pipe |
|---|---|---|---|---|
| Right half | `roBakesb_right_central` | `xiao_ble//zmk` | central, owns the keymap, USB HID to host A | 1 (`role = "self"`) |
| Left half | `roBakesb_left` | `xiao_ble//zmk` | peripheral | 0 |
| Lariska mouse | `roBakesb_lariska` | `nice_nano@2.0.0//zmk` | peripheral | 2 |
| Holyiot dongle | `roBakesb_relay` | `holyiot_dongle_1k` | relay, USB HID to host B | 3 |

Left and lariska are **unchanged** — the same firmware serves this topology and
the classic dongle-central one. Only the right half and the dongle get new
builds, and the classic `roBakesb` dongle targets are still in `build.yaml`.

## Artifacts

| Target | Output |
|---|---|
| `roBakesb_right_central` | `firmware/roBakesb_right_central.uf2` |
| `roBakesb_relay_holyiot` | `firmware/roBakesb_relay_holyiot.hex` (DFU, see the flashing section of the top-level README) |
| `roBakesb_left` | `firmware/roBakesb_left.uf2` (unchanged) |
| `roBakesb_lariska` | `firmware/roBakesb_lariska.uf2` (unchanged) |

Local build:

```bash
./local_build_roba_kesb.sh roBakesb_right_central
./local_build_roba_kesb.sh roBakesb_relay_holyiot
```

## What the relay actually carries

`CONFIG_ZMK_SPLIT_ESB_HID_RELAY` in damex `zmk-feature-split-esb` v0.6.4 relays
**the keyboard report only**. `esb_hid_relay_central.c` subscribes to
`zmk_keycode_state_changed` and fans out `zmk_hid_get_keyboard_report()`; there
is no mouse, consumer or indicator path. So on host B you get keys and
modifiers, and nothing from the trackball, the lariska mouse, the mouse buttons
or the media keys.

Both endpoints are live at the same time: the central types on host A over its
own USB and the dongle repeats the same report to host B. This is a mirror, not
an output switch.

Latency and healing are set by two Kconfig values, kept equal on both sides:

- `ZMK_SPLIT_ESB_HID_RELAY_POLL_MS=4` — how often the relay pings the central,
  which is what bounds added HID latency.
- `ZMK_SPLIT_ESB_HID_RELAY_KEEPALIVE_MS=4` — how often the central re-stages the
  current report, so a dropped release report heals within one period.

## How the pieces fit

### Link table

`roBakesb_addr.dtsi` gained a fourth peripheral, `esb_relay` (pipe 3, prefix
`0xE4`, `role = "relay"`), left `status = "disabled"`. `DT_FOREACH_CHILD_STATUS_OKAY`
skips disabled children, so the classic dongle-central builds still see exactly
the three pipes they always did. `roBakesb_right_central.overlay` and
`roBakesb_relay.overlay` are the only two that set it okay.

`roBakesb_right_central.overlay` also sets `&esb_right { role = "self"; }`. That
keeps pipe 1 in the table (address setup) while excluding it from the central's
source-id enumeration and connection tracking. The central has no chosen
`zmk,esb-self`; only peripherals need one.

### Trackball moved from split to local

On `roBakesb_right`, the PMW3610 fed `trackball_split` (a `zmk,input-split`)
with `sigmoid_accel` applied *on the peripheral*, before transmission, so
acceleration ran ahead of the dongle's per-layer overrides.

On `roBakesb_right_central` the trackball is a local device bound straight to
`trackball_listener`. A layer override in `zmk,input-listener` **replaces** the
base processor list rather than extending it (`input_listener.c`: `if
(!override->process_next) return 0;`), so `sigmoid_accel` is repeated as the
first entry of the `scroll` and `snipe` overrides to reproduce the old pipeline
exactly.

### Dongle-side USB

`roBakesb_relay.conf` sets `CONFIG_ZMK_USB=n` deliberately. The relay registers
`HID_0` itself in `esb_hid_relay_usb.c` using ZMK's report descriptor, and ZMK's
own `usb_hid.c` would claim the same Zephyr HID device. `CONFIG_USB_DEVICE_HID`,
`USB_DEVICE_STACK` and `USB_DEVICE_INITIALIZE_AT_BOOT` come in via
`select`s on `ZMK_SPLIT_ESB_HID_RELAY`.

Because `ZMK_USB` is off, `ZMK_USB_LOGGING` is unusable, so `CONFIG_LOG=y` is set
directly; the board's `cdc_acm_console_uart` is already `zephyr,console`, so
logs come out a CDC ACM interface alongside the HID one.

`config/boards/shields/roBakesb/boards/roBakesb_relay/holyiot_dongle_1k.conf`
turns off `ROBA_PERIPHERAL_BATTERY_LED` and `ROBA_LAYER_LED`. Zephyr matches
`boards/<board>.conf` on the board alone, so the classic dongle's LED settings
would otherwise land on this shield too — and both are central-only.

## Layer switching / DeskHop

`CONFIG_ZMK_DESKHOP_SYNC_REPORT` lives on `roBakesb_right_central` only. The
right half is the wired side, so the DeskHop vendor HID feature reports arrive
there and drive the base-layer switch exactly as they did on the dongle. The
relay dongle has no keymap and no sync config; it just mirrors whatever report
the central ends up producing.
