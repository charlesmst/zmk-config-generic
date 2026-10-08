/*
 * Central-side display support on the damex ESB transport.
 *
 * Counterpart to esb_peripheral_display_compat.c, which fills in the split-BLE
 * symbol the peripheral status screens link against.
 *
 * prospector-zmk-module's battery widgets are written for a BLE split central.
 * Each slot is built showing a red cross with the battery label at opa 0, and
 * flips to showing a number only when a zmk_split_central_status_changed with
 * connected = true arrives. The widget stores that state inside its LVGL
 * objects, cannot query the transport for the truth, and drops anything raised
 * before the status screen exists (`if (!is_initialized) return`). Only the
 * module's own BLE connection observer raises that event, so on ESB nothing
 * ever does and all three slots stay crossed out -- while battery events
 * quietly update labels nobody can see.
 *
 * Rather than translate the ESB transport's edge events and hope the timing
 * lines up, do what src/dongle_battery_display.c does for the OLED dongle:
 * keep the state here, poll the transport, and re-assert into the widget
 * whenever our view changes. zmk_split_esb.h exists for exactly this --
 * zmk_split_esb_peer_battery() and zmk_split_esb_peer_rssi_dbm() are read-only
 * snapshots with no event needed. Polling also self-heals: a missed edge, a
 * late screen, or a widget rebuilt by a status-screen change all converge
 * within one poll period instead of waiting for a peripheral to drop and
 * rejoin.
 *
 * Connect/disconnect still comes from zmk_split_esb_peripheral_changed where
 * we have it, since that is authoritative and the only thing that reports a
 * peripheral *going away* (cached battery never reverts to unknown). The poll
 * covers the window before our first edge: if the transport has battery or an
 * RSSI sample for a pipe, that pipe is demonstrably being heard.
 *
 * Slots are ESB pipe numbers, so the bars sit in device-tree order
 * (roBakesb_addr.dtsi: 0 left, 1 right, 2 lariska). There is no pairing on ESB,
 * so Prospector's "pair left before right" caveat does not apply.
 */

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <zmk/display.h>
#include <zmk/event_manager.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/split_central_status_changed.h>
#include <zmk/events/split_esb_peripheral_changed.h>
#include <zmk_split_esb.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/* Slot count the widgets index by. Sources at or above it they ignore. */
#define SLOT_COUNT CONFIG_ZMK_SPLIT_BLE_CENTRAL_PERIPHERALS

#define BATTERY_UNKNOWN 0xFF

/* Authoritative connect/disconnect, from the transport's own event. */
static bool edge_seen[SLOT_COUNT];
static bool edge_connected[SLOT_COUNT];

/* What we have pushed into the widget, so we only raise on a real change. */
static bool pushed_any[SLOT_COUNT];
static bool pushed_connected[SLOT_COUNT];
static uint8_t pushed_battery[SLOT_COUNT] = {[0 ... SLOT_COUNT - 1] = BATTERY_UNKNOWN};

static void roba_esb_display_poll_fn(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(roba_esb_display_poll, roba_esb_display_poll_fn);

static void roba_esb_display_poll_fn(struct k_work *work) {
    ARG_UNUSED(work);

    /* Before the screen exists the widgets drop everything, so there is no
     * point raising; the next tick will do it once they are up. */
    if (!zmk_display_is_initialized()) {
        goto reschedule;
    }

    for (uint8_t slot = 0; slot < SLOT_COUNT; slot++) {
        uint8_t battery = zmk_split_esb_peer_battery(slot);

        /* Battery or an RSSI sample both mean this pipe is being heard. Used
         * only until the transport gives us a real edge for this slot. */
        bool heard = (battery != BATTERY_UNKNOWN) || (zmk_split_esb_peer_rssi_dbm(slot) != 0);
        bool connected = edge_seen[slot] ? edge_connected[slot] : heard;

        if (!pushed_any[slot] || pushed_connected[slot] != connected) {
            raise_zmk_split_central_status_changed((struct zmk_split_central_status_changed){
                .slot = slot,
                .connected = connected,
            });
            LOG_DBG("esb display: slot %u connected=%d", slot, (int)connected);
        }

        /* Re-assert battery too. The central raises it on change, but a change
         * that landed before the screen existed is gone, which would leave a
         * connected slot reading N/A until the level happened to move. */
        if (connected && battery != BATTERY_UNKNOWN &&
            (!pushed_any[slot] || pushed_battery[slot] != battery)) {
            raise_zmk_peripheral_battery_state_changed(
                (struct zmk_peripheral_battery_state_changed){
                    .source = slot,
                    .state_of_charge = battery,
                });
            LOG_DBG("esb display: slot %u battery=%u%%", slot, battery);
        }

        pushed_connected[slot] = connected;
        pushed_battery[slot] = battery;
        pushed_any[slot] = true;
    }

reschedule:
    k_work_reschedule(&roba_esb_display_poll,
                      K_MSEC(CONFIG_ROBA_ESB_CENTRAL_DISPLAY_POLL_MS));
}

static int roba_esb_peripheral_changed_listener(const zmk_event_t *eh) {
    const struct zmk_split_esb_peripheral_changed *ev = as_zmk_split_esb_peripheral_changed(eh);
    if (ev == NULL || ev->source >= SLOT_COUNT) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    edge_seen[ev->source] = true;
    edge_connected[ev->source] = ev->connected;

    /* Push straight through so a disconnect shows immediately rather than at
     * the next tick; the poll is the backstop, not the primary path. */
    if (zmk_display_is_initialized() &&
        (!pushed_any[ev->source] || pushed_connected[ev->source] != ev->connected)) {
        raise_zmk_split_central_status_changed((struct zmk_split_central_status_changed){
            .slot = ev->source,
            .connected = ev->connected,
        });
        pushed_connected[ev->source] = ev->connected;
        pushed_any[ev->source] = true;
    }

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(roba_esb_central_display_compat, roba_esb_peripheral_changed_listener);
ZMK_SUBSCRIPTION(roba_esb_central_display_compat, zmk_split_esb_peripheral_changed);

static int roba_esb_central_display_compat_init(void) {
    k_work_schedule(&roba_esb_display_poll,
                    K_MSEC(CONFIG_ROBA_ESB_CENTRAL_DISPLAY_POLL_MS));
    return 0;
}

SYS_INIT(roba_esb_central_display_compat_init, APPLICATION, 99);
