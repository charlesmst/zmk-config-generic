# Keychron B1 Pro as a fourth ESB peripheral

The Keychron B1 Pro (ultra-slim 75%, US/ANSI) joins the roBakesb link as a
fourth key peripheral next to left, right and lariska. The Prospector dongle
stays the central: it owns the keymap, the layers and the USB connection, and
the Keychron only sends key positions, like the roBa halves do. Keychron's own
2.4 GHz dongle stops working once the keyboard runs this firmware.

The board port was brought up first as a plain standalone keyboard on upstream
ZMK (next section) and runs on a retail B1 Pro (PID `0x071a`). The ESB setup
after it builds on the same board files. "Unverified" marks what has not been
tried on the keyboard yet.

## Step 1: standalone on upstream ZMK

`keychron-config/` is a separate ZMK user config: its own `west.yml` (stock
`zmkfirmware/zmk` pinned to a commit, none of the roBakesb modules), its own
build matrix, and the `keychron` job in `.github/workflows/build.yml`. It builds
the same `keychron_b1_pro` board the ESB shield uses, so whatever the keyboard
proves about the board carries over.

```bash
./local_build_keychron.sh                  # firmware/keychron_b1_pro.uf2
./local_build_keychron.sh keychron_b1_pro_settings_reset
```

The script keeps an upstream-ZMK west workspace at `~/personal/zmk-upstream-ws`
(`KEYCHRON_WS`), created on first run with this workspace as its path cache.

What it is: a normal USB + BLE keyboard, Mac layout at the bottom.

| Input | Does |
|---|---|
| Mac/Win switch | Win holds the Win layer (Cmd/Opt become Win/Alt), as stock |
| BT/2.4G/Cable switch | BT prefers BLE; Cable and 2.4G prefer USB and fall back to BLE |
| Fn + F1/F2, F7-F12 | brightness, media |
| Fn + 1/2/3, Fn + Backspace | BLE profile 1/2/3, forget the current profile |
| Fn + U / Fn + B | force USB / BLE output |
| Fn + `-` held 3 s | UF2 bootloader, the same combo as stock |

What to check on the keyboard, in this order: it enumerates over USB and every
key types (a wrong pin is a dead row or column; a wrong transform is the wrong
character); a modifier held with letters on its own row types cleanly (Left
Shift and Q-T, A-F, Z-B are all row 0; chatter there means the columns are being
driven while idle, see below); the Mac/Win switch changes the bottom row; BLE
pairs and types with the switch on BT; Fn + `-` brings up `NRF52BOOT`.

### What the retail board needed that Keychron's source does not say

Read off the stock v1.0.1 image (`CURRENT.UF2` on `NRF52BOOT`) of a `0x071a`
unit, and checked against what this port builds:

- *Key positions* are Keychron's `b1n` map, not `b1`. The image holds the full
  82-entry matrix-to-position table, and it matches `b1n` exactly.
- *Polarity* is inverted from their tree: rows `ACTIVE_LOW | PULL_UP`, columns
  `ACTIVE_LOW`. Their source, and their downloadable v1.00 image for the same
  PID, are active-high with pull-downs, so the PCB changed within `0x071a`.
- *No per-key diodes.* Keychron's matrix driver switches each column back to an
  input after scanning it, and suppresses ghosts in software. Upstream ZMK keeps
  idle columns driven, which made every key on Left Shift's row chatter with it
  held. The columns are open-drain here instead (same effect, no driver
  change), with Keychron's 8 us / 5 us settle times. Three keys on the corners
  of a matrix rectangle can still ghost the fourth; nothing here hides that.

## Flashing

Confirmed on the retail unit (PID `0x071a`):

- The stock bootloader is the Adafruit nRF52 UF2 bootloader. Its drive is
  `NRF52BOOT`, and Keychron's own update files are UF2s for family
  `0xADA52840` starting at `0x1000`, the same as this port's images.
- On stock firmware, Fn + `-` held about 3 s enters it, but only with the
  system switch on Win and the mode switch on Cable.
- The reset button on the back, held with a pin while plugging in USB, enters it
  regardless of the firmware. It is read by the bootloader, which runs before
  the application, so a bad image cannot lock it out.
