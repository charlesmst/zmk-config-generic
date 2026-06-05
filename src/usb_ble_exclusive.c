/*
 * Copyright (c) 2024 Charles Stein
 *
 * SPDX-License-Identifier: MIT
 *
 * usb_ble_exclusive: fully disable BLE when USB is the selected endpoint,
 * re-enable it when switching away from USB.
 *
 * Rationale: bt_disable() releases the MPSL timeslot session, freeing radio
 * bandwidth for ESB.  bt_enable(NULL) restores it; update_advertising() then
 * restarts BLE advertising without re-registering any callbacks (Zephyr's
 * bt_conn_cb_register guards against double-registration).
 */

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/logging/log.h>

#include <zmk/event_manager.h>
#include <zmk/events/endpoint_changed.h>
#include <zmk/endpoints_types.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/*
 * update_advertising() lives in zmk/app/src/ble.c.  It is not declared in any
 * public header but has external linkage (not static).  Declare it here so we
 * can call it after bt_enable() to restart BLE advertising without repeating
 * the full zmk_ble_complete_startup() sequence (which would double-register
 * connection callbacks).
 */
extern int update_advertising(void);

/* ------------------------------------------------------------------ */
/*  Work items — BT enable/disable must not be called from event      */
/*  callbacks which may run on the system workqueue.                   */
/* ------------------------------------------------------------------ */

static struct k_work enable_ble_work;
static struct k_work disable_ble_work;

static void do_enable_ble(struct k_work *work)
{
    ARG_UNUSED(work);

    LOG_INF("usb_ble_exclusive: re-enabling BLE");

    int err = bt_enable(NULL);
    if (err && err != -EALREADY) {
        LOG_ERR("usb_ble_exclusive: bt_enable failed (%d)", err);
        return;
    }

    /* Restart advertising.  Callbacks were registered at startup and
     * bt_conn_cb_register() is idempotent (returns -EEXIST if already
     * in the list), so ZMK's ble.c callbacks survive across disable/enable. */
    int adv_err = update_advertising();
    if (adv_err) {
        LOG_WRN("usb_ble_exclusive: update_advertising returned %d", adv_err);
    }
}

static void do_disable_ble(struct k_work *work)
{
    ARG_UNUSED(work);

    LOG_INF("usb_ble_exclusive: disabling BLE (USB selected)");

    int err = bt_disable();
    if (err) {
        LOG_ERR("usb_ble_exclusive: bt_disable failed (%d)", err);
    }
}

/* ------------------------------------------------------------------ */
/*  Event listener                                                      */
/* ------------------------------------------------------------------ */

static int usb_ble_exclusive_listener(const zmk_event_t *eh)
{
    const struct zmk_endpoint_changed *ev = as_zmk_endpoint_changed(eh);
    if (ev == NULL) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    if (ev->endpoint.transport == ZMK_TRANSPORT_USB) {
        k_work_submit(&disable_ble_work);
    } else {
        k_work_submit(&enable_ble_work);
    }

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(usb_ble_exclusive, usb_ble_exclusive_listener);
ZMK_SUBSCRIPTION(usb_ble_exclusive, zmk_endpoint_changed);

/* ------------------------------------------------------------------ */
/*  Init                                                                */
/* ------------------------------------------------------------------ */

static int usb_ble_exclusive_init(void)
{
    k_work_init(&enable_ble_work, do_enable_ble);
    k_work_init(&disable_ble_work, do_disable_ble);
    return 0;
}

SYS_INIT(usb_ble_exclusive_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
