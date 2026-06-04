# ESB Shared-State Broadcast (output-driven sensor rates)

When the central (left half / BLE+USB host) switches its output between USB and
BLE, it tells every ESB peripheral so they switch their sensor polling rate:
faster on USB (cable bandwidth, low latency), slower on BLE (radio budget,
battery). Trackball and mouse both react.

This is the **second** implementation. The first one worked but slowed the mouse
down; this one is built so that talking to peripherals costs the ESB link
essentially nothing. The mechanism is generic — it is meant to be the baseline
for any future central→peripheral signalling, not just rate switching.

| Peripheral | Sensor | USB rate | BLE rate |
|------------|--------|----------|----------|
| roBaesb_right | PMW3610 | `usb-rate-ms = 2` → ~500 Hz | `ble-rate-ms = 8` → 125 Hz |
| lariska_esb   | PAW3395 | `usb-rate-ms = 1` → 1000 Hz | `ble-rate-ms = 8` → 120 Hz |

The lariska DPI button (P1.15) still cycles rate locally and overrides the
transport-set rate until the next USB/BLE switch (last writer wins).

---

## Why the first version was slow (what NOT to do)

ESB facts that constrain everything (see `app_esb.c`):

- The central is the **PRX** (receiver); peripherals are **PTX** (transmitters).
  A PRX can only send data back as an **ACK payload** riding on a packet a
  peripheral already sent. It cannot transmit spontaneously.
- ACK payloads go out on a **single shared pipe (pipe 0)**. Whichever peripheral
  transmits next consumes the next queued payload — the central **cannot address
  a specific peripheral** at the radio layer.
- A peripheral **drops** any command whose `source` field isn't its own
  (`peripheral.c`), so a payload meant for the trackball that the mouse happens
  to consume is simply lost.

The first version fought this with an addressed, retried push: for each source it
re-queued the command up to 8 times, and it did that work **inside the RX
processing loop** (`publish_events_work`) — full envelope build + CRC32 +
semaphore + `begin_tx` per source per retry, on the exact hot path that is
supposed to be draining mouse deltas. `begin_tx` also kicks the async TX state
machine, contending with the RX timeslot. Net effect: the more the mouse moved,
the more this ran, and the mouse got choppy. Wrong model.

---

## The model that works: idempotent state replication

Stop *sending a message*; *replicate a tiny value*. The only thing peripherals
need is "current transport = USB or BLE." That's one idempotent byte.

There is one happy accident in the protocol that makes this clean: the
`POLL_EVENTS` command is the one command type the peripheral processes **without
the source-ID check**. We add `TRANSPORT_CHANGED` to that same no-source-check
path, so it is a true **broadcast** — every peripheral acts on any copy it sees.

Delivery is **eventually consistent**, not reliable messaging:

1. On `zmk_endpoint_changed` the central updates `current_transport` and opens a
   short **convergence window** (`ESB_BROADCAST_WINDOW_MS = 600`).
2. While the window is open a **timer** (`ESB_BROADCAST_TICK_MS = 30`) queues a
   single `TRANSPORT_CHANGED` broadcast at a modest cadence — at most one in
   flight (`ring_buf_is_empty(&tx_buf)` gate lets real commands go first).
3. Each peripheral applies the value **only when it changes** (value-dedup on the
   peripheral side), so duplicate copies are free and a missed copy is just
   picked up from the next one.
4. After the window the central goes quiet. **Steady state adds nothing** to the
   link — bare auto-acks, exactly as before.

Crucially, **none of this runs on the RX/event hot path.** The timer is
independent of traffic, so a fast-moving mouse no longer pays for it. The only
RX-path cost added is one timestamp compare/store per packet
(`esb_note_source_seen`), used for reconnect detection.

### Convergence and the idle/reconnect edge

Within the window every peripheral that transmits at all receives the value;
trackball/mouse fire hundreds of times a second when active, so it's effectively
instant. The one gap is a peripheral that is **silent for the whole window**
(e.g. you switch USB→BLE without touching the mouse). Two things close it:

