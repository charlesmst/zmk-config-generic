/*
 * Central-side display support on the damex ESB transport.
 *
 * Counterpart to esb_peripheral_display_compat.c, which fills in the split-BLE
 * symbol the peripheral status screens link against. This one bridges the
 * other direction.
 *
 * prospector-zmk-module's battery widgets track per-peripheral connection
 * state via zmk_split_central_status_changed. The module declares and
 * implements that event itself (include/zmk/events/split_central_status_changed.h
 * and src/events/split_central_status_changed.c -- ZMK upstream has no such
 * event at this fork point), but the only thing that *raises* it is the
 * module's own BLE connection observer. With CONFIG_ZMK_SPLIT_BLE=n nothing
 * raises it, so the connection dots would sit permanently disconnected.
 *
 * zmk-feature-split-esb raises its own zmk_split_esb_peripheral_changed
 * (central.c, on the pipe-staleness sweep) carrying the same information under
 * different names. Translate one into the other: source -> slot.
 *
 * ESB pipe numbers are the widget's slot indices, so the battery bars sit in
 * pipe order (roBakesb_addr.dtsi: 0 left, 1 right, 2 lariska). That sidesteps
 * Prospector's BLE pairing-order caveat -- there is no pairing on ESB, and the
 * order is fixed at build time by the device tree.
 */

#include <zephyr/kernel.h>

#include <zmk/event_manager.h>
#include <zmk/events/split_central_status_changed.h>
#include <zmk/events/split_esb_peripheral_changed.h>

static int roba_esb_peripheral_changed_listener(const zmk_event_t *eh) {
    const struct zmk_split_esb_peripheral_changed *ev = as_zmk_split_esb_peripheral_changed(eh);
    if (ev == NULL) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    raise_zmk_split_central_status_changed((struct zmk_split_central_status_changed){
        .slot = ev->source,
        .connected = ev->connected,
    });

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(roba_esb_central_display_compat, roba_esb_peripheral_changed_listener);
ZMK_SUBSCRIPTION(roba_esb_central_display_compat, zmk_split_esb_peripheral_changed);
