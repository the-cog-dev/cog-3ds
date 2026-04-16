// The Cog — Nintendo 3DS client (Phase 1: real connection, real data, text UI)
//
// What works in this build:
//   - Loads saved Remote View URL from sdmc:/3ds/cog-3ds/config.txt
//   - If no saved URL: shows a setup screen telling you to scan a QR (Phase 1.5)
//   - HTTP GET to <URL>/state every 5s, parses JSON
//   - Bottom screen: agent list with status colors. D-pad to navigate, A to select
//   - Top screen: selected agent's name / CLI / model / role / status / project name
//
// Controls:
//   D-pad UP/DOWN  — navigate agent list
//   A              — refresh now
//   Y              — show URL info screen
//   START          — exit
//
// Phase 2 will replace the consoles with citro2d for real card-based rendering
// + theme colors. Phase 1.5 adds the QR scanner so you can scan instead of
// hardcoding the URL on PC.

#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "render.h"
#include "theme.h"
#include "canvas.h"
#include "card.h"
#include "detail.h"

#include "cJSON.h"
#include "http.h"
#include "config.h"
#include "qr_scan.h"

#define MAX_AGENTS 32

typedef struct {
    char id[64];
    char name[64];
    char cli[24];
    char model[32];
    char role[24];
    char status[16];   // "working", "active", "idle", "disconnected"
    float x, y;
    float width, height;
    u32 color_rgba;   // citro2d native
} AgentInfo;

typedef struct {
    char project_name[64];
    AgentInfo agents[MAX_AGENTS];
    int agent_count;
    int connection_count;
} CogState;

static const char *STATUS_COLOR_CODE(const char *status) {
    // ANSI color codes — match the desktop app's status palette
    if (strcmp(status, "working") == 0) return "\x1b[33m";       // yellow
    if (strcmp(status, "active") == 0) return "\x1b[32m";        // green
    if (strcmp(status, "idle") == 0) return "\x1b[37m";          // white
    if (strcmp(status, "disconnected") == 0) return "\x1b[31m";  // red
    return "\x1b[37m";
}

// Simple ease-out cubic: t in [0,1] -> smoothed out
static float ease_out_cubic(float t) {
    if (t <= 0) return 0;
    if (t >= 1) return 1;
    float x = 1 - t;
    return 1 - x * x * x;
}

// Forward declaration — defined below after build_state_url.
static u32 parse_hex_color(const char *hex);

// Pull a JSON string field, copy into dest with bounded length.
static void json_get_string(cJSON *parent, const char *field, char *dest, size_t dest_size) {
    cJSON *item = cJSON_GetObjectItemCaseSensitive(parent, field);
    if (cJSON_IsString(item) && item->valuestring) {
        strncpy(dest, item->valuestring, dest_size - 1);
        dest[dest_size - 1] = '\0';
    } else {
        dest[0] = '\0';
    }
}

