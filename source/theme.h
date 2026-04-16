// Color palette + font scales for The Cog 3DS UI. Matches the
// "Sunshine" theme on desktop (gold on dark). All colors are
// 32-bit RGBA little-endian values suitable for C2D_Color32.

#ifndef COG_THEME_H
#define COG_THEME_H

#include <citro2d.h>

// Backgrounds
extern const u32 THEME_BG_DARK;       // #0d0d0d main bg
extern const u32 THEME_BG_CANVAS;     // #141414 canvas bg (slight lift)
extern const u32 THEME_DIVIDER;       // #2a2a2a hairline

// Accents
extern const u32 THEME_GOLD;          // #f5d76e primary accent
extern const u32 THEME_GOLD_DIM;      // #a88e42 dim accent
extern const u32 THEME_LIFT_GLOW;     // #f5d76e @40% alpha

// Text
extern const u32 THEME_TEXT_PRIMARY;  // #f5f5f5
extern const u32 THEME_TEXT_DIMMED;   // #888888

// Status colors (match the desktop agent status palette)
extern const u32 THEME_STATUS_WORKING;      // #f5d76e gold
extern const u32 THEME_STATUS_ACTIVE;       // #6ed76e green
extern const u32 THEME_STATUS_IDLE;         // #888888 grey
extern const u32 THEME_STATUS_DISCONNECTED; // #d76e6e red

// Pick a status color by string name. Defaults to idle.
u32 theme_status_color(const char *status);

// Font scale factors (citro2d scales the built-in system font)
#define THEME_FONT_HEADER   0.75f
#define THEME_FONT_CARD     0.5f
#define THEME_FONT_LABEL    0.45f
#define THEME_FONT_FOOTER   0.4f

#endif
