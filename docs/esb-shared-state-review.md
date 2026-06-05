# ZMK ESB split — shared-state broadcast: design & failure-mode review

Reviewer-oriented writeup for critiquing intermittent "bad connection / dropped
packets" on the roBaesb ESB split. Separates the **pre-existing ESB substrate**
from the **shared-state feature** added on top, and ends with the concrete
suspects.

## 1. System

A split keyboard using a **custom ESB transport** (Nordic Enhanced ShockBurst)
instead of ZMK's BLE split, module `zmk-feature-split-esb`.

- **Central** — left half, nRF52840 (Xiao BLE). Simultaneously the **ESB PRX
  (receiver)** and the **BLE/USB HID host** to the PC. ESB and BLE share the
  radio via **MPSL timeslots**.
- **Peripheral 1** — right half + PMW3610 trackball. ESB **PTX**, `source id = 2`.
- **Peripheral 2** — "lariska" PAW3395 mouse + 5 buttons (nice_nano). ESB **PTX**,
  `source id = 3`.

Radio: `ESB_PROTOCOL_ESB_DPL`, **2 Mbps (BLE bitrate)**, RF channel 80
(2480 MHz), +4 dBm, `selective_auto_ack=true`, `tx_mode=MANUAL_START`, max
payload 48 B. Retransmit count 3, base delay 600 µs with ±~40 % software jitter.

## 2. ESB transport mechanics (pre-existing substrate — the constraints)

- **PTX initiates every transaction; PRX can only reply with an ACK payload.**
  The central cannot transmit spontaneously to a peripheral — it can only attach
  a payload to the hardware ACK of a packet a peripheral just sent.
- **Single shared pipe (pipe 0).** Peripherals are *not* separated by hardware
  pipe; they're distinguished by a `source` byte inside the payload.
  Consequence: a central→peripheral ACK payload is **not addressable** —
  whichever peripheral transmits next consumes the next queued ACK payload. The
  peripheral drops commands whose `source` ≠ its id, **except** `POLL_EVENTS` and
  (new) `TRANSPORT_CHANGED`, which are processed unconditionally.
- **Radio is time-shared with BLE via MPSL timeslots.** At each timeslot boundary
  the app **fully suspends/disables the ESB radio** (`app_esb_suspend`) and
  **re-runs `esb_initialize()` on resume**. ESB therefore only runs in granted
  slots between BLE activity. Central `MPSL_TIMESLOT_SESSION_COUNT=2`, peripherals
  `=1`. Peripherals also **disable ESB entirely when `activity_state != ACTIVE`**
  (idle).
- **Framing:** `prefix{ 4-byte magic "ZmKe", 1-byte payload_size } + payload +
  postfix{ crc32 }`. Parsed from a ring buffer by `zmk_split_esb_get_item()`,
  which **resyncs byte-by-byte** on a magic mismatch.
- **Key/button state** is sent as a **full idempotent bitmap** each change; the
  central XORs it against per-source tracked state to emit diffs. **Mouse motion**
  is single-shot, ACK'd; a lost delta is folded into the next motion packet.

### Buffers / queues

| | Central (PRX) | Peripheral (PTX) |
|---|---|---|
| RX ring (`*_rx_buf`) | peripheral events | central commands |
| TX ring (`*_tx_buf`) | commands/broadcasts | events |
| ESB payload msgq (`m_msgq_tx_payloads`) | 64 | 64 |
| ESB HW TX FIFO | 16 | **8 (lariska)** / 16 (right) |
| `EVENT_BUFFER_ITEMS` / `CMD_BUFFER_ITEMS` | 64 / 16 | 64 / 16 |

TX drain (`pull_packet_from_tx_msgq`): on `esb_write_payload` → `-ENOMEM` it
increments a counter and only **force-flushes the FIFO after
`MSGQ_ITEMS × RETRANSMIT_COUNT = 192` consecutive ENOMEM events**.

## 3. The shared-state feature (what was added)

**Goal:** when the central switches output USB↔BLE, tell the peripherals so their
sensor drivers pick a hardware polling rate (USB = high, BLE = low). Implemented
as **idempotent state replication, not addressed messaging**, to avoid the
unreliable/unaddressable central→peripheral path.

**Central (`central.c`):**
- New command `TRANSPORT_CHANGED` carrying one byte (`transport`: 0=USB, 1=BLE).
- On `zmk_endpoint_changed`: set `current_transport`, open a **600 ms convergence
  window**.
