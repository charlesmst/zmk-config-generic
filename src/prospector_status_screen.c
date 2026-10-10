/*
 * Custom status screen for the Prospector dongle (280x240, ST7789 rotated 270).
 *
 * Styled after the iOS Batteries widget: a ring per device on true black, the
 * percentage large in the middle, a Title Case label under it, and colour
 * carrying meaning rather than decoration -- green, yellow at 20%, red at 10%,
 * the same ladder iOS uses. Rounded arc caps, because square ends are the
 * giveaway that a ring was not drawn by Apple.
 *
 * Per device: a battery arc with the percentage inside, and antenna bars for
 * signal underneath. Both facts at once, which is what lets this stay a single
 * view with nothing to page.
 *
 * The bars are drawn twice -- all four in the track colour, then the lit count
 * over the top in a semantic colour. That is how iOS renders cellular strength,
 * and it keeps the unlit remainder reading as "bars that could be filled"
 * rather than as empty space.
 *
 * They come from src/signal_bars.c at 32x26, not the 8x10 set in
 * dongle_icons.c: that one is sized for a 128x64 OLED and on this panel a drop
 * from four bars to two was easy to miss entirely. The lit colour shifts as the
 * link degrades too, since a hue change carries further across a desk than a
 * missing bar.
 *
 * Above: which machine the keymap is driving, the largest thing on the glass.
 * Below: hop channel and link health, in secondary grey.
 *
 * The dongle itself gets no ring -- it runs off USB and has no battery.
 *
 * Gaming mode is a second tree on the same screen, shown while the gaming base
 * layer is active and hidden otherwise. It drops the battery rings, the OS and
 * the layer line and shows nothing but signal: three segmented meters, twelve
 * segments each, in their own palette. The reasoning is that in a match none
 * of what the base screen answers is still a live question -- you chose the
 * layer, the batteries were fine a minute ago -- whereas the link is the one
 * thing that can still go wrong while your hands are busy. Twelve segments
 * across 40 dB also resolves roughly ten times finer than the four-tier
 * antenna glyph, which is what makes a link visibly *drifting* rather than
 * merely crossing a threshold.
 *
 * Shape is lifted from src/dongle_battery_display.c, which has driven the OLED
 * dongle over ESB all along: own the state, poll the transport, redraw on
 * change, gate the redraw on zmk_display_is_initialized(). Deliberately not
 * event-driven for values -- prospector-zmk-module's own widgets are, and a
 * value that changes before the screen exists is then lost for good. Liveness
 * is the exception and must come from an event; see edge_seen below.
 */

#include <stdio.h>
#include <string.h>

#include <lvgl.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <zmk/display.h>
#include <zmk/display/status_screen.h>
#include <zmk/event_manager.h>
#include <zmk/events/split_esb_peripheral_changed.h>
#include <zmk/keymap.h>
#include <zmk_split_esb.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/* Glyph fonts from this repo. os_logos is shared with the OLED dongle screen;
 * signal_bars is drawn for this panel's size. */
LV_FONT_DECLARE(os_logos);
LV_FONT_DECLARE(signal_bars);
LV_FONT_DECLARE(periph_icons);

/*
 * Text faces bundled by prospector-zmk-module. Its fonts.h only declares them
 * under one of the four stock layouts and we are PROSPECTOR_STATUS_SCREEN_NONE,
 * so declare what we use; the sources compile unconditionally either way.
 *
 * FG_Medium_21, not the 20 or 26 cut: those two stop at U+0060, so they have
 * no lowercase at all and can only set ALL CAPS. Title Case is a large part of
 * why an iOS widget reads the way it does, so the face with lowercase wins
 * even though it is a pixel taller than ideal.
 */
LV_FONT_DECLARE(FR_Regular_36);
LV_FONT_DECLARE(FG_Medium_21);
/* Gaming mode sets everything in this one face. A condensed DIN is the voice
 * of a readout rather than of a widget, which is the whole difference between
 * the two screens, and condensed buys the room for "PERIPHERAL LOST" on a line
 * that also has to carry the channel. */
LV_FONT_DECLARE(DINishCondensed_SemiBold_22);

/* os_logos.c, U+E010.. */
#define LOGO_APPLE "\xEE\x80\x90"
#define LOGO_WIN   "\xEE\x80\x91"
#define LOGO_GAME  "\xEE\x80\x92"
#define LOGO_MOUSE "\xEE\x80\x93"

