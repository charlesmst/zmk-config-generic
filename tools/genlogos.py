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

# ---------------- PUBG wordmark in a rough box (64x44) ----------------
PW = 64
pubg = blank(PW)
border(pubg, 3)
# little brush gaps on the border (rough look)
rect(pubg, 0, 19, 3, 25, 0)
rect(pubg, PW-4, 19, PW-1, 25, 0)
rect(pubg, 28, 0, 36, 3, 0)
rect(pubg, 28, H-4, 36, H-1, 0)

def letter(g, ch, x, y, w=10, h=18, s=3):
    x1, y1 = x+w-1, y+h-1
    midy0, midy1 = y + h//2 - s//2, y + h//2 - s//2 + s - 1
    if ch == 'P':
        rect(g, x, y, x+s-1, y1)
        rect(g, x, y, x1, y+s-1)
        rect(g, x, midy0, x1, midy1)
        rect(g, x1-s+1, y, x1, midy1)
    elif ch == 'U':
        rect(g, x, y, x+s-1, y1-s+1)
        rect(g, x1-s+1, y, x1, y1-s+1)
        rect(g, x, y1-s+1, x1, y1)
    elif ch == 'B':
        rect(g, x, y, x+s-1, y1)
        rect(g, x, y, x1, y+s-1)
        rect(g, x, midy0, x1, midy1)
        rect(g, x, y1-s+1, x1, y1)
        rect(g, x1-s+1, y, x1, midy1)
        rect(g, x1-s+1, midy0, x1, y1)
    elif ch == 'G':
        rect(g, x, y, x+s-1, y1)
        rect(g, x, y, x1, y+s-1)
        rect(g, x, y1-s+1, x1, y1)
        rect(g, x1-s+1, midy0, x1, y1)
        rect(g, x+w//2, midy0, x1, midy1)

lx = 8
for ch in "PUBG":
    letter(pubg, ch, lx, 13, w=10, h=18, s=3)
    lx += 12

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

glyphs = [("Apple", apple), ("Windows", win), ("PUBG", pubg), ("Mouse", mouse)]

def to_bytes(g):
    w = len(g[0])
    stride = (w + 7) // 8
    out = []
    for row in g:
        for bx in range(stride):
            byte = 0
            for b in range(8):
                x = bx*8 + b
                if x < w and row[x]:
                    byte |= (1 << (7-b))
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
