#include "trollbox.h"
#include "theme.h"
#include "http.h"
#include "keyboard.h"
#include "cJSON.h"
#include <3ds.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define TOP_W 400
#define TOP_H 240
#define BOT_W 320
#define BOT_H 240
#define HEADER_H 28
#define FOOTER_H 22
#define ROW_H 18

#define NICK_FILE_PATH "sdmc:/3ds/cog-3ds/trollbox-nick.txt"

typedef struct {
    char id[16];
    long long ts;
    char nick[TROLLBOX_NICK_MAX + 1];
    char text[256];
} TrollboxMsg;

typedef struct {
    char status[16];     // "connected" | "offline" | "paused" | ...
    int online_count;
    TrollboxMsg msgs[TROLLBOX_MAX_MESSAGES];
    int msg_count;
    long long pause_until;  // 0 = not paused
} TrollboxView;

// Persist nick across sessions in plaintext on the SD card. Tiny file,
// trivial format — exactly the bytes you typed.
static void load_nick(char *out, size_t out_size) {
    FILE *f = fopen(NICK_FILE_PATH, "r");
    if (!f) {
        strncpy(out, "3ds", out_size - 1);
        out[out_size - 1] = '\0';
        return;
    }
    if (!fgets(out, out_size, f)) {
        out[0] = '\0';
    }
    fclose(f);
    // Strip trailing newline
    size_t n = strlen(out);
    while (n > 0 && (out[n - 1] == '\n' || out[n - 1] == '\r')) {
        out[--n] = '\0';
    }
    if (n == 0) {
        strncpy(out, "3ds", out_size - 1);
        out[out_size - 1] = '\0';
    }
}

static void save_nick(const char *nick) {
    FILE *f = fopen(NICK_FILE_PATH, "w");
    if (!f) return;
    fputs(nick, f);
    fclose(f);
}

static void parse_view(const char *json_text, TrollboxView *out) {
    memset(out, 0, sizeof(*out));
    strncpy(out->status, "offline", sizeof(out->status) - 1);

    cJSON *root = cJSON_Parse(json_text);
    if (!root) return;

    cJSON *st = cJSON_GetObjectItemCaseSensitive(root, "status");
    if (cJSON_IsString(st) && st->valuestring) {
        strncpy(out->status, st->valuestring, sizeof(out->status) - 1);
    }
    cJSON *oc = cJSON_GetObjectItemCaseSensitive(root, "onlineCount");
    if (cJSON_IsNumber(oc)) out->online_count = oc->valueint;
    cJSON *pu = cJSON_GetObjectItemCaseSensitive(root, "pauseUntil");
    if (cJSON_IsNumber(pu)) out->pause_until = (long long)pu->valuedouble;

    cJSON *msgs = cJSON_GetObjectItemCaseSensitive(root, "messages");
    if (cJSON_IsArray(msgs)) {
        cJSON *m = NULL;
        cJSON_ArrayForEach(m, msgs) {
            if (out->msg_count >= TROLLBOX_MAX_MESSAGES) break;
            TrollboxMsg *tm = &out->msgs[out->msg_count];
            memset(tm, 0, sizeof(*tm));
            cJSON *id = cJSON_GetObjectItemCaseSensitive(m, "id");
            cJSON *ts = cJSON_GetObjectItemCaseSensitive(m, "ts");
            cJSON *nick = cJSON_GetObjectItemCaseSensitive(m, "nick");
            cJSON *text = cJSON_GetObjectItemCaseSensitive(m, "text");
            if (cJSON_IsString(id) && id->valuestring)
                strncpy(tm->id, id->valuestring, sizeof(tm->id) - 1);
            if (cJSON_IsNumber(ts)) tm->ts = (long long)ts->valuedouble;
            if (cJSON_IsString(nick) && nick->valuestring)
                strncpy(tm->nick, nick->valuestring, sizeof(tm->nick) - 1);
            if (cJSON_IsString(text) && text->valuestring) {
                // Strip non-ASCII bytes and control chars to keep citro2d
                // happy — matches the policy in the agent output viewer.
                const char *src = text->valuestring;
                int di = 0;
                for (int i = 0; src[i] && di < (int)sizeof(tm->text) - 1; i++) {
                    unsigned char c = (unsigned char)src[i];
                    if (c < 0x20 && c != '\t') continue;
                    if (c > 0x7E) continue;
                    tm->text[di++] = (char)c;
                }
                tm->text[di] = '\0';
            }
            out->msg_count++;
        }
    }
    cJSON_Delete(root);
}