- While the window is open, a **`k_timer` fires every 30 ms** and submits a work
  item that enqueues **one** `TRANSPORT_CHANGED` into `tx_buf` — **only if
  `tx_buf` is empty** (real commands take priority; ≤1 broadcast in flight). It
  rides out as an ACK payload to the next peripheral that transmits.
- Boot seeds `current_transport` from `zmk_endpoints_selected()`.
- **Steady state (no USB/BLE change) emits nothing.** (Earlier there was a
  per-idle-gap re-trigger that flooded the link; removed.)

**Peripheral (`peripheral.c`):**
- `TRANSPORT_CHANGED` handled **before** the source-id filter (broadcast), with
  **value dedup** (apply only when `transport` changed) → raises a ZMK event the
  sensor driver consumes. No retransmit, no extra TX.

**Net new load on the link:** during a ≤600 ms window after each USB↔BLE switch,
up to ~20 extra central→peripheral ACK-payload commands; zero otherwise.

## 4. Reported symptom

Intermittent "bad connection / dropped packets after a while," sometimes
requiring a **power cycle** to recover. One *confirmed* bug already fixed:
`get_item` returned `-EINVAL` **without consuming bytes** on an impossible
`payload_size`, permanently wedging the RX ring until reboot — now changed to
discard-and-resync. Symptom reportedly persists intermittently after that fix.

## 5. Suspects to critique (ranked by suspicion)

1. **ESB/BLE timeslot starvation.** ESB only runs inside MPSL timeslots and the
   **radio is fully re-initialized every timeslot**. The central is *also* a BLE
   peripheral to the PC. Under BLE load (connection events, retransmits, host
   interval changes) ESB slots may be squeezed/late → bursts of missed ACKs. Is
   per-timeslot `esb_disable()`/`esb_initialize()` sound, or is there a re-init
   race / settling cost causing periodic drops?
2. **Peripheral TX-FIFO saturation.** When ACKs aren't received (out-of-slot /
   range), retransmits accumulate in `m_msgq_tx_payloads` (64) and the HW FIFO
   (**8 on lariska**). When the TX ring fills, new key/button packets hit
   `-ENOSPC` and are **silently dropped**. The ENOMEM force-flush only triggers
   after **192** failures — possibly far too slow to recover, matching "needs
   power cycle."
3. **ACK-payload back-pressure on the central.** Central→peripheral data
   (commands + broadcasts) sits in a 64-deep msgq drained **only when some
   peripheral transmits**. If a peripheral idles/disables ESB, payloads linger;
   could a stale/oversized ACK payload desync a peripheral's RX framing?
4. **Single pipe-0 + source-byte addressing.** ACK payloads consumed by the
   "wrong" peripheral; relevant to commands generally and to the broadcast
   (mitigated by making it a true broadcast, but still consumes ACK slots).
5. **Framing resync.** `get_item` resyncs byte-by-byte; a single desync drops a
   span of bytes (now bounded, no longer a permanent wedge). How often can DPL
   length / CRC desync occur at 2 Mbps with this timeslot scheme?
6. **The broadcast as aggravator.** Now bounded to 600 ms windows on endpoint
   change — but during those windows it adds ACK-payload traffic competing with
   peripheral input. Worth confirming it's truly quiescent in steady state.
7. **Jittered 600 µs retransmit delay** and `selective_auto_ack` interaction with
   the manual TX-start timeslot model.

**A/B available:** the same firmware built on the ESB module *without* the
shared-state feature (no broadcast) is being tested. If loss persists identically
there, suspects 1/2 (substrate) are implicated over the feature.

## 6. Source map

| Concern | File |
|---|---|
| Radio init, timeslot suspend/resume, TX FIFO drain, retransmit/flush | `zmk-feature-split-esb/src/split/esb/app_esb.c` |
| Framing, `get_item` resync, RX→ring, async TX | `.../src/split/esb/common.c` |
| Central: PRX RX loop, broadcast window/timer, endpoint listener | `.../src/split/esb/central.c` |
| Peripheral: PTX send, key/button bitmap, `TRANSPORT_CHANGED` handling | `.../src/split/esb/peripheral.c` |
| Command/event wire types | `zmk/app/include/zmk/split/transport/types.h` |
