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
#include <zmk/events/split_esb_peripheral_changed.h>
#include <zmk_split_esb.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/* Custom device icons from dongle_icons.c (U+E000–U+E003, Private Use Area) */
LV_FONT_DECLARE(dongle_icons);
#define ICON_DONGLE   "\xEE\x80\x80"
#define ICON_LEFT_KB  "\xEE\x80\x81"
#define ICON_RIGHT_KB "\xEE\x80\x82"
#define ICON_MOUSE    "\xEE\x80\x83"

/* Signal-strength bars, 0..4 lit (U+E004–U+E008, see dongle_icons.c) */
#define SIG_BARS_0 "\xEE\x80\x84"
#define SIG_BARS_1 "\xEE\x80\x85"
#define SIG_BARS_2 "\xEE\x80\x86"
#define SIG_BARS_3 "\xEE\x80\x87"
#define SIG_BARS_4 "\xEE\x80\x88"

/* Large OS / game logos from os_logos.c (U+E010–U+E012), keyed off base layer */
LV_FONT_DECLARE(os_logos);
#define LOGO_APPLE "\xEE\x80\x90"
#define LOGO_WIN   "\xEE\x80\x91"
#define LOGO_PUBG  "\xEE\x80\x92"
#define LOGO_MOUSE "\xEE\x80\x93"

/* Layers that select which logo to draw (see roBakesb.keymap). The mouse layer
 * stacks on top of whatever base is active and takes priority on the display. */
#define LAYER_WINDOWS 1
#define LAYER_GAMING  2
#define LAYER_MOUSE   7

static const char *logo_syms[4] = { LOGO_APPLE, LOGO_WIN, LOGO_PUBG, LOGO_MOUSE };

/* One row per device: dongle + left KB + right KB + mouse. The mouse battery
 * is not delivered over ESB yet, so its row stays at "-- " until that lands. */
#define NUM_LABELS 4

#define BAT_RING_SIZE 24
#define BAT_RING_X0   76
#define BAT_RING_Y0   4
#define BAT_RING_GAP  4
#define BAT_DASH_COUNT 8
#define BAT_LOW_BLINK_THRESHOLD 25

/* Per-peripheral RF signal display. Bars replace the battery ring + device icon
 * for that slot; the dongle slot (row 0) is the central and has no link RSSI.
 * DONGLE_SIGNAL_BARS_ALWAYS=1 keeps bars up at all times (observation mode);
 * set it to 0 to show the device icon on an excellent link and bars only when
 * the link is below excellent. */
#define DONGLE_SIGNAL_BARS_ALWAYS 0
#define SIGNAL_EXCELLENT_BARS     4

static const char *label_syms[NUM_LABELS] = {
    ICON_DONGLE,
    ICON_LEFT_KB,
    ICON_RIGHT_KB,
    ICON_MOUSE,
};

static const char *sig_bars_syms[5] = {
    SIG_BARS_0, SIG_BARS_1, SIG_BARS_2, SIG_BARS_3, SIG_BARS_4,
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
    /* ESB link presence per row. Row 0 is the dongle itself (always present);
     * rows 1..3 follow the peripheral connection events and start hidden until
     * the central reports the peripheral connected. */
    bool    connected[NUM_LABELS];
    /* Last received signal per slot, dBm (negative); 0 = no sample yet. Slot 0
     * is the dongle and has no peripheral link, so it stays unused. */
    int8_t  rssi_dbm[NUM_LABELS];
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
    if (zmk_keymap_layer_active(LAYER_MOUSE)) {
        return 3; /* Mouse */
    }
    if (zmk_keymap_layer_active(LAYER_GAMING)) {
        return 2; /* PUBG */
    }
    if (zmk_keymap_layer_active(LAYER_WINDOWS)) {
        return 1; /* Windows */
    }
    return 0; /* Mac */
}

/* Map received signal (negative dBm) to 0..4 bars. 0 dBm is the "no sample /
 * out of range" sentinel and reads as 0 bars, not full. */
static uint8_t rssi_to_bars(int8_t dbm) {
    if (dbm == 0) {
        return 0;
    }
    if (dbm >= -60) {
        return 4;
    }
    if (dbm >= -70) {
        return 3;
    }
    if (dbm >= -80) {
        return 2;
    }
    if (dbm >= -90) {
        return 1;
    }
    return 0;
}

static void set_label_text(lv_obj_t *label, const char *text) {
    if (strcmp(lv_label_get_text(label), text) != 0) {
        lv_label_set_text(label, text);
    }
}

