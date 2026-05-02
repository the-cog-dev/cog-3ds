#include "detail.h"
#include "theme.h"
#include <stdio.h>
#include <string.h>

#define TOP_W 400
#define TOP_H 240
#define HEADER_H 28
#define FOOTER_H 20
#define MAX_TASKS 32

typedef struct { char id[64]; char title[128]; char description[256];
                 char priority[8]; char status[16];
                 char created_by[64]; char claimed_by[64]; } DetailTask;
typedef struct { char id[64]; char from[64]; char note[256];
                 char tags[128]; } DetailInfo;
typedef struct { char id[64]; char name[128]; char agent_name[64];
                 int interval_minutes; char status[16]; } DetailSchedule;

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
    if (card_or_null && card_or_null->card_type == CARD_TYPE_PINBOARD_CARD) {
        cog_render_text(r, "[L/R] tabs  [A] actions  [B] deselect  [D-pad] scroll",
                        12, fy + 4, THEME_FONT_FOOTER, THEME_TEXT_DIMMED);
    } else if (card_or_null) {
        cog_render_text(r, "[A] actions  [B] deselect  [D-pad] scroll",
                        12, fy + 4, THEME_FONT_FOOTER, THEME_TEXT_DIMMED);
    } else {
        cog_render_text(r, "[A] spawn  [Y] fit  [SELECT] setup  [START] exit",
                        12, fy + 4, THEME_FONT_FOOTER, THEME_TEXT_DIMMED);
    }
}

