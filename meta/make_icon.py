"""Generate the 48x48 cog icon for The Cog 3DS Homebrew Launcher entry.

48x48 is the 3DS SMDH icon size. Golden gear (#f5d76e) on dark background
(#0d0d0d) — matches the brand. Run this whenever the icon needs regenerating;
the output is meta/icon.png.
"""
from PIL import Image, ImageDraw
import math

SIZE = 48
BG = (13, 13, 13, 255)        # #0d0d0d
GOLD = (245, 215, 110, 255)   # #f5d76e — Sunshine theme accent

img = Image.new('RGBA', (SIZE, SIZE), BG)
draw = ImageDraw.Draw(img)

cx, cy = SIZE / 2, SIZE / 2

# Outer gear teeth — 8 trapezoidal teeth around an outer ring.
# We approximate teeth as polygons radiating from the center.
TEETH = 8
TOOTH_OUTER = 22   # outer radius (tooth tip)
TOOTH_INNER = 17   # inner radius (gear body radius)
TOOTH_HALF_ANGLE = math.radians(11)  # half-angle of each tooth

for i in range(TEETH):
    a = math.radians(i * (360 / TEETH)) - math.pi / 2  # start at top
    # Tooth tips (outer)
    x1 = cx + TOOTH_OUTER * math.cos(a - TOOTH_HALF_ANGLE)
    y1 = cy + TOOTH_OUTER * math.sin(a - TOOTH_HALF_ANGLE)
    x2 = cx + TOOTH_OUTER * math.cos(a + TOOTH_HALF_ANGLE)
    y2 = cy + TOOTH_OUTER * math.sin(a + TOOTH_HALF_ANGLE)
    # Tooth base (inner)
    base_half = TOOTH_HALF_ANGLE * 1.4
    x3 = cx + TOOTH_INNER * math.cos(a + base_half)
    y3 = cy + TOOTH_INNER * math.sin(a + base_half)
    x4 = cx + TOOTH_INNER * math.cos(a - base_half)
    y4 = cy + TOOTH_INNER * math.sin(a - base_half)
    draw.polygon([(x1, y1), (x2, y2), (x3, y3), (x4, y4)], fill=GOLD)

# Gear body (filled circle in gold)
draw.ellipse((cx - TOOTH_INNER, cy - TOOTH_INNER, cx + TOOTH_INNER, cy + TOOTH_INNER), fill=GOLD)

# Inner hole (back to background) — gives the gear its hollow center look
HOLE_RADIUS = 7
draw.ellipse((cx - HOLE_RADIUS, cy - HOLE_RADIUS, cx + HOLE_RADIUS, cy + HOLE_RADIUS), fill=BG)

img.save('meta/icon.png', 'PNG')
print('Wrote meta/icon.png (48x48)')
