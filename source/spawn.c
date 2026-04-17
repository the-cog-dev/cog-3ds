#include "spawn.h"
#include "theme.h"
#include "http.h"

#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Screen dimensions
#define TOP_W    400
#define TOP_H    240
#define BOT_W    320
#define BOT_H    240

// Layout constants
#define HEADER_H     28
#define FOOTER_H     20
#define ITEM_H       28
#define LIST_START_Y 30
#define BADGE_W      28

// Flash duration: ~45 vblanks @ 60fps ≈ 0.75s
#define FLASH_FRAMES 45
// Empty state display: ~120 vblanks ≈ 2s
#define EMPTY_FRAMES 120

// ── HTTP helper ──────────────────────────────────────────────────────────────

// POST a single agent to the workshop/spawn endpoint.
// Returns true if the server responded with HTTP 200.
static bool spawn_agent(const char *base_url, const SpawnAgent *agent) {
    char url[512];
    snprintf(url, sizeof(url), "%sworkshop/spawn", base_url);

    char body[512];
    snprintf(body, sizeof(body),
             "{\"name\":\"%s\",\"cli\":\"%s\",\"role\":\"%s\","
             "\"ceoNotes\":\"\",\"autoMode\":false}",
             agent->name, agent->cli, agent->role);

    char *resp = NULL;
    size_t resp_len = 0;
    int status = cog_http_post_json(url, body, &resp, &resp_len);
    if (resp) free(resp);
    return status == 200;
}

// ── Drawing ──────────────────────────────────────────────────────────────────

// Top screen: selected preset detail view.
static void draw_picker_top(CogRender *r, const SpawnPresetList *presets, int sel) {
    // Header bar
    cog_render_rect(0, 0, TOP_W, HEADER_H, THEME_BG_CANVAS);
    cog_render_rect(0, HEADER_H - 1, TOP_W, 1, THEME_DIVIDER);
    cog_render_text(r, "Spawn Agents", 12, 6, THEME_FONT_HEADER, THEME_GOLD);

    if (presets->count == 0) {
        // Empty state message
        cog_render_text(r, "No presets saved", 12, HEADER_H + 30,
                        THEME_FONT_LABEL, THEME_TEXT_PRIMARY);
        cog_render_text(r, "Save presets from the desktop app.",
                        12, HEADER_H + 54, THEME_FONT_FOOTER, THEME_TEXT_DIMMED);
        return;
    }

    const SpawnPreset *p = &presets->presets[sel];

    // Preset name
    float y = HEADER_H + 10;
    cog_render_text(r, p->name, 12, y, THEME_FONT_HEADER, THEME_TEXT_PRIMARY);
    y += 26;

    // Agent count badge line
    char count_str[32];
    snprintf(count_str, sizeof(count_str), "%d agent%s",
             p->agent_count, p->agent_count == 1 ? "" : "s");
    cog_render_text(r, count_str, 12, y, THEME_FONT_LABEL, THEME_GOLD_DIM);
    y += 22;

    // Divider
    cog_render_rect(12, y, TOP_W - 24, 1, THEME_DIVIDER);
    y += 10;

    // Agent list
    for (int i = 0; i < p->agent_count && y + ITEM_H <= TOP_H - FOOTER_H; i++) {
        const SpawnAgent *a = &p->agents[i];
        cog_render_text(r, a->name, 12, y, THEME_FONT_LABEL, THEME_TEXT_PRIMARY);

        // cli / role on the right
        char meta[52];
        snprintf(meta, sizeof(meta), "%s  %s", a->cli, a->role);
        cog_render_text_right(r, meta, TOP_W - 12, y + 2,
                              THEME_FONT_FOOTER, THEME_TEXT_DIMMED);
        y += ITEM_H;
    }

    // Footer hint
    float fy = TOP_H - FOOTER_H;
    cog_render_rect(0, fy, TOP_W, 1, THEME_DIVIDER);
    cog_render_text(r, "[A] spawn  [B] cancel  D-pad scroll",
                    12, fy + 4, THEME_FONT_FOOTER, THEME_TEXT_DIMMED);
}

// Bottom screen: scrollable preset list.
static void draw_picker_bottom(CogRender *r, const SpawnPresetList *presets,
                               int sel, int scroll_offset) {
    // Header bar
    cog_render_rect(0, 0, BOT_W, HEADER_H, THEME_BG_CANVAS);
    cog_render_rect(0, HEADER_H - 1, BOT_W, 1, THEME_DIVIDER);
    cog_render_text(r, "Presets", 12, 6, THEME_FONT_HEADER, THEME_GOLD);

    if (presets->count == 0) {
        cog_render_text(r, "No presets", 80, BOT_H / 2 - 8,
                        THEME_FONT_LABEL, THEME_TEXT_DIMMED);
        return;
    }

    // How many items fit in the list area
    int list_area_h = BOT_H - HEADER_H;
    int visible = list_area_h / ITEM_H;

    for (int i = 0; i < visible; i++) {
        int idx = scroll_offset + i;
        if (idx >= presets->count) break;

        float iy = HEADER_H + i * ITEM_H;
        const SpawnPreset *p = &presets->presets[idx];
        bool is_sel = (idx == sel);

        // Highlight selected item
        if (is_sel) {
            cog_render_rounded_rect(4, iy + 2, BOT_W - 8, ITEM_H - 4,
                                    4.0f, THEME_GOLD_DIM);
        }

        // Preset name
        u32 name_color = is_sel ? THEME_TEXT_PRIMARY : THEME_TEXT_DIMMED;
        cog_render_text(r, p->name, 12, iy + 6, THEME_FONT_LABEL, name_color);

        // Agent count badge on the right
        char badge[8];
        snprintf(badge, sizeof(badge), "%d", p->agent_count);
        cog_render_text_right(r, badge, BOT_W - 10, iy + 6,
                              THEME_FONT_FOOTER, THEME_GOLD);
    }
}