// Parse the /state response into our struct. Returns true on success.
static bool parse_state(const char *json_text, CogState *out) {
    out->agent_count = 0;
    out->connection_count = 0;
    out->project_name[0] = '\0';

    cJSON *root = cJSON_Parse(json_text);
    if (!root) return false;

    json_get_string(root, "projectName", out->project_name, sizeof(out->project_name));

    cJSON *cc = cJSON_GetObjectItemCaseSensitive(root, "connectionCount");
    if (cJSON_IsNumber(cc)) out->connection_count = cc->valueint;

    cJSON *agents = cJSON_GetObjectItemCaseSensitive(root, "agents");
    if (cJSON_IsArray(agents)) {
        cJSON *agent = NULL;
        cJSON_ArrayForEach(agent, agents) {
            if (out->agent_count >= MAX_AGENTS) break;
            AgentInfo *a = &out->agents[out->agent_count];
            json_get_string(agent, "id", a->id, sizeof(a->id));
            json_get_string(agent, "name", a->name, sizeof(a->name));
            json_get_string(agent, "cli", a->cli, sizeof(a->cli));
            json_get_string(agent, "model", a->model, sizeof(a->model));
            json_get_string(agent, "role", a->role, sizeof(a->role));
            json_get_string(agent, "status", a->status, sizeof(a->status));
            cJSON *x = cJSON_GetObjectItemCaseSensitive(agent, "x");
            cJSON *y = cJSON_GetObjectItemCaseSensitive(agent, "y");
            cJSON *w = cJSON_GetObjectItemCaseSensitive(agent, "width");
            cJSON *h = cJSON_GetObjectItemCaseSensitive(agent, "height");
            cJSON *col = cJSON_GetObjectItemCaseSensitive(agent, "color");
            a->x = cJSON_IsNumber(x) ? (float)x->valuedouble : 0.0f;
            a->y = cJSON_IsNumber(y) ? (float)y->valuedouble : 0.0f;
            a->width = cJSON_IsNumber(w) ? (float)w->valuedouble : 0.0f;
            a->height = cJSON_IsNumber(h) ? (float)h->valuedouble : 0.0f;
            a->color_rgba = parse_hex_color(cJSON_IsString(col) ? col->valuestring : NULL);
            out->agent_count++;
        }
    }

    cJSON_Delete(root);
    return true;
}

// Build the state URL by appending "state" to the config URL. Config is
// expected to end with /r/<token>/ — so we just concatenate.
static void build_state_url(const char *base_url, char *out, size_t out_size) {
    snprintf(out, out_size, "%sstate", base_url);
}

static void build_window_url(const char *base_url, const char *agent_id,
                             char *out, size_t out_size) {
    snprintf(out, out_size, "%sworkshop/window/%s", base_url, agent_id);
}

// POST new position for the given card. Fire-and-forget; we don't
// block the render loop on network.
static void post_card_position(const char *base_url, const Card *c) {
    char url[COG_URL_MAX + 96];
    build_window_url(base_url, c->id, url, sizeof(url));
    char body[96];
    snprintf(body, sizeof(body), "{\"x\":%.0f,\"y\":%.0f}", c->x, c->y);
    char *resp = NULL;
    size_t resp_len = 0;
    cog_http_post_json(url, body, &resp, &resp_len);
    if (resp) free(resp);
}

// Parse a hex color string "#RRGGBB" into citro2d's 0xAABBGGRR layout.
// Returns a grey fallback for invalid input.
static u32 parse_hex_color(const char *hex) {
    if (!hex || hex[0] != '#' || !hex[1]) return 0xff888888;
    unsigned int r = 0, g = 0, b = 0;
    if (sscanf(hex + 1, "%2x%2x%2x", &r, &g, &b) != 3) return 0xff888888;
    return 0xff000000 | (b << 16) | (g << 8) | r;
}

