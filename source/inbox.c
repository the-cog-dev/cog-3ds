#include "inbox.h"
#include "theme.h"
#include "http.h"
#include <3ds.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// Layout — both screens have plenty of space, so we use the top for the
// detail of the highlighted message and the bottom for the scrollable list.
#define BOT_W 320
#define BOT_H 240
#define TOP_W 400
#define TOP_H 240

#define LIST_HEADER_H 22
#define LIST_FOOTER_H 22
#define LIST_ROW_H    32

#define DETAIL_HEADER_H 28

static u32 priority_color(const char *priority) {
    if (!priority) return THEME_TEXT_DIMMED;
    if (strcmp(priority, "urgent") == 0) return THEME_STATUS_DISCONNECTED;  // red
    if (strcmp(priority, "high")   == 0) return THEME_STATUS_WORKING;       // yellow
    if (strcmp(priority, "normal") == 0) return THEME_TEXT_PRIMARY;
    return THEME_TEXT_DIMMED;
}

static int visible_rows(void) {
    return (BOT_H - LIST_HEADER_H - LIST_FOOTER_H) / LIST_ROW_H;
}

// POST inbox/<id>/read. Fire-and-forget, response ignored.
static void post_mark_read(const char *base_url, const char *msg_id) {
    char url[512];
    snprintf(url, sizeof(url), "%sinbox/%s/read", base_url, msg_id);
    char *resp = NULL; size_t rlen = 0;
    cog_http_post_json(url, "{}", &resp, &rlen);
    if (resp) free(resp);
}

// POST inbox/<id>/respond with {"action":"approve"|"reject","proposalId":"..."}
static int post_respond(const char *base_url, const char *msg_id,
                        const char *proposal_id, const char *action) {
    char url[512];
    snprintf(url, sizeof(url), "%sinbox/%s/respond", base_url, msg_id);
    char body[256];
    snprintf(body, sizeof(body),
             "{\"action\":\"%s\",\"proposalId\":\"%s\"}", action, proposal_id);
    char *resp = NULL; size_t rlen = 0;
    int code = cog_http_post_json(url, body, &resp, &rlen);
    if (resp) free(resp);
    return code;
}

// Word-wrap a long message into lines that fit within max_width pixels.
// Writes into out_buf (one continuous buffer with '\n' separators) and
// returns the number of lines. Caller passes a buffer big enough for the
// worst case — we cap copy length at out_size - 1.
static int wrap_text(CogRender *r, const char *text, float max_width,
                     float scale, char *out_buf, size_t out_size) {
    size_t pos = 0;
    int line_count = 0;
    if (!text || !*text) {
        if (out_size > 0) out_buf[0] = '\0';
        return 0;
    }
    char line_buf[256];
    int li = 0;
    int last_space = -1;
    for (size_t i = 0; text[i] && pos < out_size - 1; i++) {
        char c = text[i];
        if (c == '\n') {
            line_buf[li] = '\0';
            int copy = li < (int)(out_size - pos - 2) ? li : (int)(out_size - pos - 2);
            memcpy(out_buf + pos, line_buf, copy);
            pos += copy;
            out_buf[pos++] = '\n';
            line_count++;
            li = 0;
            last_space = -1;
            continue;
        }
        if (c == ' ') last_space = li;
        if (li < (int)sizeof(line_buf) - 1) line_buf[li++] = c;
        line_buf[li] = '\0';
        if (cog_render_text_width(r, line_buf, scale) > max_width && last_space > 0) {
            // Break at last space
            char tmp = line_buf[last_space];
            line_buf[last_space] = '\0';
            int copy = last_space < (int)(out_size - pos - 2) ? last_space : (int)(out_size - pos - 2);
            memcpy(out_buf + pos, line_buf, copy);
            pos += copy;
            out_buf[pos++] = '\n';
            line_count++;
            // Carry remainder
            int rem_len = li - last_space - 1;
            memmove(line_buf, line_buf + last_space + 1, rem_len);
            li = rem_len;
            line_buf[li] = '\0';
            line_buf[last_space] = tmp;
            last_space = -1;
        }
    }
    if (li > 0) {
        int copy = li < (int)(out_size - pos - 1) ? li : (int)(out_size - pos - 1);
        memcpy(out_buf + pos, line_buf, copy);
        pos += copy;
        line_count++;
    }
    out_buf[pos] = '\0';
    return line_count;
}

