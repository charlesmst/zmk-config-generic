#include <lvgl.h>

LV_FONT_DECLARE(lv_font_montserrat_12);

/*
 * Hand-drawn 8×10 px, 1bpp device icons for the roBaesb dongle OLED.
 * Mapped to Unicode Private Use Area U+E000–U+E003 so they can be
 * embedded directly in lv_label text strings alongside normal glyphs.
 *
 * The font chains to lv_font_montserrat_12 as a fallback, so ASCII text
 * and the LVGL symbols (battery, wifi, etc.) render in the same font as
 * the rest of the status screen.
 *
 * UTF-8 macros (defined in dongle_battery_display.c):
 *   U+E000 "\xEE\x80\x80"  dongle  – chip body + USB-A plug below
 *   U+E001 "\xEE\x80\x81"  left KB – keyboard grid + left arrow ←
 *   U+E002 "\xEE\x80\x82"  right KB – keyboard grid + right arrow →
 *   U+E003 "\xEE\x80\x83"  mouse   – corded top-down mouse + scroll wheel
 *
 * Pixel layout (col 0 = MSB = leftmost):
 *
 *  Dongle           Left KB          Right KB         Mouse
 *  ........         ########         ########         ...##...
 *  ########         #.#.#.#.         #.#.#.#.         ....#...
 *  #......#         ########         ########         ..####..
 *  #.####.#         #.#.#.#.         #.#.#.#.         .######.
 *  #.####.#         ########         ########         .##.###.
 *  #......#         ########         ########         .##.###.
 *  ########         ........         ........         .######.
 *  ..####..         .#......         ......#.         .######.
 *  ..####..         .######.         .######.         ..####..
 *  ........         .#......         ......#.         ........
 */
static const uint8_t dongle_icons_bitmap[] = {
    /* U+E000  Dongle */
    0x00, 0xFF, 0x81, 0xBD, 0xBD, 0x81, 0xFF, 0x3C, 0x3C, 0x00,
    /* U+E001  Left keyboard */
    0xFF, 0xAA, 0xFF, 0xAA, 0xFF, 0xFF, 0x00, 0x40, 0x7E, 0x40,
    /* U+E002  Right keyboard */
    0xFF, 0xAA, 0xFF, 0xAA, 0xFF, 0xFF, 0x00, 0x02, 0x7E, 0x02,
    /* U+E003  Mouse */
    0x18, 0x08, 0x3C, 0x7E, 0x6E, 0x6E, 0x7E, 0x7E, 0x3C, 0x00,
};

/* glyph 0 is the "not found" sentinel required by LVGL */
static const lv_font_fmt_txt_glyph_dsc_t dongle_icons_glyph_dsc[] = {
    {.bitmap_index = 0,  .adv_w = 0,   .box_w = 0, .box_h = 0,  .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 0,  .adv_w = 144, .box_w = 8, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 10, .adv_w = 144, .box_w = 8, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 20, .adv_w = 144, .box_w = 8, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 30, .adv_w = 144, .box_w = 8, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
};

/* FORMAT0_TINY: contiguous range, glyph_id = glyph_id_start + (cp - range_start) */
static const lv_font_fmt_txt_cmap_t dongle_icons_cmaps[] = {
    {
        .range_start       = 0xE000,
        .range_length      = 4,
        .glyph_id_start    = 1,
        .unicode_list      = NULL,
        .glyph_id_ofs_list = NULL,
        .list_length       = 0,
        .type              = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY,
    },
};

static const lv_font_fmt_txt_dsc_t dongle_icons_dsc = {
    .glyph_bitmap  = dongle_icons_bitmap,
    .glyph_dsc     = dongle_icons_glyph_dsc,
    .cmaps         = dongle_icons_cmaps,
    .kern_dsc      = NULL,
    .kern_scale    = 0,
    .cmap_num      = 1,
    .bpp           = 1,
    .kern_classes  = 0,
    .bitmap_format = 0,
};

const lv_font_t dongle_icons = {
    .get_glyph_dsc       = lv_font_get_glyph_dsc_fmt_txt,
    .get_glyph_bitmap    = lv_font_get_bitmap_fmt_txt,
    .line_height         = 15,
    .base_line           = 3,
    .subpx               = LV_FONT_SUBPX_NONE,
    .underline_position  = -1,
    .underline_thickness = 1,
    .dsc                 = &dongle_icons_dsc,
    .fallback            = &lv_font_montserrat_12,
};
