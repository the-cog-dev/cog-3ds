#include "output_viewer.h"
#include "theme.h"
#include "http.h"
#include "cJSON.h"

#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// -----------------------------------------------------------------------
// Layout constants
// -----------------------------------------------------------------------
#define TOP_W           400
#define TOP_H           240
#define BOT_W           320
#define BOT_H           240

#define HEADER_H        28
#define FOOTER_H        30    // footer bar height on bottom screen
#define LINE_H          16    // vertical spacing per output line
#define LINE_SCALE      THEME_FONT_FOOTER
#define LINE_X          8     // left margin for output text

#define TOP_LINES       12    // lines visible on top screen
#define BOT_LINES       10    // lines visible on bottom screen
#define TOTAL_VISIBLE   (TOP_LINES + BOT_LINES)

#define MAX_LINES       200
#define MAX_LINE_LEN    256

// -----------------------------------------------------------------------
// State
// -----------------------------------------------------------------------
typedef struct {
    char lines[MAX_LINES][MAX_LINE_LEN];
    int  count;
    int  scroll;   // index of first visible line
} OutputState;

// -----------------------------------------------------------------------
// fetch_output — GET output lines from the API
// -----------------------------------------------------------------------
static void fetch_output(OutputState *st, const char *base_url,
                         const char *agent_id) {
    st->count  = 0;
    st->scroll = 0;

    char url[512];
    // Use the non-workshop output endpoint — doesn't require PIN
    // verification. /agent/:id/output returns last 50 lines by default.
    snprintf(url, sizeof(url), "%sagent/%s/output",
             base_url, agent_id);

    char  *body = NULL;
    size_t body_len = 0;
    int    status  = cog_http_get(url, &body, &body_len);

    if (status != 200 || !body) {
        if (body) free(body);
        strncpy(st->lines[0], "(Failed to fetch output)", MAX_LINE_LEN - 1);
        st->lines[0][MAX_LINE_LEN - 1] = '\0';
        st->count = 1;
        return;
    }

    cJSON *root = cJSON_Parse(body);
    free(body);

    if (!root) {
        strncpy(st->lines[0], "(Failed to parse response)", MAX_LINE_LEN - 1);
        st->lines[0][MAX_LINE_LEN - 1] = '\0';
        st->count = 1;
        return;
    }

    cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "lines");
    if (cJSON_IsArray(arr)) {
        cJSON *item = NULL;
        cJSON_ArrayForEach(item, arr) {
            if (st->count >= MAX_LINES) break;
            if (cJSON_IsString(item) && item->valuestring) {
                // Strip ANSI escape codes (ESC[...X sequences)
                // before storing — the 3DS can't render them.
                const char *src = item->valuestring;
                char *dst = st->lines[st->count];
                int di = 0;
                for (int si = 0; src[si] && di < MAX_LINE_LEN - 1; si++) {
                    if (src[si] == '\x1b' && src[si + 1] == '[') {
                        // Skip ESC[ then all params (digits, semicolons, ?)
                        // until the command letter (A-Z a-z)
                        si += 2;
                        while (src[si] && !((src[si] >= 'A' && src[si] <= 'Z') ||
                               (src[si] >= 'a' && src[si] <= 'z'))) si++;
                        // si now points at the command letter, loop will si++
                        continue;
                    }
                    if (src[si] == '\x1b') continue; // bare ESC
                    if ((unsigned char)src[si] < 0x20 && src[si] != '\t') continue; // other control chars
                    dst[di++] = src[si];
                }
                dst[di] = '\0';
                st->count++;
            }
        }
    }

    cJSON_Delete(root);

    if (st->count == 0) {
        strncpy(st->lines[0], "(No output)", MAX_LINE_LEN - 1);
        st->lines[0][MAX_LINE_LEN - 1] = '\0';
        st->count = 1;
    }

    // Auto-scroll to bottom
    int max_scroll = st->count - TOTAL_VISIBLE;
    st->scroll = max_scroll > 0 ? max_scroll : 0;
}