static void update_display(struct k_work *work) {
    k_mutex_lock(&state_mutex, K_FOREVER);
    struct display_state s = dstate;
    k_mutex_unlock(&state_mutex);

    if (logo_label == NULL) {
        return;
    }

    /* OS / game logo for the active base layer */
    const char *logo = logo_syms[s.os_logo < 4 ? s.os_logo : 0];
    if (strcmp(lv_label_get_text(logo_label), logo) != 0) {
        lv_label_set_text(logo_label, logo);
    }

    /* Apple-widget-inspired battery rings on the right half of the OLED. */
    for (int i = 0; i < NUM_LABELS; i++) {
        if (bat_arcs[i] == NULL) {
            continue;
        }

        /* A disconnected peripheral hides its whole slot — device icon, value
         * arc, and the dashed ring — so the layout reads as "not here". */
        if (!s.connected[i]) {
            set_obj_hidden(bat_arcs[i], true);
            set_obj_hidden(bat_icon_labels[i], true);
            set_dash_ring_hidden(i, true);
            continue;
        }

        /* Pick the center glyph: peripheral slots (1..) can show signal bars
         * nested inside the battery ring; the dongle (slot 0) has no link RSSI,
         * so it always keeps its device icon. The ring keeps showing battery. */
        const char *center_sym = label_syms[i];
        if (i >= 1) {
            uint8_t bars = rssi_to_bars(s.rssi_dbm[i]);
            if (DONGLE_SIGNAL_BARS_ALWAYS || bars < SIGNAL_EXCELLENT_BARS) {
                center_sym = sig_bars_syms[bars];
            }
        }
        set_label_text(bat_icon_labels[i], center_sym);

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

/* Poll each connected peripheral's link RSSI (there is no event for it) and
 * fold it into the display state. Returns true when a slot's bar level changed,
 * so the caller only redraws on a visible difference. */
static bool poll_signal(void) {
    bool changed = false;

    k_mutex_lock(&state_mutex, K_FOREVER);
    for (int i = 1; i < NUM_LABELS; i++) {
        if (!dstate.connected[i]) {
            continue;
        }
        int8_t dbm = zmk_split_esb_pipe_rssi_dbm(i - 1);
        if (rssi_to_bars(dbm) != rssi_to_bars(dstate.rssi_dbm[i])) {
            changed = true;
        }
        dstate.rssi_dbm[i] = dbm;
    }
    k_mutex_unlock(&state_mutex);

    return changed;
}

static void blink_work_cb(struct k_work *work) {
    ARG_UNUSED(work);

    bool need_update = poll_signal();

    if (any_low_battery()) {
        low_blink_visible = !low_blink_visible;
        need_update = true;
    } else {
        low_blink_visible = true;
    }
    if (need_update) {
        schedule_update();
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

static bool set_connected_state(uint8_t index, bool connected) {
    bool changed;

    k_mutex_lock(&state_mutex, K_FOREVER);
    changed = dstate.connected[index] != connected;
    if (changed) {
        dstate.connected[index] = connected;
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

/* Peripheral source is the ESB pipe (0=left, 1=right, 2=mouse). Display rows
 * reserve row 0 for the dongle, so a peripheral lives on row pipe+1. */
static int peripheral_bat_listener(const zmk_event_t *eh) {
    const struct zmk_peripheral_battery_state_changed *ev =
        as_zmk_peripheral_battery_state_changed(eh);
    if (ev && ev->source < NUM_LABELS - 1 &&
        set_battery_state(ev->source + 1, ev->state_of_charge)) {
        schedule_update();
    }
    return ZMK_EV_EVENT_BUBBLE;
}

/* ESB peripheral connect/disconnect. Source is the ESB pipe; it shares the
 * battery event's numbering, so a peripheral's ring and battery land on the
 * same row (pipe+1). Row 0 (the dongle) is never a peripheral source. */
static int peripheral_conn_listener(const zmk_event_t *eh) {
    const struct zmk_split_esb_peripheral_changed *ev =
        as_zmk_split_esb_peripheral_changed(eh);
    if (ev && ev->source < NUM_LABELS - 1 &&
        set_connected_state(ev->source + 1, ev->connected)) {
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

ZMK_LISTENER(peripheral_conn_display, peripheral_conn_listener);
ZMK_SUBSCRIPTION(peripheral_conn_display, zmk_split_esb_peripheral_changed);

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
    /* The dongle row is always present; peripherals stay hidden until their
     * connection event fires. */
    dstate.connected[0]  = true;
    dstate.os_logo       = current_os_logo();
    k_mutex_unlock(&state_mutex);

    k_work_submit_to_queue(zmk_display_work_q(), &update_work);
    k_work_reschedule(&blink_work, K_SECONDS(1));

    return screen;
}
