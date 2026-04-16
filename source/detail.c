#include "detail.h"
#include "theme.h"
#include <stdio.h>

#define TOP_W 400
#define TOP_H 240
#define HEADER_H 28
#define FOOTER_H 20

static void draw_header(CogRender *r, const char *project_name,
                        int agent_count, int connection_count) {
    // Header bar background
    cog_render_rect(0, 0, TOP_W, HEADER_H, THEME_BG_CANVAS);
    cog_render_rect(0, HEADER_H - 1, TOP_W, 1, THEME_DIVIDER);
    // Title on the left
    cog_render_text(r, "The Cog", 12, 4, THEME_FONT_HEADER, THEME_GOLD);
    // Project name + meta on the right
    char meta[64];
    snprintf(meta, sizeof(meta), "%s  %d agents  %d conn",
             project_name && *project_name ? project_name : "(no project)",
             agent_count, connection_count);
    cog_render_text_right(r, meta, TOP_W - 12, 10, THEME_FONT_FOOTER, THEME_TEXT_DIMMED);
}

static void draw_footer(CogRender *r) {
    float fy = TOP_H - FOOTER_H;
    cog_render_rect(0, fy, TOP_W, 1, THEME_DIVIDER);
    cog_render_text(r, "[A] refresh [X] QR [L/R] zoom [SEL] setup",
                    12, fy + 4, THEME_FONT_FOOTER, THEME_TEXT_DIMMED);
}

static void draw_empty_body(CogRender *r) {
    cog_render_text(r, "Tap a card to see details.",
                    12, HEADER_H + 20, THEME_FONT_CARD, THEME_TEXT_DIMMED);
    cog_render_text(r, "D-pad cycles through agents.",
                    12, HEADER_H + 40, THEME_FONT_FOOTER, THEME_TEXT_DIMMED);
}

static void draw_label_value(CogRender *r, const char *label, const char *value,
                             float y) {
    cog_render_text(r, label, 12, y, THEME_FONT_LABEL, THEME_TEXT_DIMMED);
    cog_render_text(r, value[0] ? value : "—", 90, y, THEME_FONT_LABEL, THEME_TEXT_PRIMARY);
}

static void draw_body(CogRender *r, const Card *c) {
    float y = HEADER_H + 8;
    // Status dot + name as a title
    u32 dot = theme_status_color(c->status);
    C2D_DrawCircleSolid(20, y + 10, 0.5f, 6.0f, dot);
    cog_render_text(r, c->name, 34, y, THEME_FONT_HEADER, THEME_TEXT_PRIMARY);
    y += 34;
    draw_label_value(r, "CLI",    c->cli,    y); y += 22;
    draw_label_value(r, "Model",  c->model,  y); y += 22;
    draw_label_value(r, "Role",   c->role,   y); y += 22;
    draw_label_value(r, "Status", c->status, y); y += 22;
    // Divider
    cog_render_rect(12, y + 4, TOP_W - 24, 1, THEME_DIVIDER);
    y += 16;
    cog_render_text(r, "(Phase 3 will show live output here.)",
                    12, y, THEME_FONT_FOOTER, THEME_TEXT_DIMMED);
}

void detail_draw(CogRender *r, const char *project_name,
                 const Card *card_or_null, int agent_count,
                 int connection_count) {
    draw_header(r, project_name, agent_count, connection_count);
    if (card_or_null) {
        draw_body(r, card_or_null);
    } else {
        draw_empty_body(r);
    }
    draw_footer(r);
}