// -----------------------------------------------------------------------
// clamp_scroll — enforce scroll bounds
// -----------------------------------------------------------------------
static void clamp_scroll(OutputState *st) {
    if (st->scroll < 0) st->scroll = 0;
    int max_scroll = st->count - TOTAL_VISIBLE;
    if (max_scroll < 0) max_scroll = 0;
    if (st->scroll > max_scroll) st->scroll = max_scroll;
}

// -----------------------------------------------------------------------
// draw_output — render both screens
// -----------------------------------------------------------------------
static void draw_output(CogRender *r, const OutputState *st,
                        const char *agent_name) {
    // ===== TOP SCREEN =====
    cog_render_target_top(r, THEME_BG_DARK);

    // Header bar
    cog_render_rect(0, 0, TOP_W, HEADER_H, THEME_BG_CANVAS);
    cog_render_rect(0, HEADER_H - 1, TOP_W, 1, THEME_DIVIDER);

    char header[128];
    snprintf(header, sizeof(header), "Output: %s",
             agent_name && *agent_name ? agent_name : "(unknown)");
    cog_render_text(r, header, 12, 6, THEME_FONT_LABEL, THEME_GOLD);

    // Scroll info at top-right
    char scroll_info[32];
    int  visible_end = st->scroll + TOTAL_VISIBLE;
    if (visible_end > st->count) visible_end = st->count;
    snprintf(scroll_info, sizeof(scroll_info), "%d/%d",
             st->count > 0 ? st->scroll + 1 : 0, st->count);
    cog_render_text_right(r, scroll_info, TOP_W - 8, 6,
                          THEME_FONT_FOOTER, THEME_TEXT_DIMMED);

    // Output lines — top screen (TOP_LINES)
    float y = HEADER_H + 4;
    for (int i = 0; i < TOP_LINES; i++) {
        int line_idx = st->scroll + i;
        if (line_idx >= st->count) break;
        cog_render_text(r, st->lines[line_idx], LINE_X, y,
                        LINE_SCALE, THEME_TEXT_PRIMARY);
        y += LINE_H;
    }

    // ===== BOTTOM SCREEN =====
    cog_render_target_bottom(r, THEME_BG_DARK);

    // Output lines — bottom screen (BOT_LINES)
    y = 4;
    for (int i = 0; i < BOT_LINES; i++) {
        int line_idx = st->scroll + TOP_LINES + i;
        if (line_idx >= st->count) break;
        cog_render_text(r, st->lines[line_idx], LINE_X, y,
                        LINE_SCALE, THEME_TEXT_PRIMARY);
        y += LINE_H;
    }

    // Footer bar at bottom of bottom screen
    float footer_y = BOT_H - FOOTER_H;
    cog_render_rect(0, footer_y, BOT_W, 1, THEME_DIVIDER);
    cog_render_rect(0, footer_y + 1, BOT_W, FOOTER_H - 1, THEME_BG_CANVAS);
    cog_render_text(r, "[D-pad] scroll  [A] refresh  [B] back",
                    8, footer_y + 8, THEME_FONT_FOOTER, THEME_TEXT_DIMMED);
}

// -----------------------------------------------------------------------
// cog_output_viewer — main entry point
// -----------------------------------------------------------------------
void cog_output_viewer(CogRender *r, const char *base_url,
                       const char *agent_id, const char *agent_name) {
    static OutputState st;
    memset(&st, 0, sizeof(st));

    // Fetch once on entry
    fetch_output(&st, base_url, agent_id);

    while (aptMainLoop()) {
        hidScanInput();
        u32 down = hidKeysDown();

        if (down & KEY_B) break;

        if (down & KEY_A) {
            fetch_output(&st, base_url, agent_id);
        }

        if (down & KEY_DUP) {
            st.scroll--;
            clamp_scroll(&st);
        }
        if (down & KEY_DDOWN) {
            st.scroll++;
            clamp_scroll(&st);
        }
        if (down & KEY_L) {
            st.scroll -= TOTAL_VISIBLE;
            clamp_scroll(&st);
        }
        if (down & KEY_R) {
            st.scroll += TOTAL_VISIBLE;
            clamp_scroll(&st);
        }

        cog_render_frame_begin(r);
        draw_output(r, &st, agent_name);
        cog_render_frame_end(r);
    }
}