// Sync the canvas's cards from the freshly-polled CogState. Preserves
// selection/lift for any card whose id is still present.
static void sync_canvas_from_state(Canvas *cv, const CogState *state) {
    // Stash currently-selected/lifted ids so we can restore
    char prev_selected[64] = "";
    char prev_lifted[64] = "";
    if (cv->selected_idx >= 0 && cv->selected_idx < cv->card_count)
        strncpy(prev_selected, cv->cards[cv->selected_idx].id, sizeof(prev_selected) - 1);
    if (cv->lifted_idx >= 0 && cv->lifted_idx < cv->card_count)
        strncpy(prev_lifted, cv->cards[cv->lifted_idx].id, sizeof(prev_lifted) - 1);

    cv->card_count = state->agent_count < CANVAS_MAX_CARDS ? state->agent_count : CANVAS_MAX_CARDS;
    cv->selected_idx = -1;
    cv->lifted_idx = -1;

    for (int i = 0; i < cv->card_count; i++) {
        Card *c = &cv->cards[i];
        const AgentInfo *a = &state->agents[i];
        strncpy(c->id, a->id, sizeof(c->id) - 1); c->id[sizeof(c->id) - 1] = '\0';
        strncpy(c->name, a->name, sizeof(c->name) - 1); c->name[sizeof(c->name) - 1] = '\0';
        strncpy(c->cli, a->cli, sizeof(c->cli) - 1); c->cli[sizeof(c->cli) - 1] = '\0';
        strncpy(c->model, a->model, sizeof(c->model) - 1); c->model[sizeof(c->model) - 1] = '\0';
        strncpy(c->role, a->role, sizeof(c->role) - 1); c->role[sizeof(c->role) - 1] = '\0';
        strncpy(c->status, a->status, sizeof(c->status) - 1); c->status[sizeof(c->status) - 1] = '\0';

        c->x = a->x; c->y = a->y;
        c->width = a->width > 0 ? a->width : CARD_DEFAULT_WIDTH;
        c->height = a->height > 0 ? a->height : CARD_DEFAULT_HEIGHT;
        c->color = a->color_rgba;
        c->lift_scale = 1.0f;
        c->enter_alpha = 1.0f;
        c->selected = false;
        c->lifted = false;

        if (prev_selected[0] && strcmp(prev_selected, c->id) == 0) {
            cv->selected_idx = i;
            c->selected = true;
        }
        if (prev_lifted[0] && strcmp(prev_lifted, c->id) == 0) {
            cv->lifted_idx = i;
            c->lifted = true;
            c->lift_scale = 1.2f;
        }
    }
}

// ── Rendering ────────────────────────────────────────────────────────────────

static void render_setup_screen(PrintConsole *top, PrintConsole *bottom) {
    consoleSelect(top);
    printf("\x1b[2J\x1b[1;1H");
    printf("\n");
    printf("    \x1b[33m  ___  _____  ___\x1b[0m\n");
    printf("    \x1b[33m / _ \\| ____|/ _ \\\x1b[0m       The Cog\n");
    printf("    \x1b[33m| (_) | |    | (_) |\x1b[0m       v0.2 - first connection\n");
    printf("    \x1b[33m \\___/|_|     \\___/\x1b[0m\n");
    printf("\n");
    printf("    \x1b[31mNo Remote View URL saved.\x1b[0m\n");
    printf("\n");
    printf("    To get connected:\n");
    printf("    1. On your PC, open The Cog\n");
    printf("    2. Settings -> Remote View -> Enable\n");
    printf("    3. Toggle on \x1b[32mEnable LAN access\x1b[0m\n");
    printf("    4. Copy the LAN URL (the http:// one)\n");
    printf("    5. Save as: \x1b[36m/3ds/cog-3ds/config.txt\x1b[0m\n");
    printf("       on your SD card\n");
    printf("\n");
    printf("    Or skip the file step entirely:\n");
    printf("    \x1b[32mPress [X] to scan a QR code.\x1b[0m\n");
    printf("\n");
    printf("    \x1b[37m[X]\x1b[0m scan QR  \x1b[37m[START]\x1b[0m exit\n");

    consoleSelect(bottom);
    printf("\x1b[2J\x1b[1;1H");
    printf("\n  \x1b[33mSetup needed\x1b[0m\n\n");
    printf("  Once config.txt is on your\n");
    printf("  SD card, relaunch this app.\n");
}

