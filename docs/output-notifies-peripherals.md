# Output-Notifies-Peripherals

When the central (dongle) switches between USB and BLE output, it broadcasts a
`CMD_TRANSPORT_CHANGED` ESB command to all peripherals so they can adjust their
sensor hardware polling rates.

---

## Rates

| Peripheral | Sensor | USB rate | BLE rate |
|------------|--------|----------|----------|
| roBaesb_right | PMW3610 | 2 ms → ~500 Hz | 8 ms → 125 Hz |
| lariska_esb   | PAW3395 | 1 ms → 1000 Hz  | 8 ms → 120 Hz  |

The lariska DPI button (P1.15) continues to work and can override the transport-set rate.

---

## Data flow

```
USB plugged in / BLE profile changed
  → zmk_endpoint_changed (ZMK event, central-side)
  → ESB central.c listener
  → split_central_esb_send_command(source=0, CMD_TRANSPORT_CHANGED{transport})
  → ESB RF (broadcast, no source check on peripheral side)
  → ESB peripheral.c: queues command, skipping source-ID filter
  → zmk_split_transport_peripheral_command_handler()
  → case TRANSPORT_CHANGED: raise zmk_peripheral_transport_changed(transport)
  → PMW3610/PAW3395 driver listener fires
  → Updates active_perf (PMW3610) / report_interval_ms (PAW3395)
```

**Reconnect:** When the central first receives data from a peripheral source ID it
hasn't notified yet (i.e., it rebooted/reconnected), it re-broadcasts the current
transport. The peripheral boots at USB (high-performance) rate so if the central
is also on USB, no re-broadcast is needed anyway.

---

## Repos and branches

All five repos are on **`feat/output-notifies-peripherals`**.

| Repo | Remote | What changed |
|------|--------|-------------|
| `zmk` | charlesmst/zmk (fork) | `types.h`: new enum value + union member; `peripheral_transport_changed` event; `split/peripheral.c`: handles new command type |
| `zmk-feature-split-esb` | charlesmst/zmk-feature-split-esb | `central.c`: subscribes to `zmk_endpoint_changed`, tracks notified sources, sends broadcast on change and reconnect; `peripheral.c`: handles CMD_TRANSPORT_CHANGED before source-ID check |
| `zmk-pmw3610-driver-efog` | charlesmst/zmk-pmw3610-driver-efog | `Kconfig`: `CONFIG_PMW3610_OUTPUT_RATE_NOTIFY`; `pixart.h`: `usb_rate_ms`/`ble_rate_ms` in config struct; `pmw3610.c`: transport listener, updates `active_perf` + calls `pmw3610_set_performance`; `pixart,pmw3610.yml`: `usb-rate-ms`/`ble-rate-ms` DTS props |
| `zmk-paw3395-driver` | charlesmst/zmk-paw3395-driver | Same pattern; listener sets `report_interval_ms` directly (software throttle, no SPI) |
| `zmk-config-generic` | (this repo) | `west.yml`: updated revisions; overlays: `usb-rate-ms`/`ble-rate-ms` on sensor nodes; conf: `CONFIG_*_OUTPUT_RATE_NOTIFY=y` |

---

## Enabling / disabling per shield

Shield conf files:
- `roBaesb_right.conf`: `CONFIG_PMW3610_OUTPUT_RATE_NOTIFY=y`
- `lariska_esb.conf`: `CONFIG_PAW3395_OUTPUT_RATE_NOTIFY=y`

Overlay DTS properties on the sensor node:
```dts
usb-rate-ms = <2>;   /* 2ms ≈ 500 Hz (PMW3610) */
ble-rate-ms = <8>;   /* 8ms = 125 Hz */
```

Remove the Kconfig and DTS props to disable; the sensor reverts to its default
`force-high-performance` / `CONFIG_PAW3395_REPORT_INTERVAL_MIN` behavior.

---

## PMW3610 rate mechanism

The PMW3610 has a hardware `PERFORMANCE` register (0x11) that directly controls
the sensor's sampling rate. `pmw3610_ms_to_perf()` converts ms → register byte:

| Rate    | Register |
|---------|----------|
| 8ms (125 Hz) | `0x00` |
| 4ms (250 Hz) | `0x0D` |
| 2ms (500 Hz) | `0x0E` |

The driver stores the target value in `data->active_perf`. On transport change,
it updates `active_perf` and calls `pmw3610_set_performance(dev, true)` which
writes the register over SPI. This rate survives sleep/wake because
`pmw3610_set_performance(dev, false)` writes `0x00` on sleep and restores
`active_perf` on wake.

**To change rate from driver code:**
```c
data->active_perf = pmw3610_ms_to_perf(rate_ms);
if (data->ready) pmw3610_set_performance(dev, true);
```

---

## PAW3395 rate mechanism

The PAW3395 uses a **software rate limiter** in `paw3395_report_data()`:
it compares `k_uptime_get()` against `data->last_rpt_time` and drops reports
that arrive sooner than `report_interval_ms`. The sensor itself always samples
at hardware speed; `report_interval_ms` just gates how often `input_report()` is
called. Accumulated `dx/dy` are flushed on the next allowed report.

**To change rate from driver code:**
```c
data->report_interval_ms = rate_ms; /* 1=1000Hz, 4=250Hz, 8=120Hz, 0=unlimited */
```
No SPI access needed. The DPI button (GPIO rate-cycle) also writes this field,
so both mechanisms coexist — last write wins.

---

## Protocol detail

New command type in `zmk/app/include/zmk/split/transport/types.h`:
```c
ZMK_SPLIT_TRANSPORT_CENTRAL_CMD_TYPE_TRANSPORT_CHANGED = 4,
```
Payload: `uint8_t transport` (matches `enum zmk_transport`: `0=USB`, `1=BLE`).

This is a **broadcast** command: the peripheral ESB layer processes it before the
per-source-ID filter, so all peripherals handle it regardless of their `peripheral_id`.
The central sends it with `source=0` and only needs one TX per event.
