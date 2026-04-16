#include "card.h"
#include "theme.h"
#include <string.h>
#include <stdio.h>

static void truncate_copy(char *dest, size_t dest_size, const char *src, int max_chars) {
    int copy = max_chars < (int)(dest_size - 1) ? max_chars : (int)(dest_size - 1);
    int n = 0;
    while (src && src[n] && n < copy) { dest[n] = src[n]; n++; }
    dest[n] = '\0';
    if (src && src[n]) {
        // Replace last char with ellipsis marker "."
        if (n >= 1) dest[n - 1] = '.';
    }
}

void card_draw(CogRender *r, const Card *c, float sx, float sy,
               float world_to_screen_scale) {
    float sw = c->width * world_to_screen_scale * c->lift_scale;
    float sh = c->height * world_to_screen_scale * c->lift_scale;

    // Lift glow (if lifted)
    if (c->lift_scale > 1.0f) {
        card_draw_lift_glow(sx, sy, sw, sh);
    }

    // Card background — tinted by group color but with fixed dark alpha
    // so text stays legible even with bright group colors.
    u32 bg = (c->color & 0x00ffffff) | 0xcc000000;  // 80% alpha
    cog_render_rounded_rect(sx, sy, sw, sh, 4.0f, bg);

    // Status dot (top-left)
    u32 dot_color = theme_status_color(c->status);
    C2D_DrawCircleSolid(sx + 6, sy + 6, 0.5f, 3.0f, dot_color);

    // Name (truncated to 12 chars, placed to the right of the dot)
    char name_trunc[16];
    truncate_copy(name_trunc, sizeof(name_trunc), c->name, 12);
    cog_render_text(r, name_trunc, sx + 14, sy + 2, THEME_FONT_CARD, THEME_TEXT_PRIMARY);

    // CLI badge + role on second line (truncated)
    char line2[32];
    const char *cli_short = c->cli[0] ? c->cli : "?";
    const char *role = c->role[0] ? c->role : "";
    snprintf(line2, sizeof(line2), "%.3s %s%.*s", cli_short, role[0] ? "· " : "",
             role[0] ? (int)sizeof(line2) - 8 : 0, role);
    cog_render_text(r, line2, sx + 4, sy + 20, THEME_FONT_FOOTER, THEME_TEXT_DIMMED);

    // Selection ring overlay
    if (c->selected) {
        card_draw_selection_ring(sx, sy, sw, sh);
    }
}

void card_draw_selection_ring(float sx, float sy, float sw, float sh) {
    cog_render_rect_outline(sx - 1, sy - 1, sw + 2, sh + 2, 2.0f, THEME_GOLD);
}

void card_draw_lift_glow(float sx, float sy, float sw, float sh) {
    // Three concentric faded rects to fake a soft glow — cheap + readable.
    u32 g1 = (THEME_LIFT_GLOW & 0x00ffffff) | 0x66000000;
    u32 g2 = (THEME_LIFT_GLOW & 0x00ffffff) | 0x33000000;
    u32 g3 = (THEME_LIFT_GLOW & 0x00ffffff) | 0x15000000;
    cog_render_rounded_rect(sx - 2, sy - 2, sw + 4, sh + 4, 6.0f, g1);
    cog_render_rounded_rect(sx - 4, sy - 4, sw + 8, sh + 8, 8.0f, g2);
    cog_render_rounded_rect(sx - 6, sy - 6, sw + 12, sh + 12, 10.0f, g3);
}