static void render_main_screens(PrintConsole *top, PrintConsole *bottom,
                                const CogState *state, int selected,
                                const char *url) {
    // ── Top: selected agent detail ───────────────────────────────────────────
    consoleSelect(top);
    printf("\x1b[2J\x1b[1;1H");
    printf("\n");
    printf("  \x1b[33m== The Cog : %s ==\x1b[0m\n", state->project_name);
    printf("\n");
    printf("  Agents: %d  |  Connections: %d\n", state->agent_count, state->connection_count);
    printf("\n");
    printf("  ----------------------------------\n");

    if (state->agent_count == 0) {
        printf("\n  \x1b[37mNo agents in workspace.\x1b[0m\n");
        printf("  Spawn some on your PC.\n\n");
    } else if (selected >= 0 && selected < state->agent_count) {
        const AgentInfo *a = &state->agents[selected];
        printf("\n  \x1b[1m%s%s\x1b[0m\n", STATUS_COLOR_CODE(a->status), a->name);
        printf("\n");
        printf("  CLI:     \x1b[36m%s\x1b[0m\n", a->cli);
        printf("  Model:   \x1b[36m%s\x1b[0m\n", a->model[0] ? a->model : "(default)");
        printf("  Role:    \x1b[36m%s\x1b[0m\n", a->role);
        printf("  Status:  %s%s\x1b[0m\n", STATUS_COLOR_CODE(a->status), a->status);
        printf("\n");
        printf("  ----------------------------------\n");
        printf("  \x1b[37mPhase 2 will show output here.\x1b[0m\n");
    }

    printf("\n");
    printf("  \x1b[37m[A] refresh  [Y] url  [X] rescan QR  [START] exit\x1b[0m\n");

    // ── Bottom: agent list ───────────────────────────────────────────────────
    consoleSelect(bottom);
    printf("\x1b[2J\x1b[1;1H");
    printf("\n  \x1b[33mAgents (D-pad to navigate)\x1b[0m\n\n");

    if (state->agent_count == 0) {
        printf("  \x1b[37m(empty)\x1b[0m\n\n");
        printf("  \x1b[37mWaiting for agents...\x1b[0m\n");
    } else {
        for (int i = 0; i < state->agent_count; i++) {
            const AgentInfo *a = &state->agents[i];
            const char *cursor = (i == selected) ? "\x1b[33m>\x1b[0m" : " ";
            const char *color = STATUS_COLOR_CODE(a->status);
            // Status as a colored dot, then name
            printf("  %s %s\xE2\x97\x8F\x1b[0m  %.20s\n", cursor, color, a->name);
        }
    }
}

// ── Main ─────────────────────────────────────────────────────────────────────

