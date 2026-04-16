#include "theme.h"
#include <string.h>

// C2D_Color32 packs RGBA as 0xAABBGGRR — the byte order looks swapped
// but that's citro2d's native layout. Using C2D_Color32(r,g,b,a) is
// clearest. We preconvert here at static init so render code is cheap.

const u32 THEME_BG_DARK       = 0xff0d0d0d;
const u32 THEME_BG_CANVAS     = 0xff141414;
const u32 THEME_DIVIDER       = 0xff2a2a2a;
const u32 THEME_GOLD          = 0xff6ed7f5;  // 0xAABBGGRR of #f5d76e
const u32 THEME_GOLD_DIM      = 0xff428ea8;  // #a88e42
const u32 THEME_LIFT_GLOW     = 0x666ed7f5;  // gold @ 40% alpha
const u32 THEME_TEXT_PRIMARY  = 0xfff5f5f5;
const u32 THEME_TEXT_DIMMED   = 0xff888888;
const u32 THEME_STATUS_WORKING      = 0xff6ed7f5;
const u32 THEME_STATUS_ACTIVE       = 0xff6ed76e;
const u32 THEME_STATUS_IDLE         = 0xff888888;
const u32 THEME_STATUS_DISCONNECTED = 0xff6e6ed7;

u32 theme_status_color(const char *status) {
    if (!status) return THEME_STATUS_IDLE;
    if (strcmp(status, "working") == 0) return THEME_STATUS_WORKING;
    if (strcmp(status, "active") == 0)  return THEME_STATUS_ACTIVE;
    if (strcmp(status, "disconnected") == 0) return THEME_STATUS_DISCONNECTED;
    return THEME_STATUS_IDLE;
}