/* signal_bars.c, U+E020.. -- 0 to 4 lit, plus all four for the dim track */
static const char *const bar_syms[5] = {
    "\xEE\x80\xA0", "\xEE\x80\xA1", "\xEE\x80\xA2", "\xEE\x80\xA3", "\xEE\x80\xA4",
};
#define BARS_TRACK "\xEE\x80\xA5"

/* periph_icons.c, U+E030.. -- which device this ring belongs to. An icon in
 * place of a percentage: the arc already carries the level, and a figure in the
 * middle of it was saying the same thing twice in a face that was never
 * designed for this screen. */
static const char *const periph_syms[3] = {
    "\xEE\x80\xB0", "\xEE\x80\xB1", "\xEE\x80\xB2",
};

#define EN_DASH "\xE2\x80\x93"

/* Base layers, matching roBakesb.keymap. The mouse layer stacks on top of
 * whichever base is active and takes priority, as current_os_logo() decides
 * it for the OLED. */
#define LAYER_MAC     0
#define LAYER_WINDOWS 1
#define LAYER_GAMING  2
#define LAYER_MOUSE   7

struct base_identity {
    const char *logo;
    const char *name;
};

static const struct base_identity bases[4] = {
    {LOGO_APPLE, "macOS"},
    {LOGO_WIN, "Windows"},
    {LOGO_GAME, "Gaming"},
    {LOGO_MOUSE, "Mouse"},
};

/* ESB pipes, fixed at build time by roBakesb_addr.dtsi -- pipe number is the
 * slot, and there is no pairing order to account for. Slot 0 is the left half,
 * 1 the right, 2 the lariska, which is the order periph_syms is drawn in. */
#define SLOT_COUNT 3

#define BATTERY_UNKNOWN 0xFF

/* ---- geometry (280x240) ------------------------------------------------- */

#define SCREEN_W 280
#define SCREEN_H 240

/* The vertical rhythm. The first cut was laid out against nominal point sizes
 * rather than the faces' actual line_height, and those do not agree: FG_Medium
 * _21 is 25 px tall and FR_Regular_36 is 40, not 36. Two things followed from
 * that, both of them visible on the panel:
 *
 *   - name at y8 ran to y48, and the subtitle started at y46. They overlapped
 *     by 2 px.
 *   - the subtitle ran to y71, and the ring row started at y68. Overlapped by
 *     another 3.
 *
 * So the header was not merely crowding the rings, it was sitting on top of
 * them, while 30 px of nothing sat between the bars and the footer. Laid out
 * on real box heights the slack comes to 48 px, spread as below. The one gap
 * kept tight is ring-to-bars: those two describe the same device and have to
 * read as one object, which only works while the gap inside the group stays
 * clearly smaller than the gaps either side of it.
 *
 *    6  top margin
 *    .  name, 40
 *    .  subtitle, 25            (abuts the name; the faces' ink does not)
 *   13  header -> rings
 *    .  ring, 76
 *    5  ring -> its own bars    <- tightest gap on the screen, deliberately
 *    .  bars, 26
 *   15  bars -> footer
 *    .  footer, 25
 *    9  bottom margin
 */
#define LOGO_X 14
#define LOGO_Y 12
#define NAME_X 66
#define NAME_Y 6
#define SUB_Y  46

/* 76, not 80: at 80 the row spans 276 of 280 px and clears the bezel by two
 * pixels a side, which looks like a crop rather than a margin. Four pixels off
 * the diameter buys 6 px of air at each end and is not visible in the ring. */
#define RING_SIZE 76
#define RING_GAP  20
#define RING_Y    84
/* Roughly a sixth of the diameter, which is the proportion Apple's rings use.
 * Thinner reads as a progress bar bent into a circle. */
#define RING_W    12

#define BARS_W 32
#define BARS_H 26

#define ICON_SIZE 34
#define ICON_OFFSET ((RING_SIZE - ICON_SIZE) / 2)

#define BARS_Y   (RING_Y + RING_SIZE + 5)
#define FOOTER_Y 206

/* Centred as a group rather than padded from the left, so the row reads as one
 * object the way a widget's contents do. */
#define RING_ROW_W ((SLOT_COUNT * RING_SIZE) + ((SLOT_COUNT - 1) * RING_GAP))
#define RING_X0    ((SCREEN_W - RING_ROW_W) / 2)

static inline int ring_x(uint8_t slot) {
    return RING_X0 + slot * (RING_SIZE + RING_GAP);
}

/* ---- geometry, gaming mode --------------------------------------------- */