static void draw_list_top(CogRender *r, const InboxMsg *msg) {
    cog_render_target_top(r, THEME_BG_DARK);
    cog_render_rect(0, 0, TOP_W, DETAIL_HEADER_H, THEME_BG_CANVAS);
    cog_render_rect(0, DETAIL_HEADER_H - 1, TOP_W, 1, THEME_DIVIDER);
    cog_render_text(r, "Inbox Detail", 12, 5, THEME_FONT_HEADER, THEME_GOLD);

    if (!msg) {
        cog_render_text(r, "(empty inbox)", 140, 110, THEME_FONT_LABEL, THEME_TEXT_DIMMED);
        return;
    }

    float y = DETAIL_HEADER_H + 8;

    // From + priority dot
    char meta[96];
    snprintf(meta, sizeof(meta), "from %s  [%s]",
             msg->agent_name, msg->priority);
    cog_render_text(r, meta, 12, y, THEME_FONT_LABEL, priority_color(msg->priority));
    y += 22;

    // Proposal indicator
    if (msg->proposal_id[0]) {
        char ptag[32];
        snprintf(ptag, sizeof(ptag), "[PROPOSAL — %s]", msg->proposal_status);
        u32 col = strcmp(msg->proposal_status, "pending") == 0
                  ? THEME_GOLD : THEME_TEXT_DIMMED;
        cog_render_text(r, ptag, 12, y, THEME_FONT_FOOTER, col);
        y += 18;
    }

    // Body — word-wrapped
    char wrapped[1024];
    wrap_text(r, msg->message, TOP_W - 24, THEME_FONT_LABEL,
              wrapped, sizeof(wrapped));
    char *line = wrapped;
    while (line && *line && y < TOP_H - 24) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        cog_render_text(r, line, 12, y, THEME_FONT_LABEL, THEME_TEXT_PRIMARY);
        y += 18;
        if (!nl) break;
        line = nl + 1;
    }
}

static void draw_list_bottom(CogRender *r, const InboxMsg *msgs, int count,
                             int selected, int scroll) {
    cog_render_target_bottom(r, THEME_BG_DARK);

    // Header
    cog_render_rect(0, 0, BOT_W, LIST_HEADER_H, THEME_BG_CANVAS);
    cog_render_rect(0, LIST_HEADER_H - 1, BOT_W, 1, THEME_DIVIDER);
    char hdr[32];
    snprintf(hdr, sizeof(hdr), "Inbox  (%d)", count);
    cog_render_text(r, hdr, 8, 4, THEME_FONT_LABEL, THEME_GOLD);

    // List rows
    int rows = visible_rows();
    for (int row = 0; row < rows; row++) {
        int idx = scroll + row;
        if (idx < 0 || idx >= count) break;
        const InboxMsg *m = &msgs[idx];
        float y = LIST_HEADER_H + row * LIST_ROW_H;

        // Row background — gold dim if selected, alternate-band otherwise
        if (idx == selected) {
            cog_render_rounded_rect(2, y + 2, BOT_W - 4, LIST_ROW_H - 4,
                                    3.0f, THEME_GOLD_DIM);
        } else if (!m->read) {
            // Subtle highlight for unread rows
            cog_render_rect(0, y, 3, LIST_ROW_H, THEME_GOLD);
        }

        u32 name_col = (idx == selected) ? THEME_TEXT_PRIMARY : priority_color(m->priority);
        u32 body_col = (idx == selected) ? THEME_TEXT_PRIMARY : THEME_TEXT_DIMMED;

        // Line 1: agent name + proposal badge
        char line1[64];
        if (m->proposal_id[0]) {
            snprintf(line1, sizeof(line1), "%.20s  [PROPOSAL]", m->agent_name);
        } else {
            snprintf(line1, sizeof(line1), "%.30s", m->agent_name);
        }
        cog_render_text(r, line1, 8, y + 4, THEME_FONT_FOOTER, name_col);

        // Line 2: message preview
        char preview[64];
        snprintf(preview, sizeof(preview), "%.40s", m->message);
        cog_render_text(r, preview, 8, y + 18, THEME_FONT_FOOTER, body_col);
    }

    // Footer hints
    float fy = BOT_H - LIST_FOOTER_H;
    cog_render_rect(0, fy, BOT_W, 1, THEME_DIVIDER);
    cog_render_text(r, "[A] open  [X] mark read  [B] back",
                    8, fy + 4, THEME_FONT_FOOTER, THEME_TEXT_DIMMED);
}

