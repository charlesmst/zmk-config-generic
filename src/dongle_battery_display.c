#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <string.h>
#include <lvgl.h>

#include <zmk/display.h>
#include <zmk/display/status_screen.h>
#include <zmk/battery.h>
#include <zmk/event_manager.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/endpoints.h>
#include <zmk/endpoints_types.h>
#include <zmk/events/endpoint_changed.h>
#include <zmk/keymap.h>
#include <zmk/events/layer_state_changed.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define NUM_PERIPHERALS CONFIG_ZMK_SPLIT_BLE_CENTRAL_PERIPHERALS
#define NUM_LABELS      (1 + NUM_PERIPHERALS)

BUILD_ASSERT(NUM_PERIPHERALS <= 3, "dongle_battery_display supports up to 3 peripherals");

static const char label_chars[4] = {'D', 'L', 'R', 'M'};

static lv_obj_t *bat_labels[NUM_LABELS];
static lv_obj_t *conn_label;
static lv_obj_t *layer_label;

struct display_state {
    uint8_t          bat_levels[NUM_LABELS];
    bool             bat_valid[NUM_LABELS];
    enum zmk_transport transport;
    int              ble_profile;
    uint8_t          active_layer;
};

K_MUTEX_DEFINE(state_mutex);
static struct display_state dstate;

static void update_display(struct k_work *work) {
    k_mutex_lock(&state_mutex, K_FOREVER);
    struct display_state s = dstate;
    k_mutex_unlock(&state_mutex);

    char buf[8];

    /* Connection label: "USB" or "BT 1" / "BT 2" */
    if (s.transport == ZMK_TRANSPORT_USB) {
        snprintf(buf, sizeof(buf), "USB");
    } else {
        snprintf(buf, sizeof(buf), "BT %d", s.ble_profile + 1);
    }
    if (strcmp(lv_label_get_text(conn_label), buf) != 0) {
        lv_label_set_text(conn_label, buf);
    }

    /* Layer label: name of highest active layer, truncated */
    const char *name = zmk_keymap_layer_name(s.active_layer);
    char layer_buf[10];
    if (name == NULL) {
        snprintf(layer_buf, sizeof(layer_buf), "L%u", s.active_layer);
    } else {
        snprintf(layer_buf, sizeof(layer_buf), "%s", name);
    }
    if (strcmp(lv_label_get_text(layer_label), layer_buf) != 0) {
        lv_label_set_text(layer_label, layer_buf);
    }

    /* Battery labels: "X NNN%" right-aligned, or "X  --" when unknown */
    for (int i = 0; i < NUM_LABELS; i++) {
        if (s.bat_valid[i]) {
            snprintf(buf, sizeof(buf), "%c%3u%%", label_chars[i], s.bat_levels[i]);
        } else {
            snprintf(buf, sizeof(buf), "%c --", label_chars[i]);
        }
        if (strcmp(lv_label_get_text(bat_labels[i]), buf) != 0) {
            lv_label_set_text(bat_labels[i], buf);
            lv_obj_align(bat_labels[i], LV_ALIGN_TOP_RIGHT, 0, i * 16);
        }
    }
}

K_WORK_DEFINE(update_work, update_display);

static void schedule_update(void) {
    if (zmk_display_is_initialized()) {
        k_work_submit_to_queue(zmk_display_work_q(), &update_work);
    }
}

static int dongle_bat_listener(const zmk_event_t *eh) {
    const struct zmk_battery_state_changed *ev = as_zmk_battery_state_changed(eh);
    if (ev) {
        k_mutex_lock(&state_mutex, K_FOREVER);
        dstate.bat_levels[0] = ev->state_of_charge;
        dstate.bat_valid[0]  = true;
        k_mutex_unlock(&state_mutex);
        schedule_update();
    }
    return ZMK_EV_EVENT_BUBBLE;
}

static int peripheral_bat_listener(const zmk_event_t *eh) {
    const struct zmk_peripheral_battery_state_changed *ev =
        as_zmk_peripheral_battery_state_changed(eh);
    if (ev && ev->source >= 1 && ev->source < NUM_LABELS) {
        k_mutex_lock(&state_mutex, K_FOREVER);
        dstate.bat_levels[ev->source] = ev->state_of_charge;
        dstate.bat_valid[ev->source]  = true;
        k_mutex_unlock(&state_mutex);
        schedule_update();
    }
    return ZMK_EV_EVENT_BUBBLE;
}

static int endpoint_listener(const zmk_event_t *eh) {
    const struct zmk_endpoint_changed *ev = as_zmk_endpoint_changed(eh);
    if (ev) {
        k_mutex_lock(&state_mutex, K_FOREVER);
        dstate.transport = ev->endpoint.transport;
        if (ev->endpoint.transport == ZMK_TRANSPORT_BLE) {
            dstate.ble_profile = ev->endpoint.ble.profile_index;
        }
        k_mutex_unlock(&state_mutex);
        schedule_update();
    }
    return ZMK_EV_EVENT_BUBBLE;
}

static int layer_listener(const zmk_event_t *eh) {
    const struct zmk_layer_state_changed *ev = as_zmk_layer_state_changed(eh);
    if (ev) {
        k_mutex_lock(&state_mutex, K_FOREVER);
        dstate.active_layer = zmk_keymap_highest_layer_active();
        k_mutex_unlock(&state_mutex);
        schedule_update();
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(dongle_bat_display, dongle_bat_listener);
ZMK_SUBSCRIPTION(dongle_bat_display, zmk_battery_state_changed);

ZMK_LISTENER(peripheral_bat_display, peripheral_bat_listener);
ZMK_SUBSCRIPTION(peripheral_bat_display, zmk_peripheral_battery_state_changed);

ZMK_LISTENER(endpoint_display, endpoint_listener);
ZMK_SUBSCRIPTION(endpoint_display, zmk_endpoint_changed);

ZMK_LISTENER(layer_display, layer_listener);
ZMK_SUBSCRIPTION(layer_display, zmk_layer_state_changed);

lv_obj_t *zmk_display_status_screen(void) {
    lv_obj_t *screen = lv_obj_create(NULL);

    conn_label = lv_label_create(screen);
    lv_obj_set_pos(conn_label, 0, 0);
    lv_label_set_text(conn_label, "USB");

    layer_label = lv_label_create(screen);
    lv_obj_set_pos(layer_label, 0, 48);
    lv_label_set_text(layer_label, "---");

    for (int i = 0; i < NUM_LABELS; i++) {
        bat_labels[i] = lv_label_create(screen);
        lv_obj_align(bat_labels[i], LV_ALIGN_TOP_RIGHT, 0, i * 16);
        lv_label_set_text(bat_labels[i], "? --");
    }

    struct zmk_endpoint_instance ep = zmk_endpoints_selected();
    k_mutex_lock(&state_mutex, K_FOREVER);
    dstate.bat_levels[0] = zmk_battery_state_of_charge();
    dstate.bat_valid[0]  = true;
    dstate.transport     = ep.transport;
    dstate.ble_profile   = (ep.transport == ZMK_TRANSPORT_BLE) ? ep.ble.profile_index : 0;
    dstate.active_layer  = zmk_keymap_highest_layer_active();
    k_mutex_unlock(&state_mutex);

    k_work_submit_to_queue(zmk_display_work_q(), &update_work);

    return screen;
}