int main(void) {
    gfxInitDefault();

    CogRender render = {0};
    bool use_citro2d = cog_render_init(&render);

    PrintConsole top, bottom;
    if (!use_citro2d) {
        consoleInit(GFX_TOP, &top);
        consoleInit(GFX_BOTTOM, &bottom);
    }

    Result http_rc = cog_http_init();
    if (R_FAILED(http_rc)) {
        consoleSelect(&top);
        printf("\n  \x1b[31mFailed to init httpc: %ld\x1b[0m\n", http_rc);
        printf("  Press START to exit.\n");
        while (aptMainLoop()) {
            hidScanInput();
            if (hidKeysDown() & KEY_START) break;
            gspWaitForVBlank();
        }
        gfxExit();
        return 1;
    }

    char url[COG_URL_MAX] = {0};
    bool have_url = cog_config_load(url, sizeof(url));

    CogState state = {0};
    Canvas canvas;
    canvas_init(&canvas);
    bool touching = false;
    touchPosition prev_touch = {0};
    bool did_pan = false;
    u64 touch_start_ms = 0;
    u64 lift_start_ms = 0;
    const u64 LIFT_THRESHOLD_MS = 500;
    const u64 LIFT_ANIM_MS = 150;
    int selected = 0;
    u32 last_poll_frame = 0;
    u32 frame = 0;
    bool dirty = true;
    char status_msg[128] = "Polling...";

    // Setup screen loop: waits for START (exit) or X (QR scan). If the
    // scan succeeds we save the URL and fall through to the main UI.
    while (!have_url && aptMainLoop()) {
        render_setup_screen(&top, &bottom);
        bool exit_requested = false;
        while (aptMainLoop()) {
            hidScanInput();
            u32 sd = hidKeysDown();
            if (sd & KEY_START) { exit_requested = true; break; }
            if (sd & KEY_X) {
                char scanned[COG_URL_MAX] = {0};
                if (cog_qr_scan(scanned, sizeof(scanned))) {
                    if (cog_config_save(scanned)) {
                        strncpy(url, scanned, sizeof(url) - 1);
                        url[sizeof(url) - 1] = '\0';
                        have_url = true;
                    }
                }
                break;  // re-render setup (if still no URL) or fall through
            }
            gspWaitForVBlank();
        }
        if (exit_requested) {
            cog_http_exit();
            gfxExit();
            return 0;
        }
    }

    while (aptMainLoop()) {
        hidScanInput();
        u32 down = hidKeysDown();

        u32 held = hidKeysHeld();
        touchPosition touch;
        if (held & KEY_TOUCH) {
            hidTouchRead(&touch);
            if (!touching) {
                int hit = canvas_hit_test(&canvas, touch.px, touch.py);
                if (hit >= 0) {
                    if (canvas.selected_idx >= 0 && canvas.selected_idx < canvas.card_count)
                        canvas.cards[canvas.selected_idx].selected = false;
                    canvas.selected_idx = hit;
                    canvas.cards[hit].selected = true;
                    touch_start_ms = osGetTime();
                    did_pan = false;
                } else {
                    did_pan = false;
                }
                touching = true;
            }
            else {
                int dx = touch.px - prev_touch.px;
                int dy = touch.py - prev_touch.py;

                // Check for long-press lift trigger
                if (canvas.lifted_idx < 0 && canvas.selected_idx >= 0 && !did_pan) {
                    u64 now = osGetTime();
                    if (now - touch_start_ms >= LIFT_THRESHOLD_MS) {
                        canvas.lifted_idx = canvas.selected_idx;
                        canvas.cards[canvas.lifted_idx].lifted = true;
                        lift_start_ms = now;
                    }
                }

                // Drive lift animation
                if (canvas.lifted_idx >= 0) {
                    u64 now = osGetTime();
                    float t = (now - lift_start_ms) / (float)LIFT_ANIM_MS;
                    if (t > 1.0f) t = 1.0f;
                    canvas.cards[canvas.lifted_idx].lift_scale = 1.0f + 0.2f * ease_out_cubic(t);
                }

                // Drag-move the lifted card
                if (canvas.lifted_idx >= 0 && (dx != 0 || dy != 0)) {
                    Card *lc = &canvas.cards[canvas.lifted_idx];
                    // Screen delta -> world delta via zoom
                    lc->x += (float)dx / canvas.cam_zoom;
                    lc->y += (float)dy / canvas.cam_zoom;
                }

                // Panning only if not lifted and the initial touch wasn't on a card
                if (canvas.lifted_idx < 0 &&
                    (did_pan || canvas_hit_test(&canvas, prev_touch.px, prev_touch.py) < 0)) {
                    if (dx != 0 || dy != 0) {
                        canvas_pan(&canvas, (float)dx, (float)dy);
                        did_pan = true;
                    }
                }
            }
            prev_touch = touch;
        } else if (touching) {
            // Touch release — drop lifted card back to 1.0 scale, sync position
            if (canvas.lifted_idx >= 0 && canvas.lifted_idx < canvas.card_count) {
                Card *lc = &canvas.cards[canvas.lifted_idx];
                lc->lifted = false;
                lc->lift_scale = 1.0f;
                post_card_position(url, lc);
                canvas.lifted_idx = -1;
            }
            touching = false;
            did_pan = false;
        }

        if (down & KEY_START) break;

        if (down & KEY_DUP)   { if (selected > 0) { selected--; dirty = true; } }
        if (down & KEY_DDOWN) { if (selected < state.agent_count - 1) { selected++; dirty = true; } }

        // Force re-poll on A
        if (down & KEY_A) {
            last_poll_frame = 0;
            strncpy(status_msg, "Refreshing...", sizeof(status_msg));
        }

        if (down & KEY_L) canvas_zoom(&canvas, -CANVAS_ZOOM_STEP);
        if (down & KEY_R) canvas_zoom(&canvas, +CANVAS_ZOOM_STEP);

        if (down & KEY_B) {
            if (canvas.selected_idx >= 0 && canvas.selected_idx < canvas.card_count)
                canvas.cards[canvas.selected_idx].selected = false;
            canvas.selected_idx = -1;
        }

        // Y shows the configured URL (debug)
        if (down & KEY_Y) {
            consoleSelect(&top);
            printf("\x1b[2J\x1b[1;1H");
            printf("\n  \x1b[33mConfigured URL:\x1b[0m\n\n  %s\n\n", url);
            printf("  \x1b[37mPress any button to return.\x1b[0m");
            while (aptMainLoop()) {
                hidScanInput();
                u32 d = hidKeysDown();
                if (d) break;
                gspWaitForVBlank();
            }
            dirty = true;
        }

        // X rescans a QR code to update the saved URL
        if (down & KEY_X) {
            char scanned[COG_URL_MAX] = {0};
            if (cog_qr_scan(scanned, sizeof(scanned))) {
                if (cog_config_save(scanned)) {
                    strncpy(url, scanned, sizeof(url) - 1);
                    url[sizeof(url) - 1] = '\0';
                    // Force immediate re-poll with new URL
                    last_poll_frame = 0;
                    state.agent_count = 0;
                    selected = 0;
                    strncpy(status_msg, "URL updated, refreshing...", sizeof(status_msg));
                }
            }
            dirty = true;
        }

        // Poll every 5 sec (60 fps × 5)
        if (frame == 0 || frame - last_poll_frame >= 60 * 5) {
            char state_url[COG_URL_MAX + 16];
            build_state_url(url, state_url, sizeof(state_url));

            char *body = NULL;
            size_t body_len = 0;
            int code = cog_http_get(state_url, &body, &body_len);

            if (code == 200 && body) {
                if (parse_state(body, &state)) {
                    sync_canvas_from_state(&canvas, &state);
                    if (selected >= state.agent_count) selected = state.agent_count - 1;
                    if (selected < 0) selected = 0;
                    snprintf(status_msg, sizeof(status_msg), "OK (%zu bytes)", body_len);
                } else {
                    snprintf(status_msg, sizeof(status_msg), "JSON parse failed");
                }
            } else if (code > 0) {
                snprintf(status_msg, sizeof(status_msg), "HTTP %d", code);
            } else {
                snprintf(status_msg, sizeof(status_msg), "Network error %d", code);
            }
            if (body) free(body);

            last_poll_frame = frame;
            dirty = true;
        }

        if (use_citro2d) {
            // On the first successful state with cards, frame them.
            static bool framed = false;
            if (!framed && canvas.card_count > 0) {
                canvas_frame_all(&canvas);
                framed = true;
            }

            const Card *selected = (canvas.selected_idx >= 0) ? &canvas.cards[canvas.selected_idx] : NULL;

            cog_render_frame_begin(&render);
            cog_render_target_top(&render, THEME_BG_DARK);
            detail_draw(&render, state.project_name, selected,
                        state.agent_count, state.connection_count);
            cog_render_target_bottom(&render, THEME_BG_CANVAS);
            canvas_draw(&render, &canvas);
            cog_render_frame_end(&render);
            dirty = false;
        } else if (dirty) {
            render_main_screens(&top, &bottom, &state, selected, url);
            // Append status line at bottom of bottom screen
            consoleSelect(&bottom);
            printf("\n\n  \x1b[37mPoll: %s\x1b[0m\n", status_msg);
            dirty = false;
        }

        frame++;
        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }

    if (use_citro2d) cog_render_exit(&render);

    cog_http_exit();
    gfxExit();
    return 0;
}
