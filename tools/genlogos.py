#!/usr/bin/env python3
"""Generate 1bpp logo glyphs for the roBaesb dongle OLED (src/os_logos.c).

Each logo is drawn on its own canvas (different aspect ratios) but padded to a
common height so they share one font line. Run:  python3 tools/genlogos.py
Prints an ASCII preview and the C byte arrays."""
import math

H = 44  # common glyph height (line_height)

def blank(w):
    return [[0]*w for _ in range(H)]

def disk(g, cx, cy, r, val=1):
    for y in range(len(g)):
        for x in range(len(g[0])):
            if (x-cx)**2 + (y-cy)**2 <= r*r:
                g[y][x] = val

def ellipse(g, cx, cy, rx, ry, val=1):
    for y in range(len(g)):
        for x in range(len(g[0])):
            if ((x-cx)/rx)**2 + ((y-cy)/ry)**2 <= 1.0:
                g[y][x] = val

def ellipse_rot(g, cx, cy, rx, ry, ang, val=1):
    ca, sa = math.cos(ang), math.sin(ang)
    for y in range(len(g)):
        for x in range(len(g[0])):
            dx, dy = x-cx, y-cy
            u =  dx*ca + dy*sa
            v = -dx*sa + dy*ca
            if (u/rx)**2 + (v/ry)**2 <= 1.0:
                g[y][x] = val

def rect(g, x0, y0, x1, y1, val=1):
    for y in range(max(0,y0), min(len(g),y1+1)):
        for x in range(max(0,x0), min(len(g[0]),x1+1)):
            g[y][x] = val

def border(g, t):
    w, h = len(g[0]), len(g)
    rect(g, 0, 0, w-1, t-1)
    rect(g, 0, h-t, w-1, h-1)
    rect(g, 0, 0, t-1, h-1)
    rect(g, w-t, 0, w-1, h-1)

def despeckle(g):
    """Drop pixels with fewer than two orthogonal neighbours.

    A rasterised disk ends in a single-pixel tip. On a 1bpp glyph that reads as
    a speck of dirt floating off the mark rather than part of it, so trim any
    pixel that is only attached by one edge. Safe on shapes whose real features
    are several pixels thick; only applied to the gamepad, which is built from
    overlapping disks."""
    h, w = len(g), len(g[0])
    doomed = []
    for y in range(h):
        for x in range(w):
            if not g[y][x]:
                continue
            neighbours = 0
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                nx, ny = x + dx, y + dy
                if 0 <= nx < w and 0 <= ny < h and g[ny][nx]:
                    neighbours += 1
            if neighbours <= 1:
                doomed.append((x, y))
    for x, y in doomed:
        g[y][x] = 0
    return g

def from_rows(rows):
    return [[1 if c == '#' else 0 for c in row] for row in rows]

# ---------------- Apple (40x44) ----------------
AW = 40
apple = from_rows([
    "........................................",
    ".........................######.........",
    ".......................#######..........",
    ".......................#######..........",
    ".....................########...........",
    ".....................########...........",
    "....................########............",
    "....................########............",
    "....................#######.............",
    "....................#######.............",
    "....................####................",
    ".......############.###############.....",
    "......#############...#############.....",
    ".....################################...",
    ".....################################...",
    "...###################################..",
    "..####################################..",
    "..#################################.....",
    ".##################################.....",
    ".#################################......",
    ".#################################......",
    ".#################################......",
    ".#################################......",
    ".#################################......",
    ".#################################......",
    ".#################################......",
    ".#################################......",
    ".#################################......",
    ".#################################......",
    ".###################################....",
    ".######################################.",
    "..#####################################.",
    "..#####################################.",
    "...####################################.",
    "....##################################..",
    "....##################################..",
    ".....################################...",
    ".....################################...",
    ".....##############################.....",
    "......#############################.....",
    ".......###########################......",
    ".........#########.....##########.......",
    ".........#########.....##########.......",
    "........................................",
])

# ---------------- Windows (40x44, 4-pane) ----------------
WW = 40
win = blank(WW)
pad = 2
rect(win, 0, pad, 17, pad+17)
rect(win, 22, pad, 39, pad+17)
rect(win, 0, pad+22, 17, pad+39)
rect(win, 22, pad+22, 39, pad+39)