- Going back to stock is dragging the unit's own `CURRENT.UF2`, saved before the
  first flash, back onto `NRF52BOOT`. Not Keychron's
  [download](https://www.keychron.com/blogs/archived/how-to-use-the-launcher-web-app-or-manually-flash-firmware-for-your-b-pro-series-keyboard)
  for the same PID: it has the opposite matrix polarity (above).

Keychron ships at least three ANSI revisions (`0x0711`, `0x071a`, `0x0714`;
launcher.keychron.com, Settings, Device Info). This port is the `0x071a` board
with active-low pins. `0x0711` uses Keychron's `b1` map and active-high pins.

## Why this is feasible

The B1 Pro is an nRF52840, and Keychron publishes the ZMK tree it ships from:
[Keychron/zmk](https://github.com/Keychron/zmk), branch `keychron_bpro`
(`app/boards/arm/keychron/keychron.dts`, `app/boards/shields/keychron/b1n/us`).
The matrix pins, flash layout and battery divider in this port are copied from
there rather than probed; the key positions and pin polarity were then checked
against the keyboard's own stock image (Step 1). The `rtl8762g` branch is the newer *Ultra* series,
which is a Realtek chip and not this keyboard.

## What was added

| Path | What |
|---|---|
| `boards/keychron/keychron_b1_pro/` | Board: nRF52840 QIAA, Keychron's flash layout (Adafruit bootloader at `0xf4000`, app at `0x1000`), the 8x18 col2row matrix, the slide switches, battery divider on AIN7, red/green/blue LEDs, USB, UF2 output. At the repo root (the module's `board_root`) so both `config/` and `keychron-config` see it |
| `roBakesb_keychron.overlay/.conf` | The shield for the keyboard: the board's matrix without the switches, `zmk,esb-self = &esb_keychron`, sleep, battery, low-battery LED |
| `roBakesb_keychron_transform.dtsi` | 77 Keychron positions appended to the shared matrix as positions 48-124, with Studio geometry; enables the pipe; defines `ROBA_KEYCHRON` |
| `roBakesb_keychron_link.overlay` | Add-on shield every *other* node on the link carries (see below) |
| `roBakesb_addr.dtsi` | `esb_keychron`: pipe 3, prefix `0xE3`, disabled unless a shield above turns it on |
| `roBakesb_lariska_transform.dtsi` | Same 48 positions as before, but as `ROBA_LARISKA_MAP` / `ROBA_LARISKA_KEYS` macros so the Keychron file can extend them |
| `config/roBakesb_keychron.h` | The Keychron's bindings per layer, as `KEYCHRON_*` macros |
| `config/roBakesb.keymap` | Each layer ends in one `KEYCHRON_*` macro, the way it already ends in a `LARISKA_BTNS*` one |

### One keymap, optional Keychron

`roBakesb.keymap` is still the only keymap. Without `roBakesb_keychron_link` in
the shield list `ROBA_KEYCHRON` is undefined, every `KEYCHRON_*` macro is empty
and the dongle's devicetree is what it was; the only difference is the disabled
`esb_keychron` node (checked by diffing the generated `zephyr.dts` of the plain
Prospector build before and after: that node is the whole diff, and flash and
RAM are identical).

With it, the shared matrix grows from 48 to 125 positions and every layer gets
77 more bindings.

| Macro | On which layer | Does |
|---|---|---|
| `KEYCHRON_MAC` | Mac | Full 75% QWERTY. Win and Alt swap to Option and Command, as Keychron's own firmware does in Mac mode |
| `KEYCHRON_WIN` | Windows | Same, unswapped |
| `KEYCHRON_GAME` | Gaming | Windows layout (`&to GAMIN` leaves no layer below for `&trans`) |
| `KEYCHRON_SYSTEM` | System (the Fn layer) | F1/F2 brightness, F7-F12 media, Fn+1/2/3 = `&to` Mac/Windows/Gaming |
| `KEYCHRON_TRANS` | every other layer | `&trans`, so a roBa or lariska layer held while typing on the Keychron falls through to its base |

Fn is `&mo SUPER`. Caps is a plain `&kp CAPS`; the roBa's thumb key sends
`LC(CAPS)`, so change position 'CAPS' in `roBakesb_keychron.h` if the Mac needs
that too. Only the US layout is mapped.

## Building and flashing

`config/west.yml` pins split-esb v0.8.11; point `ESB_MODULE` at a checkout of
that tag so a local build matches CI:

```bash
export ESB_MODULE=~/personal/split-esb-v0811
./local_build_roba_kesb.sh roBakesb_keychron_b1_pro
./local_build_roba_kesb.sh roBakesb_dongle_prospector_keychron
./local_build_roba_kesb.sh roBakesb_left_keychron
./local_build_roba_kesb.sh roBakesb_right_keychron
./local_build_roba_kesb.sh roBakesb_lariska_keychron
```

**Flash all five together, or none.** The beacon the central sends is
`4 + 2 x (enabled peripherals)` bytes and it carries the hop epoch. A node built
with a different set of enabled pipes sees a beacon of the wrong length, drops
it, and stops following the channel hops. That is the same failure that showed up
as `Dropping command with unexpected size 12` on the relay topology. So the
dongle, left, right and lariska all move to their `_keychron` images when the
keyboard joins, and back to the plain images (the three-peripheral link) when it
leaves. The plain targets are unchanged and still build.

The Keychron's ESB image has no bootloader key; use the reset button (see
Flashing above).

