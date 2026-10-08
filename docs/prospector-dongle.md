# Prospector dongle (ESB central with a 1.69" round LCD)

A third dongle option for the roBa KESB split, alongside the holyiot (RGB LED)
and nice!nano (OLED) builds. Hardware is [Prospector](https://github.com/carrefinho/prospector):
a XIAO nRF52840 plus a Waveshare 1.69" round LCD, with an optional APDS9960
ambient light sensor for auto brightness.

| | |
|---|---|
| Board | `xiao_ble//zmk` |
| Shields | `roBakesb_prospector prospector_adapter` |
| Artifact | `firmware/roBakesb_dongle_prospector.uf2` |
| Flashing | double-tap reset, drag the `.uf2` onto the bootloader drive |

The ESB side is unchanged, so **the halves and the lariska need no reflash** —
this swaps one central for another on the same link table.

```bash
./local_build_roba_kesb.sh roBakesb_dongle_prospector
```

## What it shows

Prospector's stock *classic* status screen, which is what covers the two things
the LED dongles could not:

- **Layer roller** — the highest active layer, by name.
- **Battery bar per peripheral** — one bar each for left, right and lariska.
- Per-peripheral connection indicator, output/profile indicator, active
  modifiers, caps-word indicator.

Three other layouts ship with the module (`field`, `operator`, `radii`); select
one with `CONFIG_PROSPECTOR_STATUS_SCREEN_FIELD=y` and so on. All four carry the
same battery/connection widget code, so the compat notes below apply whichever
you pick.

Our own `ROBA_DONGLE_BATTERY_DISPLAY` screen is **off** on this shield. It also
defines `zmk_display_status_screen()`, so enabling both is a duplicate symbol at
link time, and Prospector's screens already draw what it drew.

## Layer names

The roller reads each layer's `display-name`. `roBakesb.keymap` only had the
deprecated `label`, so all 15 layers now carry both — `display-name` for the
roller, `label` left alone so nothing else that reads it changes. Adding it also
clears the `'label' is marked as deprecated` warnings the build has been
emitting for every layer.

`CONFIG_PROSPECTOR_LAYER_NAME_UPPERCASE` defaults to `y`, so "Tile Mac" renders
as "TILE MAC".

## Automatic layer switching

`CONFIG_ZMK_DESKHOP_SYNC_REPORT` carries over from the other dongles unchanged:
DeskHop switches host, the vendor HID feature report lands on the central, and
the base layer follows (A=0 Mac, B=1 Windows, mask `0x07` so Gaming cannot
shadow the selection). The difference here is that the layer roller shows the
switch as it happens.

## ESB compatibility

Prospector's widgets are written against ZMK's **split-BLE** central. Three
things had to be bridged to make them work over the damex ESB transport, all of
them in this repo rather than in a fork of the module.

### 1. Peripheral battery — already worked

No change needed. `zmk-feature-split-esb` raises ZMK's standard
`zmk_peripheral_battery_state_changed` itself (`src/central.c`), carrying the
pipe number as `source`, and the widget subscribes to exactly that. It does
require `ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING=n`, which is how the ESB
module decides to raise the event at all.

### 2. Peripheral count — `ZMK_SPLIT_BLE_PERIPHERAL_COUNT` did not exist

The widgets size their per-peripheral arrays with `ZMK_SPLIT_BLE_PERIPHERAL_COUNT`,
which `zmk/app/include/zmk/ble.h` expands verbatim to
`CONFIG_ZMK_SPLIT_BLE_CENTRAL_PERIPHERALS`. ZMK declares that symbol inside
`if ZMK_SPLIT && ZMK_SPLIT_BLE` (`app/src/split/bluetooth/Kconfig`), so on an
ESB-only central it does not exist and the widget fails to compile.

The repo `Kconfig` now declares it unconditionally, defaulting to 3 when
`ZMK_SPLIT_ESB && !ZMK_SPLIT_BLE`. Note this makes the
`CONFIG_ZMK_SPLIT_BLE_CENTRAL_PERIPHERALS=3` line in the dongle confs *live* —
on the other dongles it was always inert decoration.

### 3. Connection state — nothing raised the event

The widgets track per-peripheral connection via `zmk_split_central_status_changed`.
That event is not a ZMK one at all — the module declares and implements it
itself (`include/zmk/events/split_central_status_changed.h`,
`src/events/split_central_status_changed.c`), because upstream ZMK has no such
event at this fork point. The only thing that *raises* it is the module's own
BLE connection observer, so with `ZMK_SPLIT_BLE=n` the connection indicators
would sit permanently disconnected.

`src/esb_central_display_compat.c` (`ROBA_ESB_CENTRAL_DISPLAY_COMPAT`)
subscribes to the ESB module's own `zmk_split_esb_peripheral_changed` and
re-raises it as `zmk_split_central_status_changed`, mapping `source` → `slot`.
It deliberately does *not* declare or implement the event — the module owns
both, and a second `ZMK_EVENT_IMPL` is a duplicate symbol at link time.

For the same reason the option is gated on `SHIELD_PROSPECTOR_ADAPTER` rather
than plain `ZMK_DISPLAY`. The module only compiles its event sources under its
own shield, so on the nice!nano OLED dongle — also an ESB central with a
display and no split BLE — `raise_zmk_split_central_status_changed` is
undefined at link.

This is the central-side twin of the existing
`src/esb_peripheral_display_compat.c`, which fills in the split-BLE symbol the
nice!view peripheral screens link against.

### 4. The module does not build without BLE — fork

Two places assume a BLE split central, and no Kconfig switches them off:

- `src/split/bluetooth/central_status_changed_observer.c` is compiled
  unconditionally and cannot build here: it reads `info.le.phy->rx_phy`, and
  that member only exists under `CONFIG_BT_USER_PHY_UPDATE`, which needs a BT
  stack this config does not have. `CONFIG_LOG=n` is no escape — Zephyr still
  type-checks `LOG_DBG` arguments.
- `classic/output.c` links against `zmk_ble_profile_is_connected`,
  `zmk_ble_profile_is_open`, `zmk_ble_active_profile_index` and the
  `ble_active_profile_changed` event.

`charlesmst/prospector-zmk-module` branch `compat/no-split-ble` gates the first
on `CONFIG_ZMK_SPLIT_BLE` and the BLE-profile half of the second on
`CONFIG_ZMK_BLE`, leaving the endpoint half alone so the widget still shows the
active output — USB, permanently, on a wired dongle. Nothing changes for BLE
builds, since both symbols are set in any BLE config.

The patch is upstreamable as-is. If it lands in `carrefinho/prospector-zmk-module`,
point `west.yml` back at upstream and delete the fork.

### Battery bar ordering

Prospector's README warns that the battery sub-widgets follow BLE pairing order,
so you should pair left before right. That does not apply here: there is no
pairing on ESB, the slot index *is* the pipe number, and the order is fixed at
build time by `roBakesb_addr.dtsi` — pipe 0 left, 1 right, 2 lariska.

## Module pin

`config/west.yml` pins `prospector-zmk-module` to **`feat/new-status-screens`**,
not `main`. `main` targets ZMK v0.3 / Zephyr 3.5 and the board name
`seeeduino_xiao_ble`; this workspace is Zephyr 4.1 with hardware-model-v2 names
(`xiao_ble//zmk`), which only that branch supports.