# ---------------- Gamepad, square like the others (40x44) ----------------
# The old PUBG wordmark was 64x44 -- 60% wider than Apple and Windows at 40x44,
# so it overflowed the logo slot on the Prospector screen and shoved the base
# name sideways. A gamepad silhouette reads as "gaming" at a glance and fits
# the same square-ish box the other two use.
GAW = 40
gaming = blank(GAW)

# Body: a capsule with flared grips, so the outline is unmistakably a pad
# rather than a generic blob.
rect(gaming, 10, 15, 29, 30)
disk(gaming, 12, 22, 9)
disk(gaming, 27, 22, 9)
disk(gaming, 8, 28, 6)
disk(gaming, 31, 28, 6)

# Punch the controls out of the solid body. Apple and Windows are solid marks
# too, so cut-outs keep the family consistent at 1bpp.
rect(gaming, 7, 20, 16, 22, 0)    # d-pad, horizontal arm
rect(gaming, 10, 17, 13, 25, 0)   # d-pad, vertical arm
disk(gaming, 26, 19, 2.5, 0)      # action button, upper
disk(gaming, 30, 24, 2.5, 0)      # action button, lower

despeckle(gaming)

# ---------------- Mouse cursor / arrow pointer (32x44, outline) ----------------
def poly_fill(g, pts, val=1):
    h, w = len(g), len(g[0])
    for y in range(h):
        xs = []
        n = len(pts)
        for i in range(n):
            x1, y1 = pts[i]
            x2, y2 = pts[(i + 1) % n]
            if (y1 <= y < y2) or (y2 <= y < y1):
                t = (y - y1) / (y2 - y1)
                xs.append(x1 + t * (x2 - x1))
        xs.sort()
        for j in range(0, len(xs) - 1, 2):
            for x in range(int(math.ceil(xs[j])), int(math.floor(xs[j + 1])) + 1):
                if 0 <= x < w:
                    g[y][x] = val

def thick_line(g, p0, p1, t=1):
    """Stroke a line from p0 to p1 with a round nib of radius t (width ~2t+1)."""
    h, w = len(g), len(g[0])
    (x0, y0), (x1, y1) = p0, p1
    steps = int(max(abs(x1 - x0), abs(y1 - y0))) * 4 + 1
    for i in range(steps + 1):
        f = i / steps
        cx, cy = x0 + (x1 - x0) * f, y0 + (y1 - y0) * f
        for dy in range(-t, t + 1):
            for dx in range(-t, t + 1):
                if dx * dx + dy * dy <= t * t + t:
                    xx, yy = int(round(cx)) + dx, int(round(cy)) + dy
                    if 0 <= yy < h and 0 <= xx < w:
                        g[yy][xx] = 1

def poly_outline(g, pts, t=1):
    n = len(pts)
    for i in range(n):
        thick_line(g, pts[i], pts[(i + 1) % n], t)

MWID = 32
# Classic arrow cursor: tip at top-left, foot pointing down-right. Stroked as a
# hollow outline (matches a system pointer icon).
cursor_pts = [
    (5, 3),    # tip
    (5, 33),   # left edge straight down
    (13, 27),  # inner notch, left of the foot
    (20, 41),  # foot tip (bottom)
    (25, 38),  # foot, right side
    (18, 25),  # inner notch, right of the foot
    (29, 22),  # right wing tip
]
mouse = blank(MWID)
poly_outline(mouse, cursor_pts, t=1)

glyphs = [("Apple", apple), ("Windows", win), ("Gaming", gaming), ("Mouse", mouse)]

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

def preview(name, g):
    print(f"--- {name} {len(g[0])}x{len(g)} ---")
    for row in g:
        print(''.join('#' if c else '.' for c in row))
    print()

for nm, g in glyphs:
    preview(nm, g)

print("=== C ARRAYS ===")
idx = 0
for nm, g in glyphs:
    bs = to_bytes(g)
    print(f"    /* {nm}  {len(g[0])}x{len(g)}  index {idx} */")
    for i in range(0, len(bs), 8):
        print('    ' + ' '.join(f'0x{v:02X},' for v in bs[i:i+8]))
    idx += len(bs)
print(f"    /* total bytes: {idx} */")
print("=== GLYPH DSC ===")
idx = 0
for nm, g in glyphs:
    print(f"    {nm}: index={idx} box_w={len(g[0])} box_h={len(g)}")
    idx += len(to_bytes(g))