## Limits

**On the board** (settled by step 1)
- *Pin map.* Keychron's pins, with the polarity of the stock image.
  `GPIO_AS_PINRESET=n` and the RC 32 kHz clock are required: P0.18 is column 16
  and P0.00/P0.01 are columns 0/1.
- *Ghosting.* No per-key diodes, so three keys on the corners of a matrix
  rectangle register the fourth. Q-T, A-F, Z-B, Left Shift, Space and Tab are
  all on row 0, so combos among them cannot form one.
- *DC/DC* is enabled because Keychron's board enables it; it needs the inductor
  to be fitted.
- *Debounce* (8 ms press and release) is Keychron's figure.

**In the design**
- *Pipe 3 is also the relay's.* Enabled pipes must be contiguous from 0
  (`hop_stage_beacon` rejects a pipe at or above the enabled count), so the
  Keychron cannot take pipe 4. It therefore cannot coexist with the relay
  topology until one of them moves; that topology is out of scope here.
- *Keepalive covers positions 0-63 only.* The uplink keepalive's pressed-key
  bitmap is 8 bytes (`ESB_KEEPALIVE_BITMAP_BYTES` in split-esb). Keychron
  positions 48-63 (Esc, F1-F12, Del, ` and 1) are covered; the other 61 are not.
  Key events themselves are ACKed and retransmitted like any others, so normal
  typing is unaffected. What is lost is the self-heal for a key whose *release*
  never arrived: on those positions it stays held until pressed and released
  again. Fixing it means making the bitmap size a Kconfig in the module (the
  keepalive grows by the extra bytes; with no sensors the Keychron's is 29
  bytes, like the right half, so there is room in a 56-byte payload).
- *RAM on the Prospector.* ZMK keeps a RAM copy of the keymap for Studio:
  15 layers x 125 positions x 16 B = 30 KB, up from 11.5 KB. With the LCD's two
  67 KB draw buffers the first build overflowed the 256 KB by 918 bytes. The
  `_keychron` dongle target therefore sets `CONFIG_ZMK_BEHAVIORS_QUEUE_SIZE=256`
  (512 entries, 14 KB, is far more than the 8-binding macros need), ending at
  97.6% with about 6 KB spare. Each further layer costs 2 KB.
- *The Prospector screen shows three devices.* It draws rings for pipes 0-2 and
  ignores anything at or above its `SLOT_COUNT` of 3, so the Keychron's battery
  and signal are not shown. A fourth ring does not fit in the current row
  (`RING_ROW_W`), so it is a layout change, not a constant.
- *Airtime.* A fourth transmitter on the same hopped channel set. A keyboard is
  light traffic next to the trackball, but the lariska already shows heavy
  retransmit pressure in the relay captures, so watch the per-pipe link cost on
  the dongle.
- *Not wired:* Mac/Win, BT and 2.4G slide switches (the mode follows the base
  layer instead; the standalone build does read them), charge status,
  Caps/Num/2.4G indicator LEDs, backlight, USB data. The USB-C port charges
  only. The red LED blinks on low battery (`ROBA_SELF_BATTERY_LED`, 25%).
- *Other layouts.* ISO and JIS B1 Pro have different matrices and extra keys:
  `keychron_b1_uk` and `keychron_b1_jis` in Keychron's tree.
