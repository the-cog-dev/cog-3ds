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
#include "net_recv.h"
#include "keyboard.h"
#include "pin.h"
#include "action_menu.h"
#include "composer.h"
#include "output_viewer.h"
#include "spawn.h"

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

#define MAX_TASKS 32
#define MAX_INFO  32

typedef struct {
    char id[64];
    char title[128];
    char description[256];
    char priority[8];     // "low", "medium", "high"
    char status[16];      // "open", "in_progress", "completed"
    char created_by[64];
    char claimed_by[64];
} TaskInfo;

typedef struct {
    char id[64];
    char from[64];
    char note[256];
    char tags[128];
} InfoInfo;

typedef struct {
    char project_name[64];
    AgentInfo agents[MAX_AGENTS];
    int agent_count;
    int connection_count;
    bool workshop_passcode_set;
    TaskInfo tasks[MAX_TASKS];
    int task_count;
    InfoInfo infos[MAX_INFO];
    int info_count;
    SpawnPresetList presets;
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

    // workshopPasscodeSet
    cJSON *wps = cJSON_GetObjectItemCaseSensitive(root, "workshopPasscodeSet");
    out->workshop_passcode_set = cJSON_IsBool(wps) && cJSON_IsTrue(wps);

    // Pinboard tasks
    out->task_count = 0;
    cJSON *tasks = cJSON_GetObjectItemCaseSensitive(root, "pinboardTasks");
    if (cJSON_IsArray(tasks)) {
        cJSON *task = NULL;
        cJSON_ArrayForEach(task, tasks) {
            if (out->task_count >= MAX_TASKS) break;
            TaskInfo *t = &out->tasks[out->task_count];
            json_get_string(task, "id", t->id, sizeof(t->id));
            json_get_string(task, "title", t->title, sizeof(t->title));
            json_get_string(task, "description", t->description, sizeof(t->description));
            json_get_string(task, "priority", t->priority, sizeof(t->priority));
            json_get_string(task, "status", t->status, sizeof(t->status));
            json_get_string(task, "createdBy", t->created_by, sizeof(t->created_by));
            json_get_string(task, "claimedBy", t->claimed_by, sizeof(t->claimed_by));
            out->task_count++;
        }
    }

    // Info entries
    out->info_count = 0;
    cJSON *infos = cJSON_GetObjectItemCaseSensitive(root, "infoEntries");
    if (cJSON_IsArray(infos)) {
        cJSON *info = NULL;
        cJSON_ArrayForEach(info, infos) {
            if (out->info_count >= MAX_INFO) break;
            InfoInfo *inf = &out->infos[out->info_count];
            json_get_string(info, "id", inf->id, sizeof(inf->id));
            json_get_string(info, "from", inf->from, sizeof(inf->from));
            json_get_string(info, "note", inf->note, sizeof(inf->note));
            // Flatten tags array to comma-separated string
            cJSON *tags_arr = cJSON_GetObjectItemCaseSensitive(info, "tags");
            inf->tags[0] = '\0';
            if (cJSON_IsArray(tags_arr)) {
                int tlen = 0;
                cJSON *tag = NULL;
                cJSON_ArrayForEach(tag, tags_arr) {
                    if (cJSON_IsString(tag) && tag->valuestring) {
                        int slen = (int)strlen(tag->valuestring);
                        if (tlen + slen + 2 < (int)sizeof(inf->tags)) {
                            if (tlen > 0) { inf->tags[tlen++] = ','; inf->tags[tlen++] = ' '; }
                            memcpy(inf->tags + tlen, tag->valuestring, slen);
                            tlen += slen;
                            inf->tags[tlen] = '\0';
                        }
                    }
                }
            }
            out->info_count++;
        }
    }

    // Presets
    out->presets.count = 0;
    cJSON *presets = cJSON_GetObjectItemCaseSensitive(root, "presets");
    if (cJSON_IsArray(presets)) {
        cJSON *preset = NULL;
        cJSON_ArrayForEach(preset, presets) {
            if (out->presets.count >= SPAWN_MAX_PRESETS) break;
            SpawnPreset *sp = &out->presets.presets[out->presets.count];
            json_get_string(preset, "name", sp->name, sizeof(sp->name));
            cJSON *ac = cJSON_GetObjectItemCaseSensitive(preset, "agentCount");
            sp->agent_count = cJSON_IsNumber(ac) ? ac->valueint : 0;
            int ai = 0;
            cJSON *pagents = cJSON_GetObjectItemCaseSensitive(preset, "agents");
            if (cJSON_IsArray(pagents)) {
                cJSON *pa = NULL;
                cJSON_ArrayForEach(pa, pagents) {
                    if (ai >= SPAWN_MAX_AGENTS) break;
                    json_get_string(pa, "name", sp->agents[ai].name, sizeof(sp->agents[ai].name));
                    json_get_string(pa, "cli", sp->agents[ai].cli, sizeof(sp->agents[ai].cli));
                    json_get_string(pa, "role", sp->agents[ai].role, sizeof(sp->agents[ai].role));
                    ai++;
                }
            }
            if (sp->agent_count == 0) sp->agent_count = ai;
            out->presets.count++;
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
        c->card_type = CARD_TYPE_AGENT_CARD;
        c->draggable = true;

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

static void render_setup_screen(CogRender *r, const CogUrlHistory *hist,
                                int sel, float countdown_sec) {
    cog_render_frame_begin(r);
    cog_render_target_top(r, THEME_BG_DARK);
    cog_render_text(r, "The Cog", 150, 20, THEME_FONT_HEADER, THEME_GOLD);

    if (hist->count > 0) {
        cog_render_text(r, "Saved URLs:", 12, 55, THEME_FONT_LABEL, THEME_TEXT_DIMMED);
        for (int i = 0; i < hist->count; i++) {
            float y = 75 + i * 28;
            bool is_sel = (i == sel);
            u32 color = is_sel ? THEME_GOLD : THEME_TEXT_DIMMED;
            const char *arrow = is_sel ? "> " : "  ";
            char line[COG_URL_MAX + 4];
            snprintf(line, sizeof(line), "%s%s", arrow, hist->urls[i]);
            cog_render_text(r, line, 12, y, THEME_FONT_FOOTER, color);
        }
        if (countdown_sec > 0) {
            char cdown[32];
            snprintf(cdown, sizeof(cdown), "Connecting in %.0f...", countdown_sec);
            cog_render_text(r, cdown, 12, 165, THEME_FONT_LABEL, THEME_TEXT_PRIMARY);
        }
        cog_render_text(r, "D-pad to pick  [A] connect  [L] network",
                        12, 190, THEME_FONT_FOOTER, THEME_TEXT_DIMMED);
        cog_render_text(r, "[X] QR  [Y] type  [START] exit",
                        12, 210, THEME_FONT_FOOTER, THEME_TEXT_DIMMED);
    } else {
        cog_render_text(r, "No URLs saved.", 80, 90, THEME_FONT_LABEL, THEME_TEXT_PRIMARY);
        cog_render_text(r, "[Y] type URL  [L] network  [X] QR",
                        30, 130, THEME_FONT_LABEL, THEME_TEXT_DIMMED);
        cog_render_text(r, "[START] exit", 80, 200, THEME_FONT_FOOTER, THEME_TEXT_DIMMED);
    }

    cog_render_target_bottom(r, THEME_BG_CANVAS);
    cog_render_text(r, "The Cog", 100, 80, THEME_FONT_HEADER, THEME_GOLD);
    if (hist->count > 0) {
        cog_render_text(r, hist->urls[sel], 12, 130, THEME_FONT_FOOTER, THEME_GOLD_DIM);
    } else {
        cog_render_text(r, "Add a URL to connect", 60, 130, THEME_FONT_LABEL, THEME_TEXT_DIMMED);
    }
    cog_render_frame_end(r);
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

// ── Background poller ────────────────────────────────────────────────────────
// Runs the blocking HTTP GET in a separate thread so the render loop
// stays responsive even when the server is unreachable.

static char          poll_url[COG_URL_MAX + 16];
static char         *poll_body = NULL;
static size_t        poll_body_len = 0;
static volatile int  poll_code = 0;
static volatile bool poll_done = false;
static volatile bool poll_active = false;
static Thread        poll_thread = NULL;

static void poll_thread_func(void *arg) {
    (void)arg;
    poll_code = cog_http_get(poll_url, &poll_body, &poll_body_len);
    poll_done = true;
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
        if (use_citro2d) {
            while (aptMainLoop()) {
                hidScanInput();
                if (hidKeysDown() & KEY_START) break;
                char msg[64];
                snprintf(msg, sizeof(msg), "httpc init failed: %ld", http_rc);
                cog_render_frame_begin(&render);
                cog_render_target_top(&render, THEME_BG_DARK);
                cog_render_text(&render, msg, 40, 100, THEME_FONT_LABEL, THEME_STATUS_DISCONNECTED);
                cog_render_text(&render, "Press START to exit.", 40, 130, THEME_FONT_FOOTER, THEME_TEXT_DIMMED);
                cog_render_target_bottom(&render, THEME_BG_CANVAS);
                cog_render_frame_end(&render);
            }
            cog_render_exit(&render);
        } else {
            consoleSelect(&top);
            printf("\n  \x1b[31mFailed to init httpc: %ld\x1b[0m\n", http_rc);
            printf("  Press START to exit.\n");
            while (aptMainLoop()) {
                hidScanInput();
                if (hidKeysDown() & KEY_START) break;
                gspWaitForVBlank();
            }
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
    int detail_scroll = 0;
    char status_msg[128] = "Polling...";

    // Setup screen — shows on every boot. If a saved URL exists, auto-
    // advances after 3 seconds. User can press A to skip, X to scan a
    // new QR, or START to exit. SELECT from the main loop returns here.
setup:
    {
        CogUrlHistory hist;
        cog_config_load_history(&hist);
        int url_sel = 0;  // which URL in history is selected

        u64 setup_start = osGetTime();
        const u64 AUTO_ADVANCE_MS = 3000;
        bool advanced = false;
        have_url = hist.count > 0;
        if (have_url) {
            strncpy(url, hist.urls[0], sizeof(url) - 1);
            url[sizeof(url) - 1] = '\0';
        }

        while (aptMainLoop()) {
            hidScanInput();
            u32 sd = hidKeysDown();

            if (sd & KEY_START) {
                if (use_citro2d) cog_render_exit(&render);
                cog_http_exit();
                gfxExit();
                return 0;
            }
            // D-pad to pick from history
            if (sd & KEY_DUP && url_sel > 0) {
                url_sel--;
                strncpy(url, hist.urls[url_sel], sizeof(url) - 1);
                setup_start = osGetTime();
            }
            if (sd & KEY_DDOWN && url_sel < hist.count - 1) {
                url_sel++;
                strncpy(url, hist.urls[url_sel], sizeof(url) - 1);
                setup_start = osGetTime();
            }
            if (sd & KEY_A && have_url) { advanced = true; break; }
            if (sd & KEY_X) {
                char scanned[COG_URL_MAX] = {0};
                if (cog_qr_scan(&render, scanned, sizeof(scanned))) {
                    if (cog_config_save(scanned)) {
                        strncpy(url, scanned, sizeof(url) - 1);
                        url[sizeof(url) - 1] = '\0';
                        have_url = true;
                        cog_config_load_history(&hist);
                        url_sel = 0;
                    }
                }
                setup_start = osGetTime();
            }
            if (sd & KEY_L) {
                char received[COG_URL_MAX] = {0};
                if (cog_net_recv(&render, received, sizeof(received))) {
                    if (cog_config_save(received)) {
                        strncpy(url, received, sizeof(url) - 1);
                        url[sizeof(url) - 1] = '\0';
                        have_url = true;
                        cog_config_load_history(&hist);
                        url_sel = 0;
                    }
                }
                setup_start = osGetTime();
            }
            if (sd & KEY_Y) {
                CogKeyboard kb;
                cog_keyboard_init(&kb, false, have_url ? url : "http://");
                while (aptMainLoop() && cog_keyboard_update(&kb)) {
                    cog_render_frame_begin(&render);
                    cog_render_target_top(&render, THEME_BG_DARK);
                    cog_keyboard_draw_top(&kb, &render, "Type URL", THEME_GOLD);
                    cog_render_target_bottom(&render, THEME_BG_CANVAS);
                    cog_keyboard_draw_bottom(&kb, &render);
                    cog_render_frame_end(&render);
                }
                if (kb.submitted && cog_keyboard_text(&kb)[0]) {
                    if (cog_config_save(cog_keyboard_text(&kb))) {
                        strncpy(url, cog_keyboard_text(&kb), sizeof(url) - 1);
                        url[sizeof(url) - 1] = '\0';
                        have_url = true;
                        cog_config_load_history(&hist);
                        url_sel = 0;
                    }
                }
                setup_start = osGetTime();
            }

            u64 elapsed = osGetTime() - setup_start;
            if (have_url && elapsed >= AUTO_ADVANCE_MS) { advanced = true; break; }

            float remaining = have_url ? (AUTO_ADVANCE_MS - elapsed) / 1000.0f : 0;
            if (use_citro2d) {
                render_setup_screen(&render, &hist, url_sel, remaining);
            } else {
                if (elapsed < 100) {
                    consoleSelect(&top);
                    printf("\x1b[2J\x1b[1;1H\n");
                    for (int i = 0; i < hist.count; i++) {
                        printf("  %s %s\n", i == url_sel ? ">" : " ", hist.urls[i]);
                    }
                    if (have_url) printf("\n  Auto-connecting in 3s...\n");
                    else printf("  No URL saved.\n");
                }
                gspWaitForVBlank();
            }
        }
        if (!advanced && !have_url) {
            if (use_citro2d) cog_render_exit(&render);
            cog_http_exit();
            gfxExit();
            return 0;
        }
        last_poll_frame = 0;
        frame = 0;
        dirty = true;
    }

    // ── PIN gate ────────────────────────────────────────────────────────────
    bool workshop_verified = false;
    {
        build_state_url(url, poll_url, sizeof(poll_url));
        char *init_body = NULL;
        size_t init_len = 0;
        int init_code = cog_http_get(poll_url, &init_body, &init_len);
        if (init_code == 200 && init_body) {
            if (parse_state(init_body, &state)) {
                sync_canvas_from_state(&canvas, &state);
                if (canvas.card_count > 0) canvas_frame_all(&canvas);
            }
        }
        if (init_body) free(init_body);

        if (state.workshop_passcode_set) {
            PinResult pr = cog_pin_entry(&render, url);
            if (pr == PIN_RESULT_OK || pr == PIN_RESULT_SKIP) {
                workshop_verified = true;
            } else {
                goto setup;
            }
        } else {
            workshop_verified = true;
        }
    }
    (void)workshop_verified;

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
                if (canvas.lifted_idx < 0 && canvas.selected_idx >= 0 && !did_pan && canvas.cards[canvas.selected_idx].draggable) {
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

        CanvasNavDir nav = (CanvasNavDir)-1;
        if (down & KEY_DUP)    nav = CANVAS_NAV_UP;
        if (down & KEY_DDOWN)  nav = CANVAS_NAV_DOWN;
        if (down & KEY_DLEFT)  nav = CANVAS_NAV_LEFT;
        if (down & KEY_DRIGHT) nav = CANVAS_NAV_RIGHT;
        if ((int)nav >= 0) {
            int ni = canvas_nav_nearest(&canvas, nav);
            if (ni >= 0) {
                if (canvas.selected_idx >= 0 && canvas.selected_idx < canvas.card_count)
                    canvas.cards[canvas.selected_idx].selected = false;
                canvas.selected_idx = ni;
                canvas.cards[ni].selected = true;
                detail_scroll = 0;
            }
        }

        // D-pad scroll for panel detail views
        if (canvas.selected_idx >= 0 && canvas.selected_idx < canvas.card_count) {
            Card *sc = &canvas.cards[canvas.selected_idx];
            if (sc->card_type == CARD_TYPE_PINBOARD_CARD || sc->card_type == CARD_TYPE_INFO_CARD) {
                if (down & KEY_DUP && detail_scroll > 0) detail_scroll--;
                if (down & KEY_DDOWN) detail_scroll++;
            }
        }

        if (down & KEY_A) {
            if (canvas.selected_idx >= 0 && canvas.selected_idx < canvas.card_count) {
                Card *sel_card = &canvas.cards[canvas.selected_idx];
                CardType ct;
                if (sel_card->card_type == CARD_TYPE_PINBOARD_CARD) ct = CARD_TYPE_PINBOARD;
                else if (sel_card->card_type == CARD_TYPE_INFO_CARD) ct = CARD_TYPE_INFO;
                else ct = CARD_TYPE_AGENT;

                MenuAction action = cog_action_menu(&render, ct, sel_card->name);

                switch (action) {
                case ACTION_MESSAGE: {
                    char prompt[128];
                    snprintf(prompt, sizeof(prompt), "Message to: %s", sel_card->name);
                    ComposerResult msg = cog_composer_run(&render, prompt, NULL);
                    if (msg.submitted && msg.text[0]) {
                        char msg_url[512];
                        snprintf(msg_url, sizeof(msg_url), "%smessage", url);
                        char body[COMPOSER_MAX_LEN + 128];
                        char escaped[COMPOSER_MAX_LEN];
                        int ei = 0;
                        for (int i = 0; msg.text[i] && ei < COMPOSER_MAX_LEN - 2; i++) {
                            if (msg.text[i] == '"' || msg.text[i] == '\\') escaped[ei++] = '\\';
                            if (msg.text[i] == '\n') { escaped[ei++] = '\\'; escaped[ei++] = 'n'; continue; }
                            escaped[ei++] = msg.text[i];
                        }
                        escaped[ei] = '\0';
                        snprintf(body, sizeof(body), "{\"to\":\"%s\",\"text\":\"%s\"}", sel_card->name, escaped);
                        char *resp = NULL; size_t rlen = 0;
                        cog_http_post_json(msg_url, body, &resp, &rlen);
                        if (resp) free(resp);
                        strncpy(status_msg, "Message sent!", sizeof(status_msg));
                    }
                    break;
                }
                case ACTION_VIEW_OUTPUT:
                    cog_output_viewer(&render, url, sel_card->id, sel_card->name);
                    break;
                case ACTION_KILL: {
                    bool confirmed = false;
                    while (aptMainLoop()) {
                        hidScanInput();
                        u32 ckd = hidKeysDown();
                        if (ckd & KEY_A) { confirmed = true; break; }
                        if (ckd & KEY_B) break;
                        cog_render_frame_begin(&render);
                        cog_render_target_top(&render, THEME_BG_DARK);
                        char kmsg[128];
                        snprintf(kmsg, sizeof(kmsg), "Kill %s?", sel_card->name);
                        cog_render_text(&render, kmsg, 120, 80, THEME_FONT_HEADER, THEME_STATUS_DISCONNECTED);
                        cog_render_text(&render, "[A] yes  [B] no", 130, 130, THEME_FONT_LABEL, THEME_TEXT_DIMMED);
                        cog_render_target_bottom(&render, THEME_BG_CANVAS);
                        cog_render_frame_end(&render);
                    }
                    if (confirmed) {
                        char kill_url[512];
                        snprintf(kill_url, sizeof(kill_url), "%sworkshop/kill/%s", url, sel_card->id);
                        char *resp = NULL; size_t rlen = 0;
                        cog_http_post_json(kill_url, "{}", &resp, &rlen);
                        if (resp) free(resp);
                        strncpy(status_msg, "Agent killed", sizeof(status_msg));
                        last_poll_frame = 0;
                    }
                    break;
                }
                case ACTION_CREATE_TASK: {
                    ComposerResult title = cog_composer_run(&render, "Task title:", NULL);
                    if (title.submitted && title.text[0]) {
                        ComposerResult desc = cog_composer_run(&render, "Task description:", NULL);
                        if (desc.submitted) {
                            char task_url[512];
                            snprintf(task_url, sizeof(task_url), "%stask", url);
                            char body[1024];
                            snprintf(body, sizeof(body),
                                     "{\"title\":\"%s\",\"description\":\"%s\",\"priority\":\"medium\"}",
                                     title.text, desc.text);
                            char *resp = NULL; size_t rlen = 0;
                            cog_http_post_json(task_url, body, &resp, &rlen);
                            if (resp) free(resp);
                            strncpy(status_msg, "Task created!", sizeof(status_msg));
                            last_poll_frame = 0;
                        }
                    }
                    break;
                }
                case ACTION_CLAIM_TASK: {
                    for (int i = 0; i < state.task_count; i++) {
                        if (strcmp(state.tasks[i].status, "open") == 0) {
                            char claim_url[512];
                            snprintf(claim_url, sizeof(claim_url),
                                     "%spinboard/tasks/%s/claim", url, state.tasks[i].id);
                            char *resp = NULL; size_t rlen = 0;
                            cog_http_post_json(claim_url, "{\"from\":\"3ds-user\"}", &resp, &rlen);
                            if (resp) free(resp);
                            strncpy(status_msg, "Task claimed!", sizeof(status_msg));
                            last_poll_frame = 0;
                            break;
                        }
                    }
                    break;
                }
                case ACTION_COMPLETE_TASK: {
                    ComposerResult result_text = cog_composer_run(&render, "Result:", NULL);
                    if (result_text.submitted) {
                        for (int i = 0; i < state.task_count; i++) {
                            if (strcmp(state.tasks[i].status, "in_progress") == 0) {
                                char comp_url[512];
                                snprintf(comp_url, sizeof(comp_url),
                                         "%spinboard/tasks/%s/complete", url, state.tasks[i].id);
                                char body[1024];
                                snprintf(body, sizeof(body),
                                         "{\"from\":\"3ds-user\",\"result\":\"%s\"}", result_text.text);
                                char *resp = NULL; size_t rlen = 0;
                                cog_http_post_json(comp_url, body, &resp, &rlen);
                                if (resp) free(resp);
                                strncpy(status_msg, "Task completed!", sizeof(status_msg));
                                last_poll_frame = 0;
                                break;
                            }
                        }
                    }
                    break;
                }
                case ACTION_ABANDON_TASK: {
                    for (int i = 0; i < state.task_count; i++) {
                        if (strcmp(state.tasks[i].status, "in_progress") == 0) {
                            char abn_url[512];
                            snprintf(abn_url, sizeof(abn_url),
                                     "%spinboard/tasks/%s/abandon", url, state.tasks[i].id);
                            char *resp = NULL; size_t rlen = 0;
                            cog_http_post_json(abn_url, "{}", &resp, &rlen);
                            if (resp) free(resp);
                            strncpy(status_msg, "Task abandoned", sizeof(status_msg));
                            last_poll_frame = 0;
                            break;
                        }
                    }
                    break;
                }
                case ACTION_CREATE_NOTE: {
                    ComposerResult note = cog_composer_run(&render, "Info note:", NULL);
                    if (note.submitted && note.text[0]) {
                        char info_url[512];
                        snprintf(info_url, sizeof(info_url), "%sinfo", url);
                        char body[1024];
                        snprintf(body, sizeof(body),
                                 "{\"from\":\"3ds-user\",\"note\":\"%s\"}", note.text);
                        char *resp = NULL; size_t rlen = 0;
                        cog_http_post_json(info_url, body, &resp, &rlen);
                        if (resp) free(resp);
                        strncpy(status_msg, "Note posted!", sizeof(status_msg));
                        last_poll_frame = 0;
                    }
                    break;
                }
                case ACTION_DELETE_NOTE:
                    // Will be implemented in Task 10 with POST workaround
                    strncpy(status_msg, "Delete not yet available", sizeof(status_msg));
                    break;
                case ACTION_SPAWN:
                case ACTION_NONE:
                    break;
                }
            } else {
                // Nothing selected — spawn picker
                cog_spawn_picker(&render, &state.presets, url);
                last_poll_frame = 0;
            }
        }

        if (down & KEY_L) canvas_zoom(&canvas, -CANVAS_ZOOM_STEP);
        if (down & KEY_R) canvas_zoom(&canvas, +CANVAS_ZOOM_STEP);

        if (down & KEY_B) {
            if (canvas.selected_idx >= 0 && canvas.selected_idx < canvas.card_count)
                canvas.cards[canvas.selected_idx].selected = false;
            canvas.selected_idx = -1;
        }

        // SELECT returns to the setup screen to change URL / rescan QR
        if (down & KEY_SELECT) goto setup;

        // Y shows the configured URL (debug)
        if (down & KEY_Y) {
            if (use_citro2d) {
                while (aptMainLoop()) {
                    hidScanInput();
                    if (hidKeysDown()) break;
                    cog_render_frame_begin(&render);
                    cog_render_target_top(&render, THEME_BG_DARK);
                    cog_render_text(&render, "Configured URL:", 12, 30, THEME_FONT_HEADER, THEME_GOLD);
                    cog_render_text(&render, url, 12, 70, THEME_FONT_FOOTER, THEME_TEXT_PRIMARY);
                    cog_render_text(&render, "Press any button to return.", 12, 200, THEME_FONT_FOOTER, THEME_TEXT_DIMMED);
                    cog_render_target_bottom(&render, THEME_BG_CANVAS);
                    cog_render_frame_end(&render);
                }
            } else {
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
            }
            dirty = true;
        }

        // X rescans a QR code to update the saved URL
        if (down & KEY_X) {
            char scanned[COG_URL_MAX] = {0};
            if (cog_qr_scan(&render, scanned, sizeof(scanned))) {
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

        // Poll every 5 sec. Runs in a background thread so the main
        // loop never blocks on network — UI stays responsive even when
        // the server is unreachable.
        if (!poll_active && frame > 0 &&
            (frame == 1 || frame - last_poll_frame >= 60 * 5)) {
            build_state_url(url, poll_url, sizeof(poll_url));
            poll_body = NULL;
            poll_body_len = 0;
            poll_code = 0;
            poll_done = false;
            poll_active = true;
            poll_thread = threadCreate(poll_thread_func, NULL,
                                       0x10000, 0x30, -1, false);
            if (!poll_thread) {
                poll_active = false;
                snprintf(status_msg, sizeof(status_msg), "Thread error");
            }
        }
        if (poll_active && poll_done) {
            threadJoin(poll_thread, U64_MAX);
            threadFree(poll_thread);
            poll_thread = NULL;
            poll_active = false;

            if (poll_code == 200 && poll_body) {
                if (parse_state(poll_body, &state)) {
                    sync_canvas_from_state(&canvas, &state);
                    canvas_add_panel_cards(&canvas, state.task_count, state.info_count);
                    if (selected >= state.agent_count) selected = state.agent_count - 1;
                    if (selected < 0) selected = 0;
                    snprintf(status_msg, sizeof(status_msg), "OK (%zu bytes)", poll_body_len);
                } else {
                    snprintf(status_msg, sizeof(status_msg), "JSON parse failed");
                }
            } else if (poll_code > 0) {
                snprintf(status_msg, sizeof(status_msg), "HTTP %d", poll_code);
            } else {
                snprintf(status_msg, sizeof(status_msg), "Offline (err %d)", poll_code);
            }
            if (poll_body) { free(poll_body); poll_body = NULL; }

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
                        state.agent_count, state.connection_count,
                        state.tasks, state.task_count,
                        state.infos, state.info_count,
                        detail_scroll);
            cog_render_target_bottom(&render, THEME_BG_CANVAS);
            canvas_draw(&render, &canvas);
            // Status bar at bottom of canvas
            cog_render_rect(0, 225, 320, 15, THEME_BG_DARK);
            cog_render_text(&render, status_msg, 4, 227,
                            THEME_FONT_FOOTER, THEME_TEXT_DIMMED);
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
        if (!use_citro2d) {
            gfxFlushBuffers();
            gfxSwapBuffers();
            gspWaitForVBlank();
        }
    }

    if (use_citro2d) cog_render_exit(&render);

    cog_http_exit();
    gfxExit();
    return 0;
}