static void draw_empty_body(CogRender *r) {
    cog_render_text(r, "Tap a card to see details.",
                    12, HEADER_H + 20, THEME_FONT_CARD, THEME_TEXT_DIMMED);
    cog_render_text(r, "Circle pad to pan, D-pad to navigate.",
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
                               int detail_scroll, int pinboard_tab) {
    static const char *TAB_NAMES[] = { "Open", "In Progress", "Completed" };
    static const char *TAB_FILTERS[] = { "open", "in_progress", "completed" };
    const u32 TAB_COLORS[] = { THEME_TEXT_DIMMED, THEME_STATUS_WORKING, THEME_STATUS_ACTIVE };

    float y = HEADER_H + 8;
    cog_render_text(r, "Pinboard", 12, y, THEME_FONT_HEADER, THEME_GOLD);
    y += 28;

    // Tab bar
    float tab_x = 12;
    for (int t = 0; t < 3; t++) {
        u32 col = (t == pinboard_tab) ? TAB_COLORS[t] : THEME_DIVIDER;
        // Count tasks in this tab
        int cnt = 0;
        const DetailTask *dt = (const DetailTask *)tasks;
        for (int i = 0; i < task_count; i++)
            if (strcmp(dt[i].status, TAB_FILTERS[t]) == 0) cnt++;
        char label[32];
        snprintf(label, sizeof(label), "%s (%d)", TAB_NAMES[t], cnt);
        cog_render_text(r, label, tab_x, y, THEME_FONT_FOOTER, col);
        tab_x += 130;
    }
    y += 18;

    // Underline for active tab
    float ul_x = 12 + pinboard_tab * 130;
    cog_render_rect(ul_x, y, 100, 2, TAB_COLORS[pinboard_tab]);
    y += 6;
    cog_render_rect(12, y, TOP_W - 24, 1, THEME_DIVIDER);
    y += 8;

    // Filter tasks to current tab
    const DetailTask *all = (const DetailTask *)tasks;
    const DetailTask *filtered[MAX_TASKS];
    int filtered_count = 0;
    for (int i = 0; i < task_count && filtered_count < MAX_TASKS; i++) {
        if (strcmp(all[i].status, TAB_FILTERS[pinboard_tab]) == 0)
            filtered[filtered_count++] = &all[i];
    }

    int visible_start = detail_scroll;
    int row_h = 28;
    float body_bottom = TOP_H - FOOTER_H - 4;

    for (int i = visible_start; i < filtered_count && y + row_h <= body_bottom; i++) {
        const DetailTask *ft = filtered[i];
        // Priority color
        u32 title_color = THEME_TEXT_PRIMARY;
        if (strcmp(ft->priority, "high") == 0)
            title_color = THEME_STATUS_DISCONNECTED;
        else if (strcmp(ft->priority, "low") == 0)
            title_color = THEME_TEXT_DIMMED;

        // Show claimed_by for in_progress, created_by for open
        const char *meta = "";
        if (pinboard_tab == 1 && ft->claimed_by[0])
            meta = ft->claimed_by;
        else if (pinboard_tab == 0 && ft->created_by[0])
            meta = ft->created_by;

        cog_render_text(r, ft->title, 12, y, THEME_FONT_LABEL, title_color);
        if (meta[0]) {
            cog_render_text(r, meta, 12, y + 13, THEME_FONT_FOOTER, THEME_TEXT_DIMMED);
            y += row_h + 6;
        } else {
            y += row_h;
        }
    }

    if (filtered_count == 0) {
        const char *empty_msg = "(none)";
        if (pinboard_tab == 0) empty_msg = "No open tasks";
        else if (pinboard_tab == 1) empty_msg = "Nothing in progress";
        else empty_msg = "No completed tasks";
        cog_render_text(r, empty_msg, 12, y, THEME_FONT_FOOTER, THEME_TEXT_DIMMED);
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

static void draw_schedule_body(CogRender *r, const void *schedules, int schedule_count,
                               int detail_scroll) {
    float y = HEADER_H + 8;
    cog_render_text(r, "Schedules", 12, y, THEME_FONT_HEADER, THEME_GOLD);
    y += 28;
    char summary[32];
    snprintf(summary, sizeof(summary), "%d schedules", schedule_count);
    cog_render_text(r, summary, 12, y, THEME_FONT_LABEL, THEME_TEXT_DIMMED);
    y += 20;
    cog_render_rect(12, y, TOP_W - 24, 1, THEME_DIVIDER);
    y += 8;

    const DetailSchedule *s = (const DetailSchedule *)schedules;
    int visible_start = detail_scroll;
    int row_h = 34;
    float body_bottom = TOP_H - FOOTER_H - 4;

    for (int i = visible_start; i < schedule_count && y + row_h <= body_bottom; i++) {
        // Status color
        u32 status_color = THEME_TEXT_DIMMED;
        if (strcmp(s[i].status, "active") == 0)
            status_color = THEME_STATUS_ACTIVE;
        else if (strcmp(s[i].status, "paused") == 0)
            status_color = THEME_GOLD;
        else if (strcmp(s[i].status, "expired") == 0)
            status_color = THEME_STATUS_DISCONNECTED;

        // Name + interval
        char line[256];
        snprintf(line, sizeof(line), "%s  (every %dm)", s[i].name, s[i].interval_minutes);
        if (strlen(line) > 55) { line[52] = '.'; line[53] = '.'; line[54] = '.'; line[55] = '\0'; }
        cog_render_text(r, line, 12, y, THEME_FONT_LABEL, THEME_TEXT_PRIMARY);

        // Agent + status on second line
        char line2[128];
        snprintf(line2, sizeof(line2), "%s  [%s]", s[i].agent_name, s[i].status);
        cog_render_text(r, line2, 12, y + 14, THEME_FONT_FOOTER, status_color);
        y += row_h;
    }

    if (schedule_count == 0) {
        cog_render_text(r, "(no schedules)", 12, y, THEME_FONT_FOOTER, THEME_TEXT_DIMMED);
    }
}

static void draw_trollbox_body(CogRender *r) {
    float y = HEADER_H + 8;
    cog_render_text(r, "Trollbox", 12, y, THEME_FONT_HEADER, THEME_GOLD);
    y += 28;
    cog_render_text(r, "Realtime crew chat. Bridges through",
                    12, y, THEME_FONT_LABEL, THEME_TEXT_PRIMARY); y += 18;
    cog_render_text(r, "the desktop's Trollbox panel — open it",
                    12, y, THEME_FONT_LABEL, THEME_TEXT_PRIMARY); y += 18;
    cog_render_text(r, "on PC for chat to work.",
                    12, y, THEME_FONT_LABEL, THEME_TEXT_PRIMARY); y += 26;
    cog_render_rect(12, y, TOP_W - 24, 1, THEME_DIVIDER); y += 12;
    cog_render_text(r, "Press [A] to open chat",
                    12, y, THEME_FONT_FOOTER, THEME_GOLD); y += 18;
    cog_render_text(r, "[Y] type message  [X] change nick",
                    12, y, THEME_FONT_FOOTER, THEME_TEXT_DIMMED);
}

static void draw_inbox_body(CogRender *r, int inbox_count, int inbox_unread) {
    float y = HEADER_H + 8;
    cog_render_text(r, "Inbox", 12, y, THEME_FONT_HEADER, THEME_GOLD);
    y += 28;
    char summary[64];
    snprintf(summary, sizeof(summary), "%d total  |  %d unread",
             inbox_count, inbox_unread);
    u32 col = inbox_unread > 0 ? THEME_GOLD : THEME_TEXT_DIMMED;
    cog_render_text(r, summary, 12, y, THEME_FONT_LABEL, col);
    y += 22;
    cog_render_rect(12, y, TOP_W - 24, 1, THEME_DIVIDER);
    y += 12;
    cog_render_text(r, "Orchestrator notifications and team",
                    12, y, THEME_FONT_LABEL, THEME_TEXT_PRIMARY);
    y += 18;
    cog_render_text(r, "proposals from your agents land here.",
                    12, y, THEME_FONT_LABEL, THEME_TEXT_PRIMARY);
    y += 26;
    if (inbox_count == 0) {
        cog_render_text(r, "(empty)", 12, y, THEME_FONT_FOOTER, THEME_TEXT_DIMMED);
    } else {
        cog_render_text(r, "Press [A] to open and approve/reject",
                        12, y, THEME_FONT_FOOTER, THEME_GOLD);
        y += 18;
        cog_render_text(r, "team proposals or mark as read.",
                        12, y, THEME_FONT_FOOTER, THEME_TEXT_DIMMED);
    }
}

void detail_draw(CogRender *r, const char *project_name,
                 const Card *card_or_null, int agent_count,
                 int connection_count,
                 const void *tasks, int task_count,
                 const void *infos, int info_count,
                 const void *schedules, int schedule_count,
                 int detail_scroll, int pinboard_tab,
                 int inbox_count, int inbox_unread) {
    draw_header(r, project_name, agent_count, connection_count);
    if (card_or_null) {
        if (card_or_null->card_type == CARD_TYPE_PINBOARD_CARD) {
            draw_pinboard_body(r, tasks, task_count, detail_scroll, pinboard_tab);
        } else if (card_or_null->card_type == CARD_TYPE_INFO_CARD) {
            draw_info_body(r, infos, info_count, detail_scroll);
        } else if (card_or_null->card_type == CARD_TYPE_SCHEDULE_CARD) {
            draw_schedule_body(r, schedules, schedule_count, detail_scroll);
        } else if (card_or_null->card_type == CARD_TYPE_INBOX_CARD) {
            draw_inbox_body(r, inbox_count, inbox_unread);
        } else if (card_or_null->card_type == CARD_TYPE_TROLLBOX_CARD) {
            draw_trollbox_body(r);
        } else {
            draw_body(r, card_or_null);
        }
    } else {
        draw_empty_body(r);
    }
    draw_footer(r, card_or_null);
}