static int fetch_view(const char *base_url, TrollboxView *out) {
    char url[512];
    snprintf(url, sizeof(url), "%strollbox", base_url);
    char *body = NULL; size_t blen = 0;
    int code = cog_http_get(url, &body, &blen);
    if (code == 200 && body) {
        parse_view(body, out);
    } else {
        memset(out, 0, sizeof(*out));
        strncpy(out->status, "offline", sizeof(out->status) - 1);
    }
    if (body) free(body);
    return code;
}

// JSON-escape a string into dest. Adds escapes for backslash, quote,
// newline, tab. Non-ASCII bytes are dropped (the desktop already enforces
// length, so we trust the local input here).
static void json_escape(const char *src, char *dest, size_t dest_size) {
    size_t di = 0;
    for (size_t i = 0; src[i] && di < dest_size - 2; i++) {
        char c = src[i];
        if (c == '\\' || c == '"') {
            if (di + 2 >= dest_size - 1) break;
            dest[di++] = '\\';
            dest[di++] = c;
        } else if (c == '\n') {
            if (di + 2 >= dest_size - 1) break;
            dest[di++] = '\\'; dest[di++] = 'n';
        } else if (c == '\t') {
            if (di + 2 >= dest_size - 1) break;
            dest[di++] = '\\'; dest[di++] = 't';
        } else if ((unsigned char)c >= 0x20 && (unsigned char)c <= 0x7E) {
            dest[di++] = c;
        }
    }
    dest[di] = '\0';
}

static int post_send(const char *base_url, const char *nick, const char *text) {
    char url[512];
    snprintf(url, sizeof(url), "%strollbox/send", base_url);
    char esc_nick[64];
    char esc_text[512];
    json_escape(nick, esc_nick, sizeof(esc_nick));
    json_escape(text, esc_text, sizeof(esc_text));
    char body[640];
    snprintf(body, sizeof(body), "{\"nick\":\"%s\",\"text\":\"%s\"}",
             esc_nick, esc_text);
    char *resp = NULL; size_t rlen = 0;
    int code = cog_http_post_json(url, body, &resp, &rlen);
    if (resp) free(resp);
    return code;
}

// Word-wrap a single message's text into rendered lines. Returns line count
// written into out_lines (each ROW_H tall). max_width is in pixels.
static int wrap_message(CogRender *r, const char *text, float max_width,
                        char out_lines[][96], int max_lines) {
    int lines = 0;
    char buf[256];
    int li = 0;
    int last_space = -1;
    for (size_t i = 0; text[i] && lines < max_lines; i++) {
        char c = text[i];
        if (c == ' ') last_space = li;
        if (li < (int)sizeof(buf) - 1) buf[li++] = c;
        buf[li] = '\0';
        if (cog_render_text_width(r, buf, THEME_FONT_FOOTER) > max_width &&
            last_space > 0) {
            // Break at last space
            buf[last_space] = '\0';
            strncpy(out_lines[lines], buf, 95);
            out_lines[lines][95] = '\0';
            lines++;
            int rem = li - last_space - 1;
            memmove(buf, buf + last_space + 1, rem);
            li = rem;
            buf[li] = '\0';
            last_space = -1;
        }
    }
    if (li > 0 && lines < max_lines) {
        strncpy(out_lines[lines], buf, 95);
        out_lines[lines][95] = '\0';
        lines++;
    }
    return lines;
}