// Detail screen for a single message — full-screen takeover. Y/X to act
// when the message wraps a pending proposal, B to back out.
static bool message_detail(CogRender *r, const char *base_url,
                            InboxMsg *msg) {
    bool changed = false;
    bool show_team_team = false;  // toggle full team listing on top screen

    while (aptMainLoop()) {
        hidScanInput();
        u32 kd = hidKeysDown();

        if (kd & KEY_B) break;

        if (kd & KEY_X) {
            // Mark read (idempotent on server)
            if (!msg->read && msg->id[0]) {
                post_mark_read(base_url, msg->id);
                msg->read = true;
                changed = true;
            }
        }

        // Approve/Reject only valid for pending proposals
        bool is_pending_prop = msg->proposal_id[0] &&
                               strcmp(msg->proposal_status, "pending") == 0;
        if (is_pending_prop && (kd & KEY_Y)) {
            int code = post_respond(base_url, msg->id, msg->proposal_id, "approve");
            if (code == 200) {
                strncpy(msg->proposal_status, "approved", sizeof(msg->proposal_status) - 1);
                msg->read = true;
                changed = true;
            }
        }
        if (is_pending_prop && (kd & KEY_TOUCH)) {
            // Treat any bottom-screen tap as reject confirmation only when
            // a proposal is pending. Keeps the flow approve=Y / reject=touch
            // simple. Could add a real confirm prompt later.
            // (Skipping for now — touch reserved for scrolling later.)
        }
        if (is_pending_prop && (kd & KEY_R)) {
            // Use R as reject — Y/X are already taken (approve/mark-read).
            int code = post_respond(base_url, msg->id, msg->proposal_id, "reject");
            if (code == 200) {
                strncpy(msg->proposal_status, "rejected", sizeof(msg->proposal_status) - 1);
                msg->read = true;
                changed = true;
            }
        }
        if (is_pending_prop && (kd & KEY_L)) {
            show_team_team = !show_team_team;
        }

        // ── Render ────────────────────────────────────────────────────────
        cog_render_frame_begin(r);

        // Top screen — full message body or proposal team list
        cog_render_target_top(r, THEME_BG_DARK);
        cog_render_rect(0, 0, TOP_W, DETAIL_HEADER_H, THEME_BG_CANVAS);
        cog_render_rect(0, DETAIL_HEADER_H - 1, TOP_W, 1, THEME_DIVIDER);
        cog_render_text(r, msg->proposal_id[0] ? "Proposal Detail" : "Message",
                        12, 5, THEME_FONT_HEADER, THEME_GOLD);
        char rmeta[64];
        snprintf(rmeta, sizeof(rmeta), "from %s", msg->agent_name);
        cog_render_text_right(r, rmeta, TOP_W - 12, 10,
                              THEME_FONT_FOOTER, THEME_TEXT_DIMMED);

        float y = DETAIL_HEADER_H + 8;

        if (msg->proposal_id[0] && show_team_team) {
            // Full team listing
            cog_render_text(r, msg->proposal_summary, 12, y, THEME_FONT_LABEL, THEME_TEXT_PRIMARY);
            y += 22;
            cog_render_rect(12, y, TOP_W - 24, 1, THEME_DIVIDER);
            y += 8;
            for (int i = 0; i < msg->proposal_agent_count && y < TOP_H - 28; i++) {
                const InboxProposalAgent *a = &msg->proposal_agents[i];
                char line[128];
                snprintf(line, sizeof(line), "%-18s %s/%s  (%s)",
                         a->name, a->cli,
                         a->model[0] ? a->model : "default",
                         a->role);
                cog_render_text(r, line, 12, y, THEME_FONT_FOOTER, THEME_TEXT_PRIMARY);
                y += 16;
            }
        } else {
            char wrapped[1024];
            wrap_text(r, msg->message, TOP_W - 24, THEME_FONT_LABEL,
                      wrapped, sizeof(wrapped));
            char *line = wrapped;
            while (line && *line && y < TOP_H - 28) {
                char *nl = strchr(line, '\n');
                if (nl) *nl = '\0';
                cog_render_text(r, line, 12, y, THEME_FONT_LABEL, THEME_TEXT_PRIMARY);
                y += 18;
                if (!nl) break;
                line = nl + 1;
            }
            if (msg->proposal_id[0]) {
                y += 6;
                cog_render_rect(12, y, TOP_W - 24, 1, THEME_DIVIDER);
                y += 6;
                char psum[96];
                snprintf(psum, sizeof(psum), "Team: %d agents — [L] toggle list",
                         msg->proposal_agent_count);
                cog_render_text(r, psum, 12, y, THEME_FONT_FOOTER, THEME_GOLD);
            }
        }

        // Bottom screen — action hints / status
        cog_render_target_bottom(r, THEME_BG_DARK);
        cog_render_rect(0, 0, BOT_W, BOT_H, THEME_BG_DARK);
        float by = 20;
        cog_render_text(r, msg->proposal_id[0] ? "Proposal" : "Message",
                        12, by, THEME_FONT_HEADER, THEME_GOLD);
        by += 30;
        if (msg->proposal_id[0]) {
            char status_line[64];
            snprintf(status_line, sizeof(status_line), "Status: %s",
                     msg->proposal_status);
            u32 col = strcmp(msg->proposal_status, "pending") == 0 ? THEME_GOLD :
                      (strcmp(msg->proposal_status, "approved") == 0 ?
                          THEME_STATUS_ACTIVE : THEME_TEXT_DIMMED);
            cog_render_text(r, status_line, 12, by, THEME_FONT_LABEL, col);
            by += 26;
        }
        char rstatus[32];
        snprintf(rstatus, sizeof(rstatus), "Read: %s", msg->read ? "yes" : "no");
        cog_render_text(r, rstatus, 12, by, THEME_FONT_LABEL,
                        msg->read ? THEME_TEXT_DIMMED : THEME_GOLD);
        by += 32;

        // Hints
        cog_render_rect(0, BOT_H - 80, BOT_W, 1, THEME_DIVIDER);
        float hy = BOT_H - 72;
        if (is_pending_prop) {
            cog_render_text(r, "[Y] approve  [R] reject", 12, hy,
                            THEME_FONT_LABEL, THEME_TEXT_PRIMARY);
            hy += 20;
            cog_render_text(r, "[L] toggle team list", 12, hy,
                            THEME_FONT_FOOTER, THEME_TEXT_DIMMED);
            hy += 18;
        }
        cog_render_text(r, "[X] mark read  [B] back",
                        12, hy, THEME_FONT_FOOTER, THEME_TEXT_DIMMED);

        cog_render_frame_end(r);
    }
    return changed;
}