/* Twelve segments of 17 with 4 px between them comes to 248, which centres on
 * 280 with a whole 16 px either side. Picked for that: the obvious 16 segments
 * leaves a fractional segment width, and rounding it scatters a stray pixel
 * along the row that is very visible on a meter made of repeated parts. */
#define GAME_SEGMENTS  12
#define GAME_SEG_W     17
#define GAME_SEG_GAP   4
#define GAME_BAR_W     ((GAME_SEGMENTS * GAME_SEG_W) + ((GAME_SEGMENTS - 1) * GAME_SEG_GAP))
#define GAME_BAR_H     18
#define GAME_X         ((SCREEN_W - GAME_BAR_W) / 2)

/* DINishCondensed_SemiBold_22 is 22 px per line.
 *
 *   14  top margin
 *    .  header, 22
 *    8
 *    .  rule, 1
 *   16
 *    .  row: label 22, 4, meter 18   = 44
 *   14
 *    .  row                          = 44
 *   14
 *    .  row                          = 44
 *   19  bottom margin
 */
#define GAME_HEAD_Y    14
#define GAME_RULE_Y    44
#define GAME_ROW0_Y    61
#define GAME_ROW_PITCH 58
#define GAME_LABEL_H   22
#define GAME_METER_DY  (GAME_LABEL_H + 4)

static inline int game_row_y(uint8_t slot) {
    return GAME_ROW0_Y + slot * GAME_ROW_PITCH;
}

static inline int game_seg_x(uint8_t seg) {
    return GAME_X + seg * (GAME_SEG_W + GAME_SEG_GAP);
}

/* ---- colours ----------------------------------------------------------- */

#define COL_BG        lv_color_hex(CONFIG_ROBA_PROSPECTOR_SCREEN_BG)
#define COL_PRIMARY   lv_color_hex(CONFIG_ROBA_PROSPECTOR_SCREEN_PRIMARY)
#define COL_SECONDARY lv_color_hex(CONFIG_ROBA_PROSPECTOR_SCREEN_SECONDARY)
#define COL_GOOD      lv_color_hex(CONFIG_ROBA_PROSPECTOR_SCREEN_GOOD)
#define COL_WARN      lv_color_hex(CONFIG_ROBA_PROSPECTOR_SCREEN_WARN)
#define COL_CRIT      lv_color_hex(CONFIG_ROBA_PROSPECTOR_SCREEN_CRIT)
#define COL_SIGNAL    lv_color_hex(CONFIG_ROBA_PROSPECTOR_SCREEN_SIGNAL)
#define COL_TRACK     lv_color_hex(CONFIG_ROBA_PROSPECTOR_SCREEN_TRACK)
#define COL_BARS_TRACK lv_color_hex(CONFIG_ROBA_PROSPECTOR_SCREEN_BARS_TRACK)

#define COL_GAME_GOOD  lv_color_hex(CONFIG_ROBA_PROSPECTOR_GAMING_GOOD)
#define COL_GAME_WARN  lv_color_hex(CONFIG_ROBA_PROSPECTOR_GAMING_WARN)
#define COL_GAME_CRIT  lv_color_hex(CONFIG_ROBA_PROSPECTOR_GAMING_CRIT)
#define COL_GAME_TRACK lv_color_hex(CONFIG_ROBA_PROSPECTOR_GAMING_TRACK)

/* ---- state ------------------------------------------------------------- */

struct slot_state {
    uint8_t battery; /* 0..100, or BATTERY_UNKNOWN */
    int8_t rssi;     /* dBm, 0 = no sample */
    bool present;
};

struct screen_state {
    struct slot_state slots[SLOT_COUNT];
    uint8_t base;
    uint8_t channel;
    bool searching;
    bool gaming; /* gaming base layer held -- swap to the meters */
    const char *sub; /* momentary layer name, NULL when none */
};

static K_MUTEX_DEFINE(state_mutex);
static struct screen_state state;

/*
 * Liveness has to come from the transport's edge event, not from polling.
 *
 * zmk_split_esb_peer_battery() and zmk_split_esb_peer_rssi_dbm() are
 * last-known caches with no expiry: central.c writes tracked_battery_levels[]
 * only when a level changes and clears it only in central_init(), and
 * hop_central.c writes pipe_rssi_dbm[] only when a packet lands. Neither
 * reverts, so a peripheral that goes away leaves its last battery and signal
 * sitting there and polling cannot tell the difference -- switch the lariska
 * off and its ring keeps reporting a healthy link.
 *
 * zmk_split_esb_peripheral_changed is the only thing that reports a peripheral
 * going away. It is edge-triggered, which is why values are still polled; this
 * is a plain ZMK_LISTENER, so it observes edges whether or not the screen
 * exists yet and the cache means an early edge is not lost.
 */
