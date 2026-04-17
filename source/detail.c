#include "detail.h"
#include "theme.h"
#include <stdio.h>
#include <string.h>

#define TOP_W 400
#define TOP_H 240
#define HEADER_H 28
#define FOOTER_H 20

typedef struct { char id[64]; char title[128]; char description[256];
                 char priority[8]; char status[16];
                 char created_by[64]; char claimed_by[64]; } DetailTask;
typedef struct { char id[64]; char from[64]; char note[256];
                 char tags[128]; } DetailInfo;

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

static void draw_footer(CogRender *r, const Card *card_or_null) {
    float fy = TOP_H - FOOTER_H;
    cog_render_rect(0, fy, TOP_W, 1, THEME_DIVIDER);
    if (card_or_null) {
        cog_render_text(r, "[A] actions  [B] deselect  [D-pad] scroll",
                        12, fy + 4, THEME_FONT_FOOTER, THEME_TEXT_DIMMED);
    } else {
        cog_render_text(r, "[A] spawn  [SELECT] setup  [START] exit",
                        12, fy + 4, THEME_FONT_FOOTER, THEME_TEXT_DIMMED);
    }
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
    cog_render_text(r, value[0] ? value : "\xe2\x80\x94", 90, y, THEME_FONT_LABEL, THEME_TEXT_PRIMARY);
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
    cog_render_text(r, "[A] for actions menu",
                    12, y, THEME_FONT_FOOTER, THEME_TEXT_DIMMED);
}

static void draw_pinboard_body(CogRender *r, const void *tasks, int task_count,
                               int detail_scroll) {
    float y = HEADER_H + 8;
    cog_render_text(r, "Pinboard", 12, y, THEME_FONT_HEADER, THEME_GOLD);
    y += 28;
    char summary[32];
    snprintf(summary, sizeof(summary), "%d tasks", task_count);
    cog_render_text(r, summary, 12, y, THEME_FONT_LABEL, THEME_TEXT_DIMMED);
    y += 20;
    cog_render_rect(12, y, TOP_W - 24, 1, THEME_DIVIDER);
    y += 8;

    const DetailTask *t = (const DetailTask *)tasks;
    int visible_start = detail_scroll;
    int row_h = 28;
    float body_bottom = TOP_H - FOOTER_H - 4;

    for (int i = visible_start; i < task_count && y + row_h <= body_bottom; i++) {
        // Status icon
        const char *icon = "[ ]";
        u32 icon_color = THEME_TEXT_DIMMED;
        if (strcmp(t[i].status, "in_progress") == 0) {
            icon = "[>]";
            icon_color = THEME_STATUS_WORKING;
        } else if (strcmp(t[i].status, "completed") == 0) {
            icon = "[x]";
            icon_color = THEME_STATUS_ACTIVE;
        }
        cog_render_text(r, icon, 12, y, THEME_FONT_LABEL, icon_color);

        // Priority color
        u32 title_color = THEME_TEXT_PRIMARY;
        if (strcmp(t[i].priority, "high") == 0)
            title_color = THEME_STATUS_DISCONNECTED;
        else if (strcmp(t[i].priority, "low") == 0)
            title_color = THEME_TEXT_DIMMED;

        cog_render_text(r, t[i].title, 46, y, THEME_FONT_LABEL, title_color);
        y += row_h;
    }

    if (task_count == 0) {
        cog_render_text(r, "(no tasks)", 12, y, THEME_FONT_FOOTER, THEME_TEXT_DIMMED);
    }
}

static void draw_info_body(CogRender *r, const void *infos, int info_count,
                           int detail_scroll) {
    float y = HEADER_H + 8;
    cog_render_text(r, "Info Channel", 12, y, THEME_FONT_HEADER, THEME_GOLD);
    y += 28;
    char summary[32];
    snprintf(summary, sizeof(summary), "%d notes", info_count);
    cog_render_text(r, summary, 12, y, THEME_FONT_LABEL, THEME_TEXT_DIMMED);
    y += 20;
    cog_render_rect(12, y, TOP_W - 24, 1, THEME_DIVIDER);
    y += 8;

    const DetailInfo *inf = (const DetailInfo *)infos;
    int visible_start = detail_scroll;
    int row_h = 34;
    float body_bottom = TOP_H - FOOTER_H - 4;

    for (int i = visible_start; i < info_count && y + row_h <= body_bottom; i++) {
        // "from: note"
        char line[320];
        snprintf(line, sizeof(line), "%s: %s", inf[i].from, inf[i].note);
        // Truncate to fit
        if (strlen(line) > 60) { line[57] = '.'; line[58] = '.'; line[59] = '.'; line[60] = '\0'; }
        cog_render_text(r, line, 12, y, THEME_FONT_LABEL, THEME_TEXT_PRIMARY);
        if (inf[i].tags[0]) {
            cog_render_text(r, inf[i].tags, 12, y + 14, THEME_FONT_FOOTER, THEME_TEXT_DIMMED);
        }
        y += row_h;
    }

    if (info_count == 0) {
        cog_render_text(r, "(no notes)", 12, y, THEME_FONT_FOOTER, THEME_TEXT_DIMMED);
    }
}

void detail_draw(CogRender *r, const char *project_name,
                 const Card *card_or_null, int agent_count,
                 int connection_count,
                 const void *tasks, int task_count,
                 const void *infos, int info_count,
                 int detail_scroll) {
    draw_header(r, project_name, agent_count, connection_count);
    if (card_or_null) {
        if (card_or_null->card_type == CARD_TYPE_PINBOARD_CARD) {
            draw_pinboard_body(r, tasks, task_count, detail_scroll);
        } else if (card_or_null->card_type == CARD_TYPE_INFO_CARD) {
            draw_info_body(r, infos, info_count, detail_scroll);
        } else {
            draw_body(r, card_or_null);
        }
    } else {
        draw_empty_body(r);
    }
    draw_footer(r, card_or_null);
}