bool cog_inbox_run(CogRender *r, const char *base_url,
                   InboxMsg *msgs, int count) {
    int selected = 0;
    int scroll = 0;
    bool changed = false;

    while (aptMainLoop()) {
        hidScanInput();
        u32 kd = hidKeysDown();

        if (kd & KEY_B) break;
        if (kd & KEY_DDOWN && selected < count - 1) selected++;
        if (kd & KEY_DUP   && selected > 0)         selected--;

        // Keep selected within visible rows
        int rows = visible_rows();
        if (selected < scroll) scroll = selected;
        if (selected >= scroll + rows) scroll = selected - rows + 1;

        if (kd & KEY_X && count > 0) {
            // Mark selected as read (idempotent)
            InboxMsg *m = &msgs[selected];
            if (!m->read && m->id[0]) {
                post_mark_read(base_url, m->id);
                m->read = true;
                changed = true;
            }
        }
        if (kd & KEY_A && count > 0) {
            if (message_detail(r, base_url, &msgs[selected])) changed = true;
        }

        // Touch tap on a row selects it; double-tap could open it but for
        // now just selection — A confirms.
        if (kd & KEY_TOUCH) {
            touchPosition tp; hidTouchRead(&tp);
            int row = (tp.py - LIST_HEADER_H) / LIST_ROW_H;
            int idx = scroll + row;
            if (idx >= 0 && idx < count) selected = idx;
        }

        // Render
        cog_render_frame_begin(r);
        const InboxMsg *cur = (count > 0 && selected >= 0 && selected < count)
                              ? &msgs[selected] : NULL;
        draw_list_top(r, cur);
        draw_list_bottom(r, msgs, count, selected, scroll);
        cog_render_frame_end(r);
    }
    return changed;
}
