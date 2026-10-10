#!/usr/bin/env python3
"""Generate 1bpp peripheral icons for the Prospector screen (src/periph_icons.c).

One per ESB peripheral, drawn to sit inside the battery ring in place of a
number. dongle_icons.c has equivalents at 8x10, sized for a 128x64 OLED; these
are 34x34 for the Prospector's 1.69" panel.

All three are outlined on a 2 px stroke so they read as one set. The halves are
the same mark mirrored and are told apart by a chevron on the inner edge, both
pointing at the gap between the halves. The earlier cut used a solid body with
a thumb cluster for that cue, and on the panel the two were near enough
identical -- a mirrored blob only tells you which half it is if you already
know which corner to look at.

  U+E030  left half
  U+E031  right half
  U+E032  lariska (trackball mouse)

Run:  python3 tools/genperiphicons.py
"""
import os

W = 34
H = 34


def blank():
    return [[0] * W for _ in range(H)]


def rect(g, x0, y0, x1, y1, val=1):
    for y in range(max(0, y0), min(H, y1 + 1)):
        for x in range(max(0, x0), min(W, x1 + 1)):
            g[y][x] = val


def rrect(g, x0, y0, x1, y1, r=2, val=1):
    """Rounded rectangle: fill, then nick the corners."""
    rect(g, x0, y0, x1, y1, val)
    for i in range(r):
        for j in range(r - i):
            g[y0 + i][x0 + j] = 0 if val else g[y0 + i][x0 + j]
            g[y0 + i][x1 - j] = 0 if val else g[y0 + i][x1 - j]
            g[y1 - i][x0 + j] = 0 if val else g[y1 - i][x0 + j]
            g[y1 - i][x1 - j] = 0 if val else g[y1 - i][x1 - j]


def ellipse(g, cx, cy, rx, ry, val=1):
    for y in range(H):
        for x in range(W):
            if ((x - cx) / rx) ** 2 + ((y - cy) / ry) ** 2 <= 1.0:
                g[y][x] = val


def frame(g, x0, y0, x1, y1, t=2):
    """Hollow rectangle: fill, then clear the interior t pixels in."""
    rect(g, x0, y0, x1, y1, 1)
    rect(g, x0 + t, y0 + t, x1 - t, y1 - t, 0)


def _in_rrect(x, y, x0, y0, x1, y1, r):
    dx = max(x0 + r - x, 0, x - (x1 - r))
    dy = max(y0 + r - y, 0, y - (y1 - r))
    return dx * dx + dy * dy <= r * r


def rrect_outline(g, x0, y0, x1, y1, r, t=2):
    """Stroked rounded rectangle, constant t px all the way round.

    Not ellipse-minus-smaller-ellipse: two concentric ellipses do not stay a
    fixed distance apart, so that approach gives a stroke that is 1 px at the
    poles and 3 px at the flanks, and at 34 px the lumpiness is obvious. A
    rounded-rect distance field is uniform by construction.
    """
    for y in range(H):
        for x in range(W):
            if _in_rrect(x, y, x0, y0, x1, y1, r) and not _in_rrect(
                x, y, x0 + t, y0 + t, x1 - t, y1 - t, max(r - t, 0)
            ):
                g[y][x] = 1


def mirror(g):
    return [list(reversed(row)) for row in g]


# ---------------- keyboard half ----------------
# An outlined case with punched keys, plus a solid chevron on the inner edge.
#
# The previous cut was a solid body with a thumb cluster hanging off it, and on
# the panel the two halves were nearly indistinguishable -- a mirrored blob is
# only legible if you already know to look at which corner droops. The chevron
# is an unambiguous direction cue at a glance, and both chevrons point at the
# gap between the halves, which is where the halves actually sit on the desk.
#
# Outlined rather than solid: at 34 px a 2 px stroke holds, and the hollow case
# gives the key grid something to read against. Solid bodies with cut-out keys
# turned into a grey smear at arm's length.
def half():
    g = blank()

    # Case: 2 px stroke, interior x3..22 / y9..23.
    frame(g, 1, 7, 24, 25, t=2)

    # Four columns of 3 px keys, two rows, then a wide bottom key. The real
    # board has more rows than this; at 34 px they merge, so this is the
    # suggestion of a key grid rather than a map of one. The last column ends
    # at x21 against an interior edge of x22, so the grid never fuses with the
    # case wall -- at 1bpp a touching key and wall read as one thick bar.
    for col in range(4):
        x = 4 + col * 5
        rect(g, x, 11, x + 2, 13)
        rect(g, x, 15, x + 2, 17)
    rect(g, 7, 19, 18, 21)

    # Chevron, apex right, 6 px deep, leaving 1 px of margin at either end of
    # the 34 px box. Drawn as a filled triangle rather than a stroked arrow so
    # it keeps its point at this size.
    for i in range(6):
        x = 26 + i
        h = 5 - i
        rect(g, x, 16 - h, x, 16 + h)

    return g


