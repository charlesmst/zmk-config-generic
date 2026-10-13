# Keychron B1 Pro as a fourth ESB peripheral

The Keychron B1 Pro (ultra-slim 75%, US/ANSI) joins the roBakesb link as a
fourth key peripheral next to left, right and lariska. The Prospector dongle
stays the central: it owns the keymap, the layers and the USB connection, and
the Keychron only sends key positions, like the roBa halves do. Keychron's own
2.4 GHz dongle stops working once the keyboard runs this firmware.

**Nothing here has been run on a B1 Pro.** Everything below was built and
checked in the devicetree, not on the board. "Unverified" marks the parts that
need the keyboard in hand.

## Why this is feasible

The B1 Pro is an nRF52840, and Keychron publishes the ZMK tree it ships from:
[Keychron/zmk](https://github.com/Keychron/zmk), branch `keychron_bpro`
(`app/boards/arm/keychron/keychron.dts`, `app/boards/shields/keychron/b1/us`).
The matrix pins, flash layout and battery divider in this port are copied from
there rather than probed. The `rtl8762g` branch is the newer *Ultra* series,
which is a Realtek chip and not this keyboard.

## What was added

| Path | What |
|---|---|
| `config/boards/arm/keychron_b1_pro/` | Board: nRF52840 QIAA, Keychron's flash layout (Adafruit bootloader at `0xf4000`, app at `0x1000`), battery divider on AIN7, red/green/blue LEDs, UF2 output |
| `roBakesb_keychron.overlay/.conf` | The shield for the keyboard: 8x18 col2row matrix, `zmk,esb-self = &esb_keychron`, sleep, battery, low-battery LED |
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

```bash
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

Keychron's own board README says to hold RESET while plugging in USB to get the
UF2 drive. **Unverified** on the retail unit.

## Limits

**Needs the keyboard**
- *Flashability.* The layout assumes the stock bootloader is the Adafruit UF2
  bootloader at `0xf4000`, as in Keychron's tree. The retail unit's bootloader,
  and how to enter it, are unconfirmed. If it takes only Keychron's signed images
  the way in is SWD on the PCB. The image starts at `0x1000` and touches nothing
  from `0xf4000` up, so it does not overwrite the bootloader.
- *Getting back to stock* is Keychron's launcher, if the bootloader still
  accepts it. Unverified; assume it needs an SWD probe until tried.
- *Pin map.* Copied from Keychron's source for the version it was written for.
  `GPIO_AS_PINRESET=n` and the RC 32 kHz clock are required: P0.18 is column 16
  and P0.00/P0.01 are columns 0/1. A wrong pin shows as a dead row or column.
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
  layer instead), charge status, Caps/Num/2.4G indicator LEDs, backlight, USB
  data. The USB-C port charges only. The red LED blinks on low battery
  (`ROBA_SELF_BATTERY_LED`, 25%).
- *Other layouts.* ISO and JIS B1 Pro have different matrices and extra keys:
  `keychron_b1_uk` and `keychron_b1_jis` in Keychron's tree.
