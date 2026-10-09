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

`CONFIG_ZMK_SPLIT_ESB_HID_RELAY` in damex `zmk-feature-split-esb` v0.8.1 relays
**the keyboard, consumer and pointer reports**. `esb_hid_relay_central.c` subscribes to
`zmk_keycode_state_changed` and stages either `zmk_hid_get_keyboard_report()` or
`zmk_hid_get_consumer_report()` depending on the usage page; the idle keepalive
latches both concatenated, with a `BUILD_ASSERT` that the pair fits
`ZMK_SPLIT_ESB_MAX_PAYLOAD` (48 here, the pair is about half that). The dongle
registers ZMK's full report descriptor, so each report reaches the host under
its own report ID.

Pointer input arrives through a different door. `esb_hid_relay_pointer.c` is an
**input processor**, `zmk,input-processor-esb-relay-pointer`, which has to be
declared once and placed last in every input listener on the central
(`roBakesb_right_central.overlay`). Each listener then tees its final events —
after scaling and any xy→scroll mapping — into the relay's accumulator, which
rides out on every reply: motion and scroll sum between polls so a fast sensor
loses no counts, and a button change is queued like a key change so a click
shorter than one poll still reaches host B. The source file only compiles when
the DT node exists, so forgetting it means the pointer path silently does not
exist.

That input-processor design decides which *click* behaviors can relay, and it
cost this keymap its button bindings. `&bd` (damex `zmk-behavior-button-direct`)
and the auto-clicker both used to call `zmk_hid_mouse_button_press()` and
`zmk_endpoint_send_mouse_report()` directly, which never touches an input
listener, so neither could ever reach the relay — MB1/MB2/MB3 clicked the
central's host alone.

Both now go through the pipeline:

- Every mouse button in `roBakesb.keymap` is plain **`&mkp`**, which reports
  `BTN_0..` input events and lands in `mkp_input_listener`.
- `&autoclk` takes a `click-device = <&mkp>` phandle and reports its clicks onto
  that same device (`input_report_key`, sync on the last button of the mask),
  exactly as `behavior_mouse_key_press.c` does.

What `&bd` bought was a drain: on every release it forced ZMK's per-button press
count to zero, so a release lost over ESB healed on the next click instead of
wedging the button until a power-cycle. The ESB link heals a lost key *release*
itself through the keepalive position bitmap (since v0.2.x), so the drain was
the second line of defence rather than the first — but if a stuck button ever
shows up again, the real fix is a drain-aware behavior that reports through the
input pipeline instead of into `zmk_hid`, which is a change for the `&bd`
module, not for this config.

> Media keys are new in v0.7.x, pointer input in v0.8.x. Under v0.6.4 — what
> this branch was first built and flashed against — the relay carried the
> keyboard report only.

Host lock indicators (caps, num, scroll) now travel the other way: the dongle
forwards its `ZMK_HID_REPORT_ID_LEDS` output report up to the central, so host
B's lock state can drive central-side widgets. It is opt-in and currently off —
it needs `CONFIG_ZMK_HID_INDICATORS=y` on both the dongle and the central, and
neither sets it. Nothing in this topology displays indicators yet, so there is
nothing to gain until something does.

The two endpoints are **no longer live at the same time**. Since v0.8.0 the
central pauses the relay while it has a host of its own: `follow_endpoint()`
sets `paused` whenever the selected endpoint is not `ZMK_TRANSPORT_NONE`, then
latches released keyboard/consumer reports and zeroes the pointer so the dongle
cannot sit on a held key or button. With `CONFIG_ZMK_USB=y` on this half that
means host B goes quiet for as long as the right half's USB is enumerated, and
takes over when it is unplugged. That is the behaviour we asked damex for; note
it keys off the *selected endpoint*, so a connected BLE profile would pause the
relay too (irrelevant here, `ZMK_BLE=n`). The pause is event-driven, so it
engages when USB enumerates after boot rather than at init.

Latency and healing are set by two Kconfig values, one per side, kept equal:

- `ZMK_SPLIT_ESB_HID_RELAY_POLL_MS=2`, dongle side — how often the relay pings
  the central, which is what bounds added HID latency. A relay-role peripheral
  never backs off to the idle window, it polls at exactly this rate. 2 ms rather
  than 1 because the dongle's USB HID endpoint drains one report per 1 ms frame
  and a single poll can return keyboard + consumer + pointer.
- `ZMK_SPLIT_ESB_HID_RELAY_KEEPALIVE_MS=2`, central side — how often the central
  refreshes the *idle* reply, so a dropped release heals within one period and a
  poll that finds an empty queue still gets a fresh report. Real changes do not
  wait for it: they are queued and go out on the next poll.

Both options depend only on `ZMK_SPLIT_ESB_HID_RELAY`, so Kconfig accepts either
on either side, but `POLL_MS` is read in `hop_peripheral.c` and `KEEPALIVE_MS` in
`esb_hid_relay_central.c` — set on the wrong side, each is a silent no-op.
`USB_HID_POLL_INTERVAL_MS` needs no setting: the module defaults it to 1 for
relay peripherals.

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
exactly. `relay_pointer` is repeated as the *last* entry of each for the same
reason: an override that omits it would relay nothing while that layer is held.

### Dongle-side USB

`roBakesb_relay.conf` sets `CONFIG_ZMK_USB=n` deliberately. The relay registers
`HID_0` itself in `esb_hid_relay_usb.c` using ZMK's report descriptor, and ZMK's
own `usb_hid.c` would claim the same Zephyr HID device. `CONFIG_USB_DEVICE_HID`,
`USB_DEVICE_STACK` and `USB_DEVICE_INITIALIZE_AT_BOOT` come in via
`select`s on `ZMK_SPLIT_ESB_HID_RELAY`.

Because `ZMK_USB` is off, `ZMK_USB_LOGGING` is unusable, so `CONFIG_LOG=y` is set
directly; the board's `cdc_acm_console_uart` is already `zephyr,console`, so
logs come out a CDC ACM interface alongside the HID one.

The two sides are deliberately asymmetric about logging: the dongle keeps its
CDC console because it is the only window into an untested topology, while
`roBakesb_right_central` is `CONFIG_LOG=n` like `roBakesb_right`. The central
runs the retransmit budget tuned in `roBakesb_addr.dtsi` and has no log backend
anyway — its only snippet is `studio-rpc-usb-uart`, an RPC transport. Get it
back with `-DEXTRA_CONF_FILE=roBakesb_debug.conf`.

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