# ---------------- lariska: trackball mouse ----------------
def mouse():
    g = blank()

    # Outlined shell on the keyboards' 2 px stroke, so the three icons read as
    # one set. The old cut was a solid ellipse, which sat next to two outlined
    # keyboards looking like a mark from a different family.
    # r=8 against a 19 px width, not r=9: at r=9 the cap is a full semicircle
    # and the apex discretises to a single lit pixel sitting above a 9 px row,
    # which reads as dirt on the panel rather than as the top of a curve.
    rrect_outline(g, 8, 3, 26, 30, r=8, t=2)

    # Scroll wheel floating inside the shell, not slotted through it: cutting
    # the wheel through the outline severed the shell into two arcs and the
    # mark stopped reading as a mouse.
    rect(g, 16, 9, 18, 16)

    return g


left = half()
right = mirror(half())
lariska = mouse()

glyphs = [("left", left), ("right", right), ("mouse", lariska)]


def to_bytes(g):
    """Pack 1bpp rows continuously, with no per-row byte alignment.

    This is what LVGL's font format expects: each glyph is a single bit stream
    of box_w * box_h pixels, padded only at the very end to a byte boundary.
    Padding every *row* instead works by accident whenever the width is a
    multiple of 8 and shears the glyph into diagonal stripes when it is not --
    which is exactly what a 34 px icon rendered as.
    """
    w = len(g[0])
    bits = []
    for row in g:
        bits.extend(row[:w])
    out = []
    for i in range(0, len(bits), 8):
        byte = 0
        for b, v in enumerate(bits[i : i + 8]):
            if v:
                byte |= 1 << (7 - b)
        out.append(byte)
    return out


for name, g in glyphs:
    print(f"--- {name} {W}x{H} ---")
    for row in g:
        print("".join("#" if c else "." for c in row))
    print()

out = ["#include <lvgl.h>\n"]
out.append("""
/*
 * 1bpp peripheral icons, generated by tools/genperiphicons.py.
 * DO NOT EDIT BY HAND -- re-run the generator:
 *
 *     python3 tools/genperiphicons.py
 *
 * It rewrites this file, including the running glyph_dsc indices.
 *
 * Drawn to sit inside the battery ring in place of a percentage. Outlined on a
 * 2 px stroke; the halves are one mark mirrored, told apart by a chevron on
 * the inner edge.
 *
 *   U+E030 "\\xEE\\x80\\xB0"  left half
 *   U+E031 "\\xEE\\x80\\xB1"  right half
 *   U+E032 "\\xEE\\x80\\xB2"  lariska (trackball mouse)
 */""")

out.append("static const uint8_t periph_icons_bitmap[] = {")
idx = 0
for name, g in glyphs:
    bs = to_bytes(g)
    out.append(f"    /* {name}  {W}x{H}  index {idx} */")
    for i in range(0, len(bs), 8):
        out.append("    " + " ".join(f"0x{v:02X}," for v in bs[i : i + 8]))
    idx += len(bs)
out.append("};\n")

out.append('/* glyph 0 is the "not found" sentinel required by LVGL */')
out.append("static const lv_font_fmt_txt_glyph_dsc_t periph_icons_glyph_dsc[] = {")
out.append("    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0},")
idx = 0
for name, g in glyphs:
    out.append(
        f"    {{.bitmap_index = {idx}, .adv_w = {W * 16}, "
        f".box_w = {W}, .box_h = {H}, .ofs_x = 0, .ofs_y = 0}},"
    )
    idx += len(to_bytes(g))
out.append("};\n")

out.append("/* FORMAT0_TINY: contiguous range, glyph_id = glyph_id_start + (cp - range_start) */")
out.append(
    """static const lv_font_fmt_txt_cmap_t periph_icons_cmaps[] = {
    {
        .range_start       = 0xE030,
        .range_length      = %d,
        .glyph_id_start    = 1,
        .unicode_list      = NULL,
        .glyph_id_ofs_list = NULL,
        .list_length       = 0,
        .type              = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY,
    },
};
"""
    % len(glyphs)
)

out.append(
    """static const lv_font_fmt_txt_dsc_t periph_icons_dsc = {
    .glyph_bitmap  = periph_icons_bitmap,
    .glyph_dsc     = periph_icons_glyph_dsc,
    .cmaps         = periph_icons_cmaps,
    .kern_dsc      = NULL,
    .kern_scale    = 0,
    .cmap_num      = 1,
    .bpp           = 1,
    .kern_classes  = 0,
    .bitmap_format = 0,
};

const lv_font_t periph_icons = {
    .get_glyph_dsc       = lv_font_get_glyph_dsc_fmt_txt,
    .get_glyph_bitmap    = lv_font_get_bitmap_fmt_txt,
    .line_height         = %d,
    .base_line           = 0,
    .subpx               = LV_FONT_SUBPX_NONE,
    .underline_position  = -1,
    .underline_thickness = 1,
    .dsc                 = &periph_icons_dsc,
    .fallback            = NULL,
};"""
    % H
)

dest = os.path.join(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "src", "periph_icons.c"
)
with open(dest, "w") as fh:
    fh.write("\n".join(out) + "\n")
print(f"wrote {dest}")
