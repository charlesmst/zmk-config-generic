#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <string.h>
#include <lvgl.h>

#include <zmk/display.h>
#include <zmk/display/status_screen.h>
#include <zmk/battery.h>
#include <zmk/event_manager.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/keymap.h>
#include <zmk/events/layer_state_changed.h>

#ifdef CONFIG_ZMK_BLE
#include <zmk/endpoints.h>
#include <zmk/endpoints_types.h>
#include <zmk/events/endpoint_changed.h>
#include <zmk/ble.h>
#include <zmk/events/ble_active_profile_changed.h>
#endif

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/* Custom device icons from dongle_icons.c (U+E000–U+E003, Private Use Area) */
LV_FONT_DECLARE(dongle_icons);
#define ICON_DONGLE   "\xEE\x80\x80"
#define ICON_LEFT_KB  "\xEE\x80\x81"
#define ICON_RIGHT_KB "\xEE\x80\x82"
#define ICON_MOUSE    "\xEE\x80\x83"

/* One row per device: dongle + left KB + right KB + mouse. The mouse battery
 * is not delivered over ESB yet, so its row stays at "-- " until that lands. */
#define NUM_LABELS 4

static const char *label_syms[NUM_LABELS] = {
    ICON_DONGLE,
    ICON_LEFT_KB,
    ICON_RIGHT_KB,
    ICON_MOUSE,
};

static lv_obj_t *bat_labels[NUM_LABELS];
static lv_obj_t *conn_label;
static lv_obj_t *layer_label;

struct display_state {
    uint8_t bat_levels[NUM_LABELS];
    bool    bat_valid[NUM_LABELS];
    uint8_t active_layer;
#ifdef CONFIG_ZMK_BLE
    enum zmk_transport transport;
    int                ble_profile;
    bool               ble_connected;
    bool               ble_bonded;
#endif
};

K_MUTEX_DEFINE(state_mutex);
static struct display_state dstate;

static void update_display(struct k_work *work) {
    k_mutex_lock(&state_mutex, K_FOREVER);
    struct display_state s = dstate;
    k_mutex_unlock(&state_mutex);

    char buf[24];

#ifdef CONFIG_ZMK_BLE
    if (s.transport == ZMK_TRANSPORT_USB) {
        strncpy(buf, LV_SYMBOL_USB, sizeof(buf));
    } else if (s.ble_bonded) {
        if (s.ble_connected) {
            snprintf(buf, sizeof(buf), LV_SYMBOL_WIFI " %d " LV_SYMBOL_OK, s.ble_profile + 1);
        } else {
            snprintf(buf, sizeof(buf), LV_SYMBOL_WIFI " %d " LV_SYMBOL_CLOSE, s.ble_profile + 1);
        }
    } else {
        snprintf(buf, sizeof(buf), LV_SYMBOL_WIFI " %d " LV_SYMBOL_SETTINGS, s.ble_profile + 1);
    }
#else
    strncpy(buf, LV_SYMBOL_USB, sizeof(buf));
#endif
    if (strcmp(lv_label_get_text(conn_label), buf) != 0) {
        lv_label_set_text(conn_label, buf);
    }

    /* Layer label */
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

    /* Battery labels: NNN% then the device icon on the right (or "-- icon"). */
    for (int i = 0; i < NUM_LABELS; i++) {
        if (s.bat_valid[i]) {
            snprintf(buf, sizeof(buf), "%3u%% %s", s.bat_levels[i], label_syms[i]);
        } else {
            snprintf(buf, sizeof(buf), "-- %s", label_syms[i]);
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

#ifdef CONFIG_ZMK_BLE
static void refresh_ble_state(void) {
    struct zmk_endpoint_instance ep = zmk_endpoints_selected();
    k_mutex_lock(&state_mutex, K_FOREVER);
    dstate.transport     = ep.transport;
    dstate.ble_profile   = (ep.transport == ZMK_TRANSPORT_BLE) ? ep.ble.profile_index : dstate.ble_profile;
    dstate.ble_connected = zmk_ble_active_profile_is_connected();
    dstate.ble_bonded    = !zmk_ble_active_profile_is_open();
    k_mutex_unlock(&state_mutex);
}

static int endpoint_listener(const zmk_event_t *eh) {
    refresh_ble_state();
    schedule_update();
    return ZMK_EV_EVENT_BUBBLE;
}

static int ble_profile_listener(const zmk_event_t *eh) {
    refresh_ble_state();
    schedule_update();
    return ZMK_EV_EVENT_BUBBLE;
}
#endif

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

#ifdef CONFIG_ZMK_BLE
ZMK_LISTENER(endpoint_display, endpoint_listener);
ZMK_SUBSCRIPTION(endpoint_display, zmk_endpoint_changed);

ZMK_LISTENER(ble_profile_display, ble_profile_listener);
ZMK_SUBSCRIPTION(ble_profile_display, zmk_ble_active_profile_changed);
#endif

ZMK_LISTENER(layer_display, layer_listener);
ZMK_SUBSCRIPTION(layer_display, zmk_layer_state_changed);

lv_obj_t *zmk_display_status_screen(void) {
    lv_obj_t *screen = lv_obj_create(NULL);

    conn_label = lv_label_create(screen);
    lv_obj_set_pos(conn_label, 0, 0);
    lv_label_set_text(conn_label, LV_SYMBOL_USB);

    layer_label = lv_label_create(screen);
    lv_obj_set_pos(layer_label, 0, 48);
    lv_label_set_text(layer_label, "---");

    for (int i = 0; i < NUM_LABELS; i++) {
        bat_labels[i] = lv_label_create(screen);
        lv_obj_set_style_text_font(bat_labels[i], &dongle_icons, LV_PART_MAIN);
        lv_obj_align(bat_labels[i], LV_ALIGN_TOP_RIGHT, 0, i * 16);
        lv_label_set_text(bat_labels[i], label_syms[i]);
    }

    k_mutex_lock(&state_mutex, K_FOREVER);
    dstate.bat_levels[0] = zmk_battery_state_of_charge();
    dstate.bat_valid[0]  = true;
    dstate.active_layer  = zmk_keymap_highest_layer_active();
    k_mutex_unlock(&state_mutex);
#ifdef CONFIG_ZMK_BLE
    refresh_ble_state();
#endif

    k_work_submit_to_queue(zmk_display_work_q(), &update_work);

    return screen;
}