- **First-sight / reconnect re-trigger:** the central sees every peripheral
  packet (it's the PRX). `esb_note_source_seen()` reopens the window when a source
  is heard for the first time, or again after a gap of
  `ESB_SOURCE_RECONNECT_GAP_MS = 1500` ms (a reconnect). So a peripheral that was
  asleep converges the moment it starts transmitting.
- **Boot seed:** at init the central reads `zmk_endpoints_selected().transport`,
  so a peripheral connecting before the first `endpoint_changed` still gets the
  right value. Sensors also boot at `usb-rate-ms` (high performance).

An idle sensor's rate doesn't matter anyway — it isn't sending motion — and by
the time it moves it has transmitted and converged.

---

## Data flow

```
USB plugged/unplugged or BLE profile changed
  → zmk_endpoint_changed                          (ZMK core event, central side)
  → esb_central_on_endpoint_changed (central.c)   set current_transport, open window
  → esb_broadcast_timer (every 30 ms, ≤600 ms)    → esb_broadcast_tick_work
  → split_central_esb_send_command(0, TRANSPORT_CHANGED{transport})
  → ESB ACK payload, pipe 0, no source check
  → peripheral.c process_tx_cb: TRANSPORT_CHANGED branch, value-dedup
  → cmd queued → zmk_split_transport_peripheral_command_handler() (zmk core)
  → case TRANSPORT_CHANGED → raise zmk_peripheral_transport_changed(transport)
  → PMW3610 / PAW3395 driver listener
  → PMW3610: active_perf = ms_to_perf(rate); pmw3610_set_performance() (SPI)
    PAW3395: report_interval_ms = rate                       (software throttle)
```

---

## Repos, branches, and the build topology

> The build (`./local_build.sh`) compiles the **local working trees** under the
> west workspace `~/personal/zmk-config-roBa-charybdis-esb/`, not the pushed
> branches. `west.yml` revisions are declarative; a `west update` would reset the
> working trees to them (and is destructive to any uncommitted local edits).

All three touched repos are on branch **`feat/esb-shared-state-broadcast`**.
The sensor drivers already carried the listener code on `feat/runtime-report-rate`
(unchanged here).

| Repo | Remote | Branch | What changed |
|------|--------|--------|--------------|
| `zmk` | charlesmst/zmk | `feat/esb-shared-state-broadcast` | `types.h`: `CMD_TYPE_TRANSPORT_CHANGED` + `set_transport`; new event `peripheral_transport_changed` (+ `CMakeLists.txt`); `split/peripheral.c`: handle the command → raise event (also adds the missing `break;` to the INVOKE_BEHAVIOR case) |
| `zmk-feature-split-esb` | charlesmst/zmk-feature-split-esb | `feat/esb-shared-state-broadcast` | `central.c`: endpoint_changed listener, convergence-window timer broadcast, reconnect re-trigger, boot seed; `peripheral.c`: broadcast branch with value-dedup |
| `zmk-config-generic` | (this repo) | `feat/esb-shared-state-broadcast` | `west.yml` revisions; `*_OUTPUT_RATE_NOTIFY=y` + `usb-rate-ms`/`ble-rate-ms`; removed the central-side `zip_ble_report_rate_limit` (rate limiting now lives in the driver) |
| `zmk-pmw3610-driver-efog` | charlesmst | `feat/runtime-report-rate` | (pre-existing) `CONFIG_PMW3610_OUTPUT_RATE_NOTIFY` listener |
| `zmk-paw3395-driver` | charlesmst | `feat/runtime-report-rate` | (pre-existing) `CONFIG_PAW3395_OUTPUT_RATE_NOTIFY` listener |

**Caveat — pre-existing local zmk tweaks.** The `zmk` branch starts with one
`chore:` snapshot commit carrying three edits that were already uncommitted in the
build workspace and are required to build the tested firmware (a
`BLE_PERIPHERAL_COUNT` alias for ESB centrals in `split/central.h`, a `battery.c`
change, and a `behavior_key_toggle.c` brace fix). They are unrelated to this
feature but committed so the branch reproduces a working build.

---

## Tunables

Central, in `zmk-feature-split-esb/src/split/esb/central.c`:

```c
#define ESB_BROADCAST_TICK_MS       30    /* broadcast cadence while window open  */
#define ESB_BROADCAST_WINDOW_MS     600   /* how long to keep broadcasting         */
#define ESB_SOURCE_RECONNECT_GAP_MS 1500  /* silence > this, then heard = reconnect */
```

Per shield:

- `roBaesb_right.conf`: `CONFIG_PMW3610_OUTPUT_RATE_NOTIFY=y`
- `lariska_esb.conf`: `CONFIG_PAW3395_OUTPUT_RATE_NOTIFY=y`
- Sensor node overlay: `usb-rate-ms = <…>; ble-rate-ms = <…>;`

Disable on a shield by dropping the Kconfig + DTS props; the sensor reverts to its
default (`force-high-performance` / `CONFIG_PAW3395_REPORT_INTERVAL_MIN`).

> `CONFIG_ZMK_INPUT_PROCESSOR_REPORT_RATE_LIMIT=y` and the
> `#include report_rate_limit.dtsi` remain in the central config but are now
> **unused** — kept as an easy fallback if you ever want central-side limiting
> back instead of driver-side switching.

---

## Sensor rate mechanisms (reference)

### PMW3610 — hardware PERFORMANCE register (0x11)

`pmw3610_ms_to_perf()` maps ms → register byte:

| Rate | Register |
|------|----------|
| 8 ms (125 Hz) | `0x00` |
| 4 ms (250 Hz) | `0x0D` |
| 2 ms (500 Hz) | `0x0E` |

Driver stores the target in `data->active_perf`; on transport change it updates
the field and calls `pmw3610_set_performance(dev, true)` (SPI write). Survives
sleep/wake (`set_performance(dev,false)` writes `0x00` on sleep, restores on wake):

```c
data->active_perf = pmw3610_ms_to_perf(rate_ms);
if (data->ready) pmw3610_set_performance(dev, true);
```

### PAW3395 — software report throttle

`report_interval_ms` gates how often `input_report()` is called; the sensor always
samples at hardware speed and accumulated `dx/dy` flush on the next allowed report.
No SPI access:

```c
data->report_interval_ms = rate_ms; /* 1=1000Hz, 4=250Hz, 8=120Hz, 0=unlimited */
```

The DPI button (GPIO rate-cycle) writes the same field, so both coexist — last
write wins.

---

## Extending it to carry more than transport

The broadcast is a generic substrate. To replicate another idempotent value:

1. Add a field to `set_transport` (rename to a broader `shared_state` struct) in
   `zmk/.../transport/types.h`.
2. Set it alongside `current_transport` in the central and open the window.
3. Apply it (with value-dedup) in the peripheral's `TRANSPORT_CHANGED` branch.

Cost stays O(1) per change and zero in steady state, regardless of how many
fields or peripherals you add.