// Top screen: scrolling chat log. Newest message at the bottom; the user
// can scroll up with the D-pad once we drop them onto a fixed view.
static void draw_chat_top(CogRender *r, const TrollboxView *view, int scroll) {
    cog_render_target_top(r, THEME_BG_DARK);
    cog_render_rect(0, 0, TOP_W, HEADER_H, THEME_BG_CANVAS);
    cog_render_rect(0, HEADER_H - 1, TOP_W, 1, THEME_DIVIDER);
    cog_render_text(r, "Trollbox", 12, 5, THEME_FONT_HEADER, THEME_GOLD);

    char meta[64];
    if (strcmp(view->status, "connected") == 0) {
        snprintf(meta, sizeof(meta), "%d online", view->online_count);
        cog_render_text_right(r, meta, TOP_W - 12, 10,
                              THEME_FONT_FOOTER, THEME_STATUS_ACTIVE);
    } else if (strcmp(view->status, "paused") == 0) {
        cog_render_text_right(r, "PAUSED", TOP_W - 12, 10,
                              THEME_FONT_FOOTER, THEME_STATUS_DISCONNECTED);
    } else {
        cog_render_text_right(r, view->status, TOP_W - 12, 10,
                              THEME_FONT_FOOTER, THEME_TEXT_DIMMED);
    }

    // Render messages from oldest visible up to bottom. Since wrapping
    // varies per message, we render upward from the bottom — newest first
    // — and stop when we're above the header.
    static char line_buf[8][96];
    float y = TOP_H - FOOTER_H - 4;
    int rendered = 0;
    int skip = scroll;
    for (int i = view->msg_count - 1; i >= 0 && y > HEADER_H + 4; i--) {
        const TrollboxMsg *m = &view->msgs[i];
        int n = wrap_message(r, m->text, TOP_W - 24, line_buf, 8);
        if (skip > 0) {
            // Skip whole messages first (cheap approximation of scroll).
            skip--;
            continue;
        }
        // Each message: nick prefix + wrapped lines, plus blank line.
        int total_h = (n + 1) * ROW_H;
        float msg_top = y - total_h;
        if (msg_top < HEADER_H + 4) break;
        // Nick line
        char nick_line[64];
        snprintf(nick_line, sizeof(nick_line), "%s:", m->nick);
        cog_render_text(r, nick_line, 12, msg_top, THEME_FONT_FOOTER, THEME_GOLD);
        for (int j = 0; j < n; j++) {
            cog_render_text(r, line_buf[j],
                            24, msg_top + (j + 1) * ROW_H,
                            THEME_FONT_FOOTER, THEME_TEXT_PRIMARY);
        }
        y = msg_top - 4;
        rendered++;
    }
    if (rendered == 0) {
        if (strcmp(view->status, "connected") != 0) {
            cog_render_text(r, "Trollbox is offline.", 12, HEADER_H + 12,
                            THEME_FONT_LABEL, THEME_TEXT_DIMMED);
            cog_render_text(r, "Open it on desktop to enable chat.",
                            12, HEADER_H + 32, THEME_FONT_FOOTER, THEME_TEXT_DIMMED);
        } else {
            cog_render_text(r, "(no messages yet)", 140, 110,
                            THEME_FONT_LABEL, THEME_TEXT_DIMMED);
        }
    }
}

// Bottom screen: nick line + status hints + control hints.
static void draw_chat_bottom(CogRender *r, const TrollboxView *view,
                             const char *nick, const char *send_status) {
    cog_render_target_bottom(r, THEME_BG_DARK);

    cog_render_rect(0, 0, BOT_W, HEADER_H, THEME_BG_CANVAS);
    cog_render_rect(0, HEADER_H - 1, BOT_W, 1, THEME_DIVIDER);
    cog_render_text(r, "Trollbox", 8, 5, THEME_FONT_LABEL, THEME_GOLD);
    char online[24];
    snprintf(online, sizeof(online), "%d online", view->online_count);
    cog_render_text_right(r, online, BOT_W - 8, 10,
                          THEME_FONT_FOOTER, THEME_TEXT_DIMMED);

    float y = HEADER_H + 16;
    // Nick row
    cog_render_text(r, "Nick:", 8, y, THEME_FONT_LABEL, THEME_TEXT_DIMMED);
    cog_render_text(r, nick, 60, y, THEME_FONT_LABEL, THEME_TEXT_PRIMARY);
    y += 26;

    if (send_status && send_status[0]) {
        cog_render_text(r, send_status, 8, y, THEME_FONT_FOOTER, THEME_GOLD);
        y += 18;
    }

    if (strcmp(view->status, "paused") == 0) {
        cog_render_text(r, "Channel paused by admin.",
                        8, y, THEME_FONT_FOOTER, THEME_STATUS_DISCONNECTED);
        y += 18;
    }

    // Hints
    cog_render_rect(0, BOT_H - 56, BOT_W, 1, THEME_DIVIDER);
    float hy = BOT_H - 50;
    cog_render_text(r, "[Y] type message",
                    8, hy, THEME_FONT_LABEL, THEME_TEXT_PRIMARY); hy += 18;
    cog_render_text(r, "[X] change nick   [B] back",
                    8, hy, THEME_FONT_FOOTER, THEME_TEXT_DIMMED);
}

