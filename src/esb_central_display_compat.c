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

#include <zephyr/init.h>
#include <zephyr/kernel.h>

#include <zmk/event_manager.h>
#include <zmk/events/split_central_status_changed.h>
#include <zmk/events/split_esb_peripheral_changed.h>

/* Slot count the widgets use. Anything at or above it they ignore outright. */
#define SLOT_COUNT CONFIG_ZMK_SPLIT_BLE_CENTRAL_PERIPHERALS

static bool slot_seen[SLOT_COUNT];
static bool slot_connected[SLOT_COUNT];

/*
 * Replay, and why it is needed.
 *
 * The widgets build in the "not connected" state (red cross, battery label at
 * opa 0) and their generated init seeds slot 0 from get_state(NULL), which
 * reports disconnected -- there is no accessor to query real connection state,
 * so a fresh widget cannot discover it. Everything depends on a
 * zmk_split_central_status_changed arriving *after* the status screen exists;
 * anything earlier is dropped by `if (!is_initialized) return`.
 *
 * On BLE that is free: scanning plus GATT discovery takes seconds. On ESB the
 * link can be up almost immediately, and the central's sweep is edge-triggered
 * (central.c raises only when connected != pipe_connected[pipe]). Lose that one
 * edge to a not-yet-built screen and the slot shows a red cross until the
 * peripheral actually drops and rejoins -- while battery events keep updating
 * labels nobody can see.
 *
 * So cache what we forward and re-raise it once, late enough that the screen is
 * up. This listener is a plain ZMK_LISTENER, not a display widget listener, so
 * it sees the early edges even when the widgets cannot.
 */
static void roba_esb_replay_work_cb(struct k_work *work) {
    ARG_UNUSED(work);

    for (uint8_t slot = 0; slot < SLOT_COUNT; slot++) {
        if (!slot_seen[slot]) {
            continue;
        }
        raise_zmk_split_central_status_changed((struct zmk_split_central_status_changed){
            .slot = slot,
            .connected = slot_connected[slot],
        });
    }
}

static K_WORK_DELAYABLE_DEFINE(roba_esb_replay_work, roba_esb_replay_work_cb);

static int roba_esb_peripheral_changed_listener(const zmk_event_t *eh) {
    const struct zmk_split_esb_peripheral_changed *ev = as_zmk_split_esb_peripheral_changed(eh);
    if (ev == NULL) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    if (ev->source < SLOT_COUNT) {
        slot_seen[ev->source] = true;
        slot_connected[ev->source] = ev->connected;
    }

    raise_zmk_split_central_status_changed((struct zmk_split_central_status_changed){
        .slot = ev->source,
        .connected = ev->connected,
    });

    return ZMK_EV_EVENT_BUBBLE;
}

static int roba_esb_central_display_compat_init(void) {
    k_work_schedule(&roba_esb_replay_work,
                    K_MSEC(CONFIG_ROBA_ESB_CENTRAL_DISPLAY_REPLAY_MS));
    return 0;
}

SYS_INIT(roba_esb_central_display_compat_init, APPLICATION, 99);

ZMK_LISTENER(roba_esb_central_display_compat, roba_esb_peripheral_changed_listener);
ZMK_SUBSCRIPTION(roba_esb_central_display_compat, zmk_split_esb_peripheral_changed);