# ---------------- emit src/os_logos.c ----------------
# Written directly rather than printed for hand-pasting: the glyph_dsc indices
# are a running total of bitmap sizes, so changing one glyph's width shifts
# every later index. Doing that by hand is how you get a font that renders
# garbage.
import os

CODEPOINTS = [(0xE010, "Apple", "default (Mac) base layer"),
              (0xE011, "Windows", "Windows base layer (4-pane)"),
              (0xE012, "Gaming", "gaming base layer (gamepad)"),
              (0xE013, "Mouse", "mouse layer active (arrow cursor outline)")]

out = []
out.append("#include <lvgl.h>\n")
out.append("""
/*
 * Hand-generated 1bpp OS / game logos. Drawn geometrically by
 * tools/genlogos.py and mapped to the Unicode Private Use Area so they embed
 * in lv_label strings. DO NOT EDIT BY HAND -- re-run the generator:
 *
 *     python3 tools/genlogos.py
 *
 * It rewrites this file, including the running glyph_dsc indices that shift
 * whenever any glyph's width changes.
 *
 * All glyphs share a 44 px line height; widths differ per logo.
 *""")
idx = 0
for (cp, nm, note), (_, g) in zip(CODEPOINTS, glyphs):
    utf8 = chr(cp).encode("utf-8")
    esc = "".join(f"\\x{b:02X}" for b in utf8)
    out.append(f' *   U+{cp:04X} "{esc}"  {nm:<8}{len(g[0])}x{len(g)} - {note}')
out.append(" */\n")

out.append("static const uint8_t os_logos_bitmap[] = {")
idx = 0
for nm, g in glyphs:
    bs = to_bytes(g)
    out.append(f"    /* {nm}  {len(g[0])}x{len(g)}  index {idx} */")
    for i in range(0, len(bs), 8):
        out.append("    " + " ".join(f"0x{v:02X}," for v in bs[i:i+8]))
    idx += len(bs)
out.append("};\n")

out.append('/* glyph 0 is the "not found" sentinel required by LVGL */')
out.append("static const lv_font_fmt_txt_glyph_dsc_t os_logos_glyph_dsc[] = {")
out.append("    {.bitmap_index = 0,   .adv_w = 0,    .box_w = 0,  .box_h = 0,  .ofs_x = 0, .ofs_y = 0},")
idx = 0
for nm, g in glyphs:
    w = len(g[0])
    out.append(f"    {{.bitmap_index = {idx},   .adv_w = {w * 16},  "
               f".box_w = {w}, .box_h = {len(g)}, .ofs_x = 0, .ofs_y = 0}},")
    idx += len(to_bytes(g))
out.append("};\n")

out.append("/* FORMAT0_TINY: contiguous range, glyph_id = glyph_id_start + (cp - range_start) */")
out.append("""static const lv_font_fmt_txt_cmap_t os_logos_cmaps[] = {
    {
        .range_start       = 0xE010,
        .range_length      = %d,
        .glyph_id_start    = 1,
        .unicode_list      = NULL,
        .glyph_id_ofs_list = NULL,
        .list_length       = 0,
        .type              = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY,
    },
};
""" % len(glyphs))

out.append("""static const lv_font_fmt_txt_dsc_t os_logos_dsc = {
    .glyph_bitmap  = os_logos_bitmap,
    .glyph_dsc     = os_logos_glyph_dsc,
    .cmaps         = os_logos_cmaps,
    .kern_dsc      = NULL,
    .kern_scale    = 0,
    .cmap_num      = 1,
    .bpp           = 1,
    .kern_classes  = 0,
    .bitmap_format = 0,
};

const lv_font_t os_logos = {
    .get_glyph_dsc       = lv_font_get_glyph_dsc_fmt_txt,
    .get_glyph_bitmap    = lv_font_get_bitmap_fmt_txt,
    .line_height         = %d,
    .base_line           = 0,
    .subpx               = LV_FONT_SUBPX_NONE,
    .underline_position  = -1,
    .underline_thickness = 1,
    .dsc                 = &os_logos_dsc,
    .fallback            = NULL,
};""" % H)

dest = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "src", "os_logos.c")
with open(dest, "w") as fh:
    fh.write("\n".join(out) + "\n")
print(f"wrote {dest}")