static bool edge_seen[SLOT_COUNT];
static bool edge_connected[SLOT_COUNT];

static int peripheral_changed_listener(const zmk_event_t *eh) {
    const struct zmk_split_esb_peripheral_changed *ev = as_zmk_split_esb_peripheral_changed(eh);

    if (ev == NULL || ev->source >= SLOT_COUNT) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    k_mutex_lock(&state_mutex, K_FOREVER);
    edge_seen[ev->source] = true;
    edge_connected[ev->source] = ev->connected;
    k_mutex_unlock(&state_mutex);

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(roba_prospector_screen, peripheral_changed_listener);
ZMK_SUBSCRIPTION(roba_prospector_screen, zmk_split_esb_peripheral_changed);

/* widgets */
static lv_obj_t *logo_label;
static lv_obj_t *name_label;
static lv_obj_t *sub_label;
static lv_obj_t *ring_bat[SLOT_COUNT];
static lv_obj_t *bars_track[SLOT_COUNT];
static lv_obj_t *bars_lit[SLOT_COUNT];
static lv_obj_t *icon_label[SLOT_COUNT];
static lv_obj_t *chan_label;
static lv_obj_t *health_label;

/* The two trees hang off a container each, and switching modes hides one and
 * shows the other. Containers rather than a list of widgets to walk: one flag
 * on a parent takes its whole subtree out of the render pass, and it cannot
 * drift out of sync with the widgets the way a hand-maintained array can.
 *
 * Both are built at boot rather than created and destroyed per layer change.
 * Building a tree inside the redraw handler would run allocation on the
 * display work queue at the instant the layer flips, which is precisely when
 * the panel is about to flush. Hidden widgets cost their object headers and
 * nothing per frame -- LVGL drops them before they reach the renderer. */
static lv_obj_t *base_root;
static lv_obj_t *game_root;

static lv_obj_t *game_head_label;
static lv_obj_t *game_chan_label;
static lv_obj_t *game_name_label[SLOT_COUNT];
static lv_obj_t *game_dbm_label[SLOT_COUNT];
static lv_obj_t *game_seg[SLOT_COUNT][GAME_SEGMENTS];

static const char *const game_names[SLOT_COUNT] = {"LEFT", "RIGHT", "MOUSE"};

/* ---- helpers ----------------------------------------------------------- */

/* Same thresholds as the OLED screen, so the two agree what "3 bars" means.
 * 0 dBm is the transport's no-sample sentinel and reads as nothing, not full. */
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

/* Colour as well as length, so a drop is obvious without reading the arc. */
static lv_color_t signal_colour(int8_t dbm) {
    if (dbm == 0) {
        return COL_TRACK;
    }
    if (dbm >= -70) {
        return COL_SIGNAL;
    }
    if (dbm >= -85) {
        return COL_WARN;
    }
    return COL_CRIT;
}

/* Gaming meter: linear between the floor and the top, which resolves ~3.3 dB
 * per segment against the antenna glyph's four fixed tiers. */
static uint8_t rssi_to_segments(int8_t dbm) {
    const int floor_dbm = CONFIG_ROBA_PROSPECTOR_GAMING_RSSI_FLOOR;
    const int top_dbm = CONFIG_ROBA_PROSPECTOR_GAMING_RSSI_TOP;
    const int span = top_dbm - floor_dbm;

    if (dbm == 0) {
        return 0;
    }
    if (dbm >= top_dbm) {
        return GAME_SEGMENTS;
    }
    if (dbm <= floor_dbm) {
        /* A sample this bad is still a sample. Zero segments is reserved for
         * "nothing on this pipe", and the two must not look alike. */
        return 1;
    }

    int lit = (((int)dbm - floor_dbm) * GAME_SEGMENTS + (span / 2)) / span;

    return (uint8_t)MAX(lit, 1);
}

/* Same thresholds as signal_colour(), so a link that is amber on one screen is
 * amber on the other. Only the palette differs. */
static lv_color_t gaming_colour(int8_t dbm) {
    if (dbm == 0) {
        return COL_GAME_TRACK;
    }
    if (dbm >= -70) {
        return COL_GAME_GOOD;
    }
    if (dbm >= -85) {
        return COL_GAME_WARN;
    }
    return COL_GAME_CRIT;
}

static lv_color_t battery_colour(uint8_t pct) {
    if (pct <= CONFIG_ROBA_PROSPECTOR_SCREEN_CRIT_PCT) {
        return COL_CRIT;
    }
    if (pct <= CONFIG_ROBA_PROSPECTOR_SCREEN_WARN_PCT) {
        return COL_WARN;
    }
    return COL_GOOD;
}

static uint8_t current_base(void) {
    if (zmk_keymap_layer_active(LAYER_MOUSE)) {
        return 3;
    }
    if (zmk_keymap_layer_active(LAYER_GAMING)) {
        return 2;
    }
    if (zmk_keymap_layer_active(LAYER_WINDOWS)) {
        return 1;
    }
    return 0;
}

/* The momentary layer held on top, by display-name. Suppressed when the
 * highest active layer is itself a base, so the identity above is never
 * restated underneath itself. */
static const char *current_sub(void) {
    zmk_keymap_layer_index_t index = zmk_keymap_highest_layer_active();
    zmk_keymap_layer_id_t id = zmk_keymap_layer_index_to_id(index);

    if (id == LAYER_MAC || id == LAYER_WINDOWS || id == LAYER_GAMING) {
        return NULL;
    }

    return zmk_keymap_layer_name(id);
}

static void set_text(lv_obj_t *label, const char *text) {
    if (label == NULL || text == NULL) {
        return;
    }
    if (strcmp(lv_label_get_text(label), text) != 0) {
        lv_label_set_text(label, text);
    }
}

/* ---- redraw (display work queue) --------------------------------------- */

enum link_health {
    HEALTH_OK = 0,
    HEALTH_LOST,
    HEALTH_SEARCHING,
};

/* Worst thing first: a lost half matters more than a weak one, and a central
 * that is still sweeping matters more than either. */
static enum link_health link_health(const struct screen_state *s) {
    if (s->searching) {
        return HEALTH_SEARCHING;
    }
    for (uint8_t i = 0; i < SLOT_COUNT; i++) {
        if (!s->slots[i].present) {
            return HEALTH_LOST;
        }
    }
    return HEALTH_OK;
}

static const char *const health_text[] = {"Connected", "Peripheral lost", "Searching"};
static const char *const game_health_text[] = {"LINK STABLE", "PERIPHERAL LOST", "SEARCHING"};

static void redraw_base(const struct screen_state *sp) {
    const struct screen_state s = *sp;

    set_text(logo_label, bases[s.base].logo);
    set_text(name_label, bases[s.base].name);

    /* Momentary layer in blue when held, the base restated in grey when not,
     * so the line never empties and the row below never shifts. */
    if (s.sub != NULL) {
        set_text(sub_label, s.sub);
        lv_obj_set_style_text_color(sub_label, COL_SIGNAL, LV_PART_MAIN);
    } else {
        set_text(sub_label, "Base layer");
        lv_obj_set_style_text_color(sub_label, COL_SECONDARY, LV_PART_MAIN);
    }

    for (uint8_t i = 0; i < SLOT_COUNT; i++) {
        const struct slot_state *slot = &s.slots[i];
        bool known = slot->present && slot->battery != BATTERY_UNKNOWN;

        if (known) {
            lv_arc_set_value(ring_bat[i], slot->battery);
            lv_obj_set_style_arc_color(ring_bat[i], battery_colour(slot->battery),
                                       LV_PART_INDICATOR);
        } else {
            /* Empty ring, and the icon drops to the track colour. A peripheral
             * that has not reported is a different thing from one reporting 0%,
             * and an empty ring alone would not say which. */
            lv_arc_set_value(ring_bat[i], 0);
        }

        lv_obj_set_style_text_color(icon_label[i], known ? COL_PRIMARY : COL_TRACK,
                                    LV_PART_MAIN);

        uint8_t lit = rssi_to_bars(slot->rssi);
        set_text(bars_lit[i], bar_syms[lit]);
        lv_obj_set_style_text_color(bars_lit[i], signal_colour(slot->rssi), LV_PART_MAIN);

    }

    char chan[16];
    snprintf(chan, sizeof(chan), "Ch %u", s.channel);
    set_text(chan_label, chan);

    enum link_health health = link_health(&s);

    set_text(health_label, health_text[health]);
    lv_obj_set_style_text_color(health_label,
                                health == HEALTH_OK ? COL_SECONDARY : COL_WARN, LV_PART_MAIN);
}

/* Last drawn state per meter, so a poll that changes nothing costs nothing.
 * Without this every tick would re-set the colour on all 36 segments, and
 * lv_obj_set_style_bg_color() invalidates unconditionally -- it does not
 * compare against the value already there. That would hand the renderer the
 * whole meter block four times a second to redraw it identically. */
static uint8_t game_drawn_lit[SLOT_COUNT];
static lv_color_t game_drawn_colour[SLOT_COUNT];
static bool game_drawn_present[SLOT_COUNT];
static bool game_drawn_valid[SLOT_COUNT];

static void redraw_gaming(const struct screen_state *s) {
    enum link_health health = link_health(s);

    set_text(game_head_label, game_health_text[health]);
    lv_obj_set_style_text_color(game_head_label,
                                health == HEALTH_OK ? COL_GAME_GOOD : COL_GAME_WARN,
                                LV_PART_MAIN);

    char chan[16];
    snprintf(chan, sizeof(chan), "CH %u", s->channel);
    set_text(game_chan_label, chan);

    for (uint8_t i = 0; i < SLOT_COUNT; i++) {
        const struct slot_state *slot = &s->slots[i];
        bool live = slot->present && slot->rssi != 0;

        uint8_t lit = live ? rssi_to_segments(slot->rssi) : 0;
        lv_color_t colour = live ? gaming_colour(slot->rssi) : COL_GAME_TRACK;

        /* "--", not an en dash: this face carries U+0020..U+007E and nothing
         * else, so anything outside ASCII draws LVGL's missing-glyph box. */
        char dbm[8];
        if (live) {
            snprintf(dbm, sizeof(dbm), "%d", slot->rssi);
        } else {
            strcpy(dbm, "--");
        }
        set_text(game_dbm_label[i], dbm);

        /* lit, colour and present are the whole of what this row draws, so if
         * none of the three moved there is nothing to say. Worth the cache
         * because present alone is not implied by the other two: a peripheral
         * that is connected but has not yet reported RSSI and one that is gone
         * both land on zero segments in the track colour, and only the device
         * name tells them apart. */
        if (game_drawn_valid[i] && game_drawn_lit[i] == lit &&
            game_drawn_present[i] == slot->present &&
            lv_color_eq(game_drawn_colour[i], colour)) {
            continue;
        }

        lv_obj_set_style_text_color(game_dbm_label[i], colour, LV_PART_MAIN);
        lv_obj_set_style_text_color(game_name_label[i],
                                    slot->present ? COL_PRIMARY : COL_GAME_TRACK, LV_PART_MAIN);

        for (uint8_t seg = 0; seg < GAME_SEGMENTS; seg++) {
            lv_obj_set_style_bg_color(game_seg[i][seg], seg < lit ? colour : COL_GAME_TRACK,
                                      LV_PART_MAIN);
        }

        game_drawn_lit[i] = lit;
        game_drawn_colour[i] = colour;
        game_drawn_present[i] = slot->present;
        game_drawn_valid[i] = true;
    }
}

static void redraw_fn(struct k_work *work) {
    ARG_UNUSED(work);

    k_mutex_lock(&state_mutex, K_FOREVER);
    struct screen_state s = state;
    k_mutex_unlock(&state_mutex);

    if (name_label == NULL || game_root == NULL) {
        return;
    }

    /* Only the visible tree is updated. Writing to a hidden one would dirty
     * objects the renderer then skips, and LVGL would still carry the
     * invalidated areas through the next refresh. */
    if (s.gaming) {
        lv_obj_add_flag(base_root, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(game_root, LV_OBJ_FLAG_HIDDEN);
        redraw_gaming(&s);
    } else {
        lv_obj_add_flag(game_root, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(base_root, LV_OBJ_FLAG_HIDDEN);
        redraw_base(&s);
    }
}

static K_WORK_DEFINE(redraw_work, redraw_fn);

static void request_redraw(void) {
    if (zmk_display_is_initialized()) {
        k_work_submit_to_queue(zmk_display_work_q(), &redraw_work);
    }
}

/* ---- poll -------------------------------------------------------------- */

static void poll_fn(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(poll_work, poll_fn);

static void poll_fn(struct k_work *work) {
    ARG_UNUSED(work);

    struct zmk_split_esb_status link;
    zmk_split_esb_get_status(&link);

    k_mutex_lock(&state_mutex, K_FOREVER);

    state.base = current_base();
    state.sub = current_sub();
    state.channel = link.channel;
    state.searching = link.searching;

    /* Keyed off the gaming layer directly rather than off current_base(), which
     * reports the mouse layer in preference to it. Tapping the mouse layer in
     * the middle of a match should not throw the meters away and put the
     * battery rings back for as long as it is held. */
    state.gaming = zmk_keymap_layer_active(LAYER_GAMING);

    for (uint8_t i = 0; i < SLOT_COUNT; i++) {
        uint8_t battery = zmk_split_esb_peer_battery(i);
        int8_t rssi = zmk_split_esb_peer_rssi_dbm(i);

        /* The edge is authoritative once we have one. Until then, cached data
         * existing at all is evidence the pipe has been heard -- that only
         * covers the window before the first sweep, 500 ms. */
        bool present = edge_seen[i] ? edge_connected[i]
                                    : ((battery != BATTERY_UNKNOWN) || (rssi != 0));

        /* Drop the cached values when the peripheral is gone. Without this the
         * rings keep drawing the last battery and signal indefinitely, which is
         * worse than showing nothing: it reports a link that is not there. */
        if (!present) {
            battery = BATTERY_UNKNOWN;
            rssi = 0;
        }

        state.slots[i].present = present;
        state.slots[i].battery = battery;
        state.slots[i].rssi = rssi;
    }

    k_mutex_unlock(&state_mutex);

    request_redraw();

    k_work_reschedule(&poll_work, K_MSEC(CONFIG_ROBA_PROSPECTOR_SCREEN_POLL_MS));
}

/* ---- build ------------------------------------------------------------- */

static lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t colour,
                            int16_t x, int16_t y, const char *text) {
    lv_obj_t *label = lv_label_create(parent);

    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, colour, LV_PART_MAIN);
    lv_obj_set_pos(label, x, y);
    lv_label_set_text(label, text);

    return label;
}

/* Centred within one ring's column, so a one-digit and a three-digit reading
 * sit on the same axis instead of drifting. */
static lv_obj_t *make_centred(lv_obj_t *parent, const lv_font_t *font, lv_color_t colour,
                              uint8_t slot, int16_t y, const char *text) {
    lv_obj_t *label = make_label(parent, font, colour, 0, 0, text);

    lv_obj_set_width(label, RING_SIZE);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_pos(label, ring_x(slot), y);

    return label;
}

static lv_obj_t *make_ring(lv_obj_t *parent, int16_t x, int16_t y, int16_t size, int16_t width,
                           lv_color_t indicator) {
    lv_obj_t *arc = lv_arc_create(parent);

    lv_obj_remove_style_all(arc);
    lv_obj_set_size(arc, size, size);
    lv_obj_set_pos(arc, x, y);
    lv_arc_set_range(arc, 0, 100);
    lv_arc_set_bg_angles(arc, 0, 360);
    lv_arc_set_rotation(arc, 270);
    lv_arc_set_value(arc, 0);
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(arc, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(arc, 0, LV_PART_MAIN);

    /* Track always drawn. The unfilled remainder is what makes a partial ring
     * legible as "some of a whole" rather than an arbitrary arc. */
    lv_obj_set_style_arc_width(arc, width, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, COL_TRACK, LV_PART_MAIN);

    lv_obj_set_style_arc_width(arc, width, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, indicator, LV_PART_INDICATOR);

    /* Rounded caps on both parts. Square ends are the tell that a ring was not
     * drawn by Apple. */
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);

    return arc;
}

/* A transparent full-screen container, so one flag hides a whole tree. */
static lv_obj_t *make_root(lv_obj_t *screen) {
    lv_obj_t *root = lv_obj_create(screen);

    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, SCREEN_W, SCREEN_H);
    lv_obj_set_pos(root, 0, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    return root;
}

/* One rounded block of the meter. Square ends on a twelve-up row turn into a
 * picket fence; 2 px of radius is enough to read as a chunk. */
static lv_obj_t *make_segment(lv_obj_t *parent, int16_t x, int16_t y) {
    lv_obj_t *seg = lv_obj_create(parent);

    lv_obj_remove_style_all(seg);
    lv_obj_set_size(seg, GAME_SEG_W, GAME_BAR_H);
    lv_obj_set_pos(seg, x, y);
    lv_obj_clear_flag(seg, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_set_style_bg_color(seg, COL_GAME_TRACK, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(seg, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(seg, 2, LV_PART_MAIN);

    return seg;
}

static void build_base(lv_obj_t *root) {
    /* --- which machine --- */
    logo_label = make_label(root, &os_logos, COL_PRIMARY, LOGO_X, LOGO_Y, LOGO_APPLE);
    name_label = make_label(root, &FR_Regular_36, COL_PRIMARY, NAME_X, NAME_Y, "macOS");
    sub_label = make_label(root, &FG_Medium_21, COL_SECONDARY, NAME_X, SUB_Y, "Base layer");

    /* --- a ring per device --- */
    for (uint8_t i = 0; i < SLOT_COUNT; i++) {
        int x = ring_x(i);

        ring_bat[i] = make_ring(root, x, RING_Y, RING_SIZE, RING_W, COL_GOOD);

        icon_label[i] = make_centred(root, &periph_icons, COL_PRIMARY, i,
                                     RING_Y + ICON_OFFSET, periph_syms[i]);

        /* Track first, lit over the top at the same position. */
        bars_track[i] = make_centred(root, &signal_bars, COL_BARS_TRACK, i, BARS_Y, BARS_TRACK);
        bars_lit[i] = make_centred(root, &signal_bars, COL_SIGNAL, i, BARS_Y, bar_syms[0]);
    }

    /* --- link --- */
    chan_label = make_label(root, &FG_Medium_21, COL_SECONDARY, LOGO_X, FOOTER_Y, "Ch " EN_DASH);

    health_label = make_label(root, &FG_Medium_21, COL_SECONDARY, 0, 0, "Connected");
    lv_obj_set_width(health_label, SCREEN_W - (2 * LOGO_X));
    lv_obj_set_style_text_align(health_label, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    lv_obj_set_pos(health_label, LOGO_X, FOOTER_Y);
}

static void build_gaming(lv_obj_t *root) {
    game_head_label =
        make_label(root, &DINishCondensed_SemiBold_22, COL_GAME_GOOD, GAME_X, GAME_HEAD_Y,
                   game_health_text[HEALTH_OK]);

    game_chan_label = make_label(root, &DINishCondensed_SemiBold_22, COL_SECONDARY, 0, 0, "CH --");
    lv_obj_set_width(game_chan_label, GAME_BAR_W);
    lv_obj_set_style_text_align(game_chan_label, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    lv_obj_set_pos(game_chan_label, GAME_X, GAME_HEAD_Y);

    /* Hairline rule under the header, in the unlit meter colour so the screen
     * has exactly two greys rather than three. */
    lv_obj_t *rule = lv_obj_create(root);
    lv_obj_remove_style_all(rule);
    lv_obj_set_size(rule, GAME_BAR_W, 1);
    lv_obj_set_pos(rule, GAME_X, GAME_RULE_Y);
    lv_obj_clear_flag(rule, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(rule, COL_GAME_TRACK, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(rule, LV_OPA_COVER, LV_PART_MAIN);

    for (uint8_t i = 0; i < SLOT_COUNT; i++) {
        int y = game_row_y(i);

        game_name_label[i] = make_label(root, &DINishCondensed_SemiBold_22, COL_PRIMARY, GAME_X,
                                        y, game_names[i]);

        /* Right-aligned over the meter's own width, so the readings stack in a
         * column and a two-digit value does not shuffle the row. */
        game_dbm_label[i] =
            make_label(root, &DINishCondensed_SemiBold_22, COL_GAME_GOOD, 0, 0, "--");
        lv_obj_set_width(game_dbm_label[i], GAME_BAR_W);
        lv_obj_set_style_text_align(game_dbm_label[i], LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
        lv_obj_set_pos(game_dbm_label[i], GAME_X, y);

        for (uint8_t seg = 0; seg < GAME_SEGMENTS; seg++) {
            game_seg[i][seg] = make_segment(root, game_seg_x(seg), y + GAME_METER_DY);
        }
    }
}

lv_obj_t *zmk_display_status_screen(void) {
    lv_obj_t *screen = lv_obj_create(NULL);

    lv_obj_remove_style_all(screen);
    lv_obj_set_size(screen, SCREEN_W, SCREEN_H);
    lv_obj_set_style_bg_color(screen, COL_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);

    base_root = make_root(screen);
    build_base(base_root);

    game_root = make_root(screen);
    build_gaming(game_root);

    /* Gaming starts hidden; the first poll decides within 250 ms. */
    lv_obj_add_flag(game_root, LV_OBJ_FLAG_HIDDEN);

    k_work_reschedule(&poll_work, K_MSEC(CONFIG_ROBA_PROSPECTOR_SCREEN_POLL_MS));

    return screen;
}
