"""Generate the 256x128 banner PNG for The Cog's HOME menu entry.

The banner is what the 3DS HOME menu shows in the top screen when you
highlight the app's icon. 256x128, gold gear on dark background with
"The Cog" text next to it.
"""
from PIL import Image, ImageDraw, ImageFont
import math

W, H = 256, 128
BG = (13, 13, 13, 255)        # #0d0d0d
GOLD = (245, 215, 110, 255)   # #f5d76e

img = Image.new('RGBA', (W, H), BG)
draw = ImageDraw.Draw(img)

# Gear on the left, centered vertically
cx, cy = 56, H / 2

TEETH = 8
TOOTH_OUTER = 40
TOOTH_INNER = 30
TOOTH_HALF_ANGLE = math.radians(11)

for i in range(TEETH):
    a = math.radians(i * (360 / TEETH)) - math.pi / 2
    x1 = cx + TOOTH_OUTER * math.cos(a - TOOTH_HALF_ANGLE)
    y1 = cy + TOOTH_OUTER * math.sin(a - TOOTH_HALF_ANGLE)
    x2 = cx + TOOTH_OUTER * math.cos(a + TOOTH_HALF_ANGLE)
    y2 = cy + TOOTH_OUTER * math.sin(a + TOOTH_HALF_ANGLE)
    base_half = TOOTH_HALF_ANGLE * 1.4
    x3 = cx + TOOTH_INNER * math.cos(a + base_half)
    y3 = cy + TOOTH_INNER * math.sin(a + base_half)
    x4 = cx + TOOTH_INNER * math.cos(a - base_half)
    y4 = cy + TOOTH_INNER * math.sin(a - base_half)
    draw.polygon([(x1, y1), (x2, y2), (x3, y3), (x4, y4)], fill=GOLD)

draw.ellipse((cx - TOOTH_INNER, cy - TOOTH_INNER, cx + TOOTH_INNER, cy + TOOTH_INNER), fill=GOLD)
draw.ellipse((cx - 12, cy - 12, cx + 12, cy + 12), fill=BG)

# Text on the right
try:
    # Try a common Windows font; fallback to default if missing
    font_large = ImageFont.truetype("C:/Windows/Fonts/segoeuib.ttf", 32)
    font_small = ImageFont.truetype("C:/Windows/Fonts/segoeui.ttf", 14)
except (OSError, IOError):
    font_large = ImageFont.load_default()
    font_small = ImageFont.load_default()

draw.text((120, 40), "The Cog", fill=GOLD, font=font_large)
draw.text((120, 78), "Agent orchestration", fill=(180, 180, 180, 255), font=font_small)
draw.text((120, 96), "theCog.dev", fill=(120, 120, 120, 255), font=font_small)

img.save('meta/banner.png', 'PNG')
print(f'Wrote meta/banner.png ({W}x{H})')
