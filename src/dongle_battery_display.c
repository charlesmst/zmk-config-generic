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

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/* Custom device icons from dongle_icons.c (U+E000–U+E003, Private Use Area) */
LV_FONT_DECLARE(dongle_icons);
#define ICON_DONGLE   "\xEE\x80\x80"
#define ICON_LEFT_KB  "\xEE\x80\x81"
#define ICON_RIGHT_KB "\xEE\x80\x82"
#define ICON_MOUSE    "\xEE\x80\x83"

/* Large OS / game logos from os_logos.c (U+E010–U+E012), keyed off base layer */
LV_FONT_DECLARE(os_logos);
#define LOGO_APPLE "\xEE\x80\x90"
#define LOGO_WIN   "\xEE\x80\x91"
#define LOGO_PUBG  "\xEE\x80\x92"

/* Base layers that select which logo to draw (see roBakesb.keymap). */
#define LAYER_WINDOWS 1
#define LAYER_GAMING  2

static const char *logo_syms[3] = { LOGO_APPLE, LOGO_WIN, LOGO_PUBG };

/* One row per device: dongle + left KB + right KB + mouse. The mouse battery
 * is not delivered over ESB yet, so its row stays at "-- " until that lands. */
#define NUM_LABELS 4

#define BAT_RING_SIZE 24
#define BAT_RING_X0   76
#define BAT_RING_Y0   4
#define BAT_RING_GAP  4
#define BAT_DASH_COUNT 8
#define BAT_LOW_BLINK_THRESHOLD 25

static const char *label_syms[NUM_LABELS] = {
    ICON_DONGLE,
    ICON_LEFT_KB,
    ICON_RIGHT_KB,
    ICON_MOUSE,
};

static lv_obj_t *bat_arcs[NUM_LABELS];
static lv_obj_t *bat_icon_labels[NUM_LABELS];
static lv_obj_t *bat_dash_lines[NUM_LABELS][BAT_DASH_COUNT];
static lv_point_precise_t bat_dash_points[NUM_LABELS][BAT_DASH_COUNT][2];
static lv_obj_t *logo_label;
static bool low_blink_visible = true;

struct display_state {
    uint8_t bat_levels[NUM_LABELS];
    bool    bat_valid[NUM_LABELS];
    uint8_t os_logo;
};

K_MUTEX_DEFINE(state_mutex);
static struct display_state dstate;

static void schedule_update(void);
static void blink_work_cb(struct k_work *work);

K_WORK_DELAYABLE_DEFINE(blink_work, blink_work_cb);