// Flash message drawn over both screens (top screen only for simplicity).
static void draw_flash(CogRender *r, const char *msg) {
    // Semi-transparent overlay box on top screen centre
    float bw = 220, bh = 40;
    float bx = (TOP_W - bw) / 2.0f;
    float by = (TOP_H - bh) / 2.0f;
    cog_render_rounded_rect(bx, by, bw, bh, 6.0f, THEME_GOLD_DIM);
    cog_render_text(r, msg, bx + 12, by + 10, THEME_FONT_LABEL, THEME_TEXT_PRIMARY);
}

// Progress message on top screen during spawning.
static void draw_progress(CogRender *r, int current, int total) {
    char msg[48];
    snprintf(msg, sizeof(msg), "Spawning %d/%d...", current, total);

    float bw = 220, bh = 40;
    float bx = (TOP_W - bw) / 2.0f;
    float by = (TOP_H - bh) / 2.0f;
    cog_render_rounded_rect(bx, by, bw, bh, 6.0f, THEME_GOLD_DIM);
    cog_render_text(r, msg, bx + 12, by + 10, THEME_FONT_LABEL, THEME_TEXT_PRIMARY);
}

// ── Main entry point ─────────────────────────────────────────────────────────

bool cog_spawn_picker(CogRender *r, const SpawnPresetList *presets,
                      const char *base_url) {
    // Empty-state: display message for ~2s then return false
    if (presets->count == 0) {
        for (int f = 0; f < EMPTY_FRAMES; f++) {
            cog_render_frame_begin(r);
            cog_render_target_top(r, THEME_BG_DARK);
            draw_picker_top(r, presets, 0);
            cog_render_target_bottom(r, THEME_BG_DARK);
            draw_picker_bottom(r, presets, 0, 0);
            cog_render_frame_end(r);
            gspWaitForVBlank();

            // Allow B to exit early
            hidScanInput();
            if (hidKeysDown() & KEY_B) break;
        }
        return false;
    }

    int sel = 0;
    int scroll_offset = 0;

    // Visible rows on bottom screen list area
    int list_area_h = BOT_H - HEADER_H;
    int visible = list_area_h / ITEM_H;

    while (aptMainLoop()) {
        hidScanInput();
        u32 kdown = hidKeysDown();

        if (kdown & KEY_B) {
            return false;
        }

        if (kdown & KEY_DUP) {
            if (sel > 0) {
                sel--;
                // Scroll up if needed
                if (sel < scroll_offset) scroll_offset = sel;
            }
        }

        if (kdown & KEY_DDOWN) {
            if (sel < presets->count - 1) {
                sel++;
                // Scroll down if needed
                if (sel >= scroll_offset + visible)
                    scroll_offset = sel - visible + 1;
            }
        }

        // Touch selection on bottom screen
        if (kdown & KEY_TOUCH) {
            touchPosition touch;
            hidTouchRead(&touch);
            int touched_idx = scroll_offset + (touch.py - HEADER_H) / ITEM_H;
            if (touch.py >= HEADER_H && touched_idx >= 0 &&
                touched_idx < presets->count) {
                sel = touched_idx;
                if (sel < scroll_offset) scroll_offset = sel;
                if (sel >= scroll_offset + visible)
                    scroll_offset = sel - visible + 1;
            }
        }

        if (kdown & KEY_A) {
            // Spawn all agents in the selected preset
            const SpawnPreset *p = &presets->presets[sel];
            int spawned = 0;

            for (int i = 0; i < p->agent_count; i++) {
                // Draw progress frame
                cog_render_frame_begin(r);
                cog_render_target_top(r, THEME_BG_DARK);
                draw_picker_top(r, presets, sel);
                draw_progress(r, i + 1, p->agent_count);
                cog_render_target_bottom(r, THEME_BG_DARK);
                draw_picker_bottom(r, presets, sel, scroll_offset);
                cog_render_frame_end(r);

                if (spawn_agent(base_url, &p->agents[i])) {
                    spawned++;
                }
            }

            // Success flash for ~0.75s
            char flash_msg[48];
            snprintf(flash_msg, sizeof(flash_msg), "Spawned %d agent%s!",
                     spawned, spawned == 1 ? "" : "s");

            for (int f = 0; f < FLASH_FRAMES; f++) {
                cog_render_frame_begin(r);
                cog_render_target_top(r, THEME_BG_DARK);
                draw_picker_top(r, presets, sel);
                draw_flash(r, flash_msg);
                cog_render_target_bottom(r, THEME_BG_DARK);
                draw_picker_bottom(r, presets, sel, scroll_offset);
                cog_render_frame_end(r);
                gspWaitForVBlank();
            }

            return spawned > 0;
        }

        // Normal render
        cog_render_frame_begin(r);
        cog_render_target_top(r, THEME_BG_DARK);
        draw_picker_top(r, presets, sel);
        cog_render_target_bottom(r, THEME_BG_DARK);
        draw_picker_bottom(r, presets, sel, scroll_offset);
        cog_render_frame_end(r);
    }

    return false;
}