void cog_trollbox_run(CogRender *r, const char *base_url) {
    TrollboxView view = {0};
    char nick[TROLLBOX_NICK_MAX + 1];
    load_nick(nick, sizeof(nick));

    int scroll = 0;
    char send_status[64] = "";
    u64 send_status_until = 0;
    u64 last_poll = 0;

    // Initial fetch so the first frame isn't blank.
    fetch_view(base_url, &view);
    last_poll = osGetTime();

    while (aptMainLoop()) {
        hidScanInput();
        u32 kd = hidKeysDown();

        if (kd & KEY_B) break;
        if (kd & KEY_DUP) scroll++;
        if (kd & KEY_DDOWN && scroll > 0) scroll--;

        if (kd & KEY_X) {
            // Change nick — reuse the keyboard module
            CogKeyboard kb;
            cog_keyboard_init(&kb, false, nick);
            while (aptMainLoop() && cog_keyboard_update(&kb)) {
                cog_render_frame_begin(r);
                cog_render_target_top(r, THEME_BG_DARK);
                cog_keyboard_draw_top(&kb, r, "Choose nick", THEME_GOLD);
                cog_render_target_bottom(r, THEME_BG_CANVAS);
                cog_keyboard_draw_bottom(&kb, r);
                cog_render_frame_end(r);
            }
            if (kb.submitted && cog_keyboard_text(&kb)[0]) {
                strncpy(nick, cog_keyboard_text(&kb), sizeof(nick) - 1);
                nick[sizeof(nick) - 1] = '\0';
                save_nick(nick);
            }
        }

        if (kd & KEY_Y) {
            // Compose message
            CogKeyboard kb;
            cog_keyboard_init(&kb, false, "");
            while (aptMainLoop() && cog_keyboard_update(&kb)) {
                cog_render_frame_begin(r);
                cog_render_target_top(r, THEME_BG_DARK);
                char prompt[64];
                snprintf(prompt, sizeof(prompt), "Trollbox as %s", nick);
                cog_keyboard_draw_top(&kb, r, prompt, THEME_GOLD);
                cog_render_target_bottom(r, THEME_BG_CANVAS);
                cog_keyboard_draw_bottom(&kb, r);
                cog_render_frame_end(r);
            }
            if (kb.submitted && cog_keyboard_text(&kb)[0]) {
                int code = post_send(base_url, nick, cog_keyboard_text(&kb));
                if (code == 200) {
                    strncpy(send_status, "Sent!", sizeof(send_status));
                    last_poll = 0;  // force immediate refresh
                } else {
                    snprintf(send_status, sizeof(send_status),
                             "Send failed (%d)", code);
                }
                send_status_until = osGetTime() + 2500;
            }
        }

        // Poll on cadence
        u64 now = osGetTime();
        if (now - last_poll >= TROLLBOX_POLL_MS) {
            fetch_view(base_url, &view);
            last_poll = now;
        }

        // Clear stale send-status banner
        if (send_status[0] && now > send_status_until) {
            send_status[0] = '\0';
        }

        cog_render_frame_begin(r);
        draw_chat_top(r, &view, scroll);
        draw_chat_bottom(r, &view, nick, send_status);
        cog_render_frame_end(r);
    }
}

int cog_trollbox_fetch_online_count(const char *base_url) {
    TrollboxView v;
    int code = fetch_view(base_url, &v);
    if (code != 200) return -1;
    if (strcmp(v.status, "connected") != 0) return -1;
    return v.online_count;
}