static void set_obj_hidden(lv_obj_t *obj, bool hidden) {
    if (obj == NULL) {
        return;
    }

    if (lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN) == hidden) {
        return;
    }

    if (hidden) {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

static void set_dash_ring_hidden(uint8_t index, bool hidden) {
    for (int i = 0; i < BAT_DASH_COUNT; i++) {
        set_obj_hidden(bat_dash_lines[index][i], hidden);
    }
}

static bool any_low_battery(void) {
    bool low = false;

    k_mutex_lock(&state_mutex, K_FOREVER);
    for (int i = 0; i < NUM_LABELS; i++) {
        if (dstate.bat_valid[i] && dstate.bat_levels[i] < BAT_LOW_BLINK_THRESHOLD) {
            low = true;
            break;
        }
    }
    k_mutex_unlock(&state_mutex);

    return low;
}

/* Which logo matches the current base layer. Momentary layers stack on top of
 * the &to-selected base, so the base stays active and we test it directly. */
static uint8_t current_os_logo(void) {
    if (zmk_keymap_layer_active(LAYER_GAMING)) {
        return 2; /* PUBG */
    }
    if (zmk_keymap_layer_active(LAYER_WINDOWS)) {
        return 1; /* Windows */
    }
    return 0; /* Mac */
}

static void update_display(struct k_work *work) {
    k_mutex_lock(&state_mutex, K_FOREVER);
    struct display_state s = dstate;
    k_mutex_unlock(&state_mutex);

    if (logo_label == NULL) {
        return;
    }

    /* OS / game logo for the active base layer */
    const char *logo = logo_syms[s.os_logo < 3 ? s.os_logo : 0];
    if (strcmp(lv_label_get_text(logo_label), logo) != 0) {
        lv_label_set_text(logo_label, logo);
    }

    /* Apple-widget-inspired battery rings on the right half of the OLED. */
    for (int i = 0; i < NUM_LABELS; i++) {
        if (bat_arcs[i] == NULL) {
            continue;
        }

        uint8_t level;
        bool valid;
        level = s.bat_valid[i] ? s.bat_levels[i] : 0;
        valid = s.bat_valid[i];
        bool low = valid && level < BAT_LOW_BLINK_THRESHOLD;
        bool visible = !low || low_blink_visible;

        set_obj_hidden(bat_arcs[i], !valid || !visible);
        set_obj_hidden(bat_icon_labels[i], valid && !visible);
        set_dash_ring_hidden(i, valid);
        if (lv_arc_get_value(bat_arcs[i]) != level) {
            lv_arc_set_value(bat_arcs[i], level);
        }
    }
}

K_WORK_DEFINE(update_work, update_display);

static void blink_work_cb(struct k_work *work) {
    ARG_UNUSED(work);

    if (any_low_battery()) {
        low_blink_visible = !low_blink_visible;
        schedule_update();
    } else {
        low_blink_visible = true;
    }
    k_work_reschedule(&blink_work, K_SECONDS(1));
}

static void schedule_update(void) {
    if (zmk_display_is_initialized()) {
        k_work_submit_to_queue(zmk_display_work_q(), &update_work);
    }
}

static void create_dash_segment(lv_obj_t *screen, uint8_t index, uint8_t dash,
                                int16_t x, int16_t y, int16_t x1, int16_t y1,
                                int16_t x2, int16_t y2) {
    bat_dash_points[index][dash][0].x = x1;
    bat_dash_points[index][dash][0].y = y1;
    bat_dash_points[index][dash][1].x = x2;
    bat_dash_points[index][dash][1].y = y2;

    bat_dash_lines[index][dash] = lv_line_create(screen);
    lv_obj_remove_style_all(bat_dash_lines[index][dash]);
    lv_obj_set_pos(bat_dash_lines[index][dash], x, y);
    lv_obj_set_size(bat_dash_lines[index][dash], BAT_RING_SIZE, BAT_RING_SIZE);
    lv_line_set_points(bat_dash_lines[index][dash], bat_dash_points[index][dash], 2);
    lv_obj_set_style_line_width(bat_dash_lines[index][dash], 2, LV_PART_MAIN);
}

static void create_dash_ring(lv_obj_t *screen, uint8_t index, int16_t x, int16_t y) {
    create_dash_segment(screen, index, 0, x, y, 9, 1, 15, 1);
    create_dash_segment(screen, index, 1, x, y, 19, 4, 22, 8);
    create_dash_segment(screen, index, 2, x, y, 23, 10, 23, 14);
    create_dash_segment(screen, index, 3, x, y, 22, 16, 19, 20);
    create_dash_segment(screen, index, 4, x, y, 15, 23, 9, 23);
    create_dash_segment(screen, index, 5, x, y, 5, 20, 2, 16);
    create_dash_segment(screen, index, 6, x, y, 1, 14, 1, 10);
    create_dash_segment(screen, index, 7, x, y, 2, 8, 5, 4);
}

static bool set_battery_state(uint8_t index, uint8_t level) {
    bool changed;

    k_mutex_lock(&state_mutex, K_FOREVER);
    changed = !dstate.bat_valid[index] || dstate.bat_levels[index] != level;
    if (changed) {
        dstate.bat_levels[index] = level;
        dstate.bat_valid[index] = true;
    }
    k_mutex_unlock(&state_mutex);

    return changed;
}

static int dongle_bat_listener(const zmk_event_t *eh) {
    const struct zmk_battery_state_changed *ev = as_zmk_battery_state_changed(eh);
    if (ev && set_battery_state(0, ev->state_of_charge)) {
        schedule_update();
    }
    return ZMK_EV_EVENT_BUBBLE;
}

static int peripheral_bat_listener(const zmk_event_t *eh) {
    const struct zmk_peripheral_battery_state_changed *ev =
        as_zmk_peripheral_battery_state_changed(eh);
    if (ev && ev->source >= 1 && ev->source < NUM_LABELS &&
        set_battery_state(ev->source, ev->state_of_charge)) {
        schedule_update();
    }
    return ZMK_EV_EVENT_BUBBLE;
}

static int layer_listener(const zmk_event_t *eh) {
    const struct zmk_layer_state_changed *ev = as_zmk_layer_state_changed(eh);
    if (ev) {
        uint8_t os_logo = current_os_logo();
        bool changed;

        k_mutex_lock(&state_mutex, K_FOREVER);
        changed = dstate.os_logo != os_logo;
        if (changed) {
            dstate.os_logo = os_logo;
        }
        k_mutex_unlock(&state_mutex);

        if (changed) {
            schedule_update();
        }
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(dongle_bat_display, dongle_bat_listener);
ZMK_SUBSCRIPTION(dongle_bat_display, zmk_battery_state_changed);

ZMK_LISTENER(peripheral_bat_display, peripheral_bat_listener);
ZMK_SUBSCRIPTION(peripheral_bat_display, zmk_peripheral_battery_state_changed);

ZMK_LISTENER(layer_display, layer_listener);
ZMK_SUBSCRIPTION(layer_display, zmk_layer_state_changed);

lv_obj_t *zmk_display_status_screen(void) {
    lv_obj_t *screen = lv_obj_create(NULL);

    /* Large OS/game logo filling the left side of the screen. */
    logo_label = lv_label_create(screen);
    lv_obj_set_style_text_font(logo_label, &os_logos, LV_PART_MAIN);
    lv_obj_set_pos(logo_label, 2, 10);
    lv_label_set_text(logo_label, LOGO_APPLE);

    for (int i = 0; i < NUM_LABELS; i++) {
        int col = i % 2;
        int row = i / 2;
        int x = BAT_RING_X0 + col * (BAT_RING_SIZE + BAT_RING_GAP);
        int y = BAT_RING_Y0 + row * (BAT_RING_SIZE + BAT_RING_GAP);

        bat_arcs[i] = lv_arc_create(screen);
        lv_obj_remove_style_all(bat_arcs[i]);
        lv_obj_set_size(bat_arcs[i], BAT_RING_SIZE, BAT_RING_SIZE);
        lv_obj_set_pos(bat_arcs[i], x, y);
        lv_arc_set_range(bat_arcs[i], 0, 100);
        lv_arc_set_bg_angles(bat_arcs[i], 0, 360);
        lv_arc_set_rotation(bat_arcs[i], 270);
        lv_arc_set_value(bat_arcs[i], 0);
        lv_obj_remove_style(bat_arcs[i], NULL, LV_PART_KNOB);
        lv_obj_clear_flag(bat_arcs[i], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_opa(bat_arcs[i], LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(bat_arcs[i], 0, LV_PART_MAIN);
        lv_obj_set_style_arc_width(bat_arcs[i], 0, LV_PART_MAIN);
        lv_obj_set_style_arc_width(bat_arcs[i], 2, LV_PART_INDICATOR);
        lv_obj_set_style_pad_all(bat_arcs[i], 0, LV_PART_MAIN);

        create_dash_ring(screen, i, x, y);
        set_dash_ring_hidden(i, true);

        bat_icon_labels[i] = lv_label_create(screen);
        lv_obj_set_style_text_font(bat_icon_labels[i], &dongle_icons, LV_PART_MAIN);
        lv_obj_set_pos(bat_icon_labels[i], x + 8, y + 5);
        lv_label_set_text(bat_icon_labels[i], label_syms[i]);
    }

    k_mutex_lock(&state_mutex, K_FOREVER);
    dstate.bat_levels[0] = zmk_battery_state_of_charge();
    dstate.bat_valid[0]  = true;
    dstate.os_logo       = current_os_logo();
    k_mutex_unlock(&state_mutex);

    k_work_submit_to_queue(zmk_display_work_q(), &update_work);
    k_work_reschedule(&blink_work, K_SECONDS(1));

    return screen;
}
