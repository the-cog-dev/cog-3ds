// PIN entry numpad module for workshop authentication.
// Renders a 3x4 numpad on the bottom screen and a 4-dot progress
// display on the top screen. Auto-submits on the 4th digit.

#include "pin.h"
#include "theme.h"
#include "http.h"
#include "cJSON.h"

#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ─── Layout constants ────────────────────────────────────────────────────────

// Bottom screen is 320x240
#define SCREEN_W       320.0f
#define SCREEN_H       240.0f

// Numpad: 3 columns, 4 rows, each button 60x40, 8px padding all around
#define BTN_W          60.0f
#define BTN_H          40.0f
#define BTN_PAD        8.0f

// 3 columns of (60 + 8) = 68 each, minus trailing pad = 3*60 + 2*8 = 196
// Center in 320px: (320 - 196) / 2 = 62
#define PAD_ORIGIN_X   62.0f
// 4 rows of (40 + 8) = 48 each = 192; start at y=40 so it sits nicely
#define PAD_ORIGIN_Y   40.0f

// Dot display on top screen (400x240)
#define TOP_W          400.0f
#define DOT_RADIUS     8.0f
#define DOT_INNER      6.0f   // inner circle radius for ring effect
#define DOT_Y          100.0f
// 4 dots with 32px spacing: total width = 3*32 + 2*8 = 128; center at 200
#define DOT_SPACING    32.0f
#define DOT_X_START    (TOP_W / 2.0f - 1.5f * DOT_SPACING)

// ─── Button layout ───────────────────────────────────────────────────────────
// Row 0: 1 2 3
// Row 1: 4 5 6
// Row 2: 7 8 9
// Row 3: < 0 X

static const char NUMPAD[4][3] = {
    {'1', '2', '3'},
    {'4', '5', '6'},
    {'7', '8', '9'},
    {'<', '0', 'X'},
};

// ─── Internal helpers ─────────────────────────────────────────────────────────

// Returns screen (x, y) for the top-left of button at (col, row).
static void btn_pos(int col, int row, float *ox, float *oy) {
    *ox = PAD_ORIGIN_X + col * (BTN_W + BTN_PAD);
    *oy = PAD_ORIGIN_Y + row * (BTN_H + BTN_PAD);
}

// Render both screens for the current state.
static void draw_pin_screen(CogRender *r,
                            const char *digits,    // current digits, '\0'-terminated, len 0-4
                            int digit_count,
                            const char *message,   // status line, may be NULL
                            u32 msg_color,
                            int attempts_left,     // -1 = unknown / not yet failed
                            bool locked_out,
                            int lockout_secs)
{
    // ── TOP SCREEN ──────────────────────────────────────────────────────────
    cog_render_target_top(r, THEME_BG_DARK);

    // Title
    cog_render_text(r, "Workshop PIN", 12.0f, 20.0f,
                    THEME_FONT_HEADER, THEME_TEXT_PRIMARY);

    // 4 dots
    for (int i = 0; i < 4; i++) {
        float dx = DOT_X_START + i * DOT_SPACING;
        float dy = DOT_Y;
        if (i < digit_count) {
            // Filled dot: solid gold
            C2D_DrawCircleSolid(dx, dy, 0.0f, DOT_RADIUS, THEME_GOLD);
        } else {
            // Ring: outer dim circle, then inner dark circle punched out
            C2D_DrawCircleSolid(dx, dy, 0.0f, DOT_RADIUS, THEME_TEXT_DIMMED);
            C2D_DrawCircleSolid(dx, dy, 0.0f, DOT_INNER,  THEME_BG_DARK);
        }
    }

    // Status message
    if (message && message[0]) {
        float mw = cog_render_text_width(r, message, THEME_FONT_LABEL);
        cog_render_text(r, message,
                        (TOP_W - mw) / 2.0f, 130.0f,
                        THEME_FONT_LABEL, msg_color);
    }

    // Attempt counter (only when we've had at least one failure)
    if (attempts_left >= 0 && attempts_left < 5 && !locked_out) {
        char att_buf[32];
        snprintf(att_buf, sizeof(att_buf), "%d attempt%s left",
                 attempts_left, attempts_left == 1 ? "" : "s");
        float aw = cog_render_text_width(r, att_buf, THEME_FONT_FOOTER);
        cog_render_text(r, att_buf,
                        (TOP_W - aw) / 2.0f, 155.0f,
                        THEME_FONT_FOOTER, THEME_TEXT_DIMMED);
    }

    // Lockout countdown
    if (locked_out) {
        char lock_buf[32];
        snprintf(lock_buf, sizeof(lock_buf), "Wait %d second%s",
                 lockout_secs, lockout_secs == 1 ? "" : "s");
        float lw = cog_render_text_width(r, lock_buf, THEME_FONT_FOOTER);
        cog_render_text(r, lock_buf,
                        (TOP_W - lw) / 2.0f, 155.0f,
                        THEME_FONT_FOOTER, THEME_TEXT_DIMMED);
    }

    // Hint
    cog_render_text(r, "[B] cancel",
                    12.0f, 220.0f, THEME_FONT_FOOTER, THEME_TEXT_DIMMED);

    // ── BOTTOM SCREEN ───────────────────────────────────────────────────────
    cog_render_target_bottom(r, THEME_BG_DARK);

    if (locked_out) {
        // Hide numpad, show lockout message centered
        const char *lk1 = "Locked out";
        const char *lk2 = "Wait 60 seconds";
        float w1 = cog_render_text_width(r, lk1, THEME_FONT_HEADER);
        float w2 = cog_render_text_width(r, lk2, THEME_FONT_LABEL);
        cog_render_text(r, lk1, (SCREEN_W - w1) / 2.0f, 90.0f,
                        THEME_FONT_HEADER, THEME_STATUS_DISCONNECTED);
        cog_render_text(r, lk2, (SCREEN_W - w2) / 2.0f, 120.0f,
                        THEME_FONT_LABEL, THEME_TEXT_DIMMED);
        return;
    }

    // Draw numpad buttons
    for (int row = 0; row < 4; row++) {
        for (int col = 0; col < 3; col++) {
            float bx, by;
            btn_pos(col, row, &bx, &by);

            // Button background
            cog_render_rounded_rect(bx, by, BTN_W, BTN_H, 5.0f, THEME_DIVIDER);

            // Label
            char label[2] = { NUMPAD[row][col], '\0' };
            // Use a readable label for special keys
            const char *lbl = label;
            if (NUMPAD[row][col] == '<') lbl = "<";
            else if (NUMPAD[row][col] == 'X') lbl = "X";

            float lw = cog_render_text_width(r, lbl, THEME_FONT_LABEL);
            float lh = 12.0f; // approximate glyph height at FONT_LABEL scale
            cog_render_text(r, lbl,
                            bx + (BTN_W - lw) / 2.0f,
                            by + (BTN_H - lh) / 2.0f,
                            THEME_FONT_LABEL, THEME_TEXT_PRIMARY);
        }
    }
}

// Returns the character for the tapped numpad button, or '\0' if none.
static char check_numpad_touch(void) {
    touchPosition tp;
    hidTouchRead(&tp);

    for (int row = 0; row < 4; row++) {
        for (int col = 0; col < 3; col++) {
            float bx, by;
            btn_pos(col, row, &bx, &by);
            if (tp.px >= (int)bx && tp.px < (int)(bx + BTN_W) &&
                tp.py >= (int)by && tp.py < (int)(by + BTN_H)) {
                return NUMPAD[row][col];
            }
        }
    }
    return '\0';
}

// POSTs {pin} to {base_url}workshop/verify.
// Returns PIN_RESULT_OK on success, PIN_RESULT_SKIP on 400 (no passcode),
// PIN_RESULT_CANCEL on network error.
// out_attempts_left is set to -1 if not present in the response.
static PinResult verify_pin(const char *base_url, const char *pin_str,
                             int *out_attempts_left, bool *out_locked)
{
    *out_attempts_left = -1;
    *out_locked = false;

    // Build URL: base_url already ends with '/' per convention in this project
    char url[256];
    snprintf(url, sizeof(url), "%sworkshop/verify", base_url);

    // Build JSON body
    char body[32];
    snprintf(body, sizeof(body), "{\"pin\":\"%s\"}", pin_str);

    char *resp_body = NULL;
    size_t resp_len = 0;
    int status = cog_http_post_json(url, body, &resp_body, &resp_len);

    if (status == 400) {
        if (resp_body) free(resp_body);
        return PIN_RESULT_SKIP;
    }

    if (status != 200 || !resp_body) {
        if (resp_body) free(resp_body);
        return PIN_RESULT_CANCEL;
    }

    cJSON *root = cJSON_Parse(resp_body);
    free(resp_body);

    if (!root) return PIN_RESULT_CANCEL;

    // {"verified": true/false, "attemptsLeft": N, "error": "..."}
    cJSON *verified  = cJSON_GetObjectItemCaseSensitive(root, "verified");
    cJSON *attempts  = cJSON_GetObjectItemCaseSensitive(root, "attemptsLeft");
    cJSON *error_msg = cJSON_GetObjectItemCaseSensitive(root, "error");

    bool ok = cJSON_IsTrue(verified);

    if (cJSON_IsNumber(attempts)) {
        *out_attempts_left = attempts->valueint;
    }

    // Lockout detection: attemptsLeft == 0 or error contains "Locked"
    if (*out_attempts_left == 0) {
        *out_locked = true;
    }
    if (cJSON_IsString(error_msg) && error_msg->valuestring) {
        if (strstr(error_msg->valuestring, "Locked") ||
            strstr(error_msg->valuestring, "locked")) {
            *out_locked = true;
        }
    }

    cJSON_Delete(root);
    return ok ? PIN_RESULT_OK : PIN_RESULT_CANCEL;
}

// ─── Public entry point ───────────────────────────────────────────────────────

PinResult cog_pin_entry(CogRender *r, const char *base_url) {
    char digits[5];   // up to 4 digits + '\0'
    int  digit_count = 0;
    memset(digits, 0, sizeof(digits));

    const char *message   = NULL;
    u32         msg_color = THEME_TEXT_DIMMED;
    int  attempts_left    = -1;
    bool locked_out       = false;
    int  lockout_secs     = 60;

    // Countdown timer state
    u64 lockout_start = 0;  // osGetTime() at lockout begin

    while (aptMainLoop()) {
        // ── Input ────────────────────────────────────────────────────────────
        hidScanInput();
        u32 kd = hidKeysDown();

        // B button = cancel
        if (kd & KEY_B) {
            return PIN_RESULT_CANCEL;
        }

        if (locked_out) {
            // Update countdown
            u64 now     = osGetTime();
            int elapsed = (int)((now - lockout_start) / 1000);
            lockout_secs = 60 - elapsed;
            if (lockout_secs < 0) lockout_secs = 0;

            if (lockout_secs <= 0) {
                // Lockout expired — reset
                locked_out   = false;
                lockout_secs = 60;
                digit_count  = 0;
                memset(digits, 0, sizeof(digits));
                message      = "Enter PIN";
                msg_color    = THEME_TEXT_DIMMED;
                attempts_left = -1;
            }
        } else if (kd & KEY_TOUCH) {
            char tapped = check_numpad_touch();
            if (tapped != '\0') {
                if (tapped == '<') {
                    // Backspace
                    if (digit_count > 0) {
                        digit_count--;
                        digits[digit_count] = '\0';
                        message   = NULL;
                        msg_color = THEME_TEXT_DIMMED;
                    }
                } else if (tapped == 'X') {
                    // Clear all
                    digit_count = 0;
                    memset(digits, 0, sizeof(digits));
                    message   = NULL;
                    msg_color = THEME_TEXT_DIMMED;
                } else {
                    // Digit
                    if (digit_count < 4) {
                        digits[digit_count++] = tapped;
                        digits[digit_count]   = '\0';
                    }

                    // Auto-submit on 4th digit
                    if (digit_count == 4) {
                        // Render "Verifying..." before the blocking HTTP call
                        cog_render_frame_begin(r);
                        draw_pin_screen(r, digits, digit_count,
                                        "Verifying...", THEME_TEXT_DIMMED,
                                        attempts_left, false, 0);
                        cog_render_frame_end(r);

                        int  new_attempts = -1;
                        bool new_locked   = false;
                        PinResult result  = verify_pin(base_url, digits,
                                                       &new_attempts, &new_locked);

                        if (result == PIN_RESULT_SKIP) {
                            return PIN_RESULT_SKIP;
                        }

                        if (result == PIN_RESULT_OK) {
                            // Flash "Verified!" for ~0.5s (30 vblanks)
                            for (int f = 0; f < 30; f++) {
                                cog_render_frame_begin(r);
                                draw_pin_screen(r, digits, digit_count,
                                                "Verified!", THEME_STATUS_ACTIVE,
                                                -1, false, 0);
                                cog_render_frame_end(r);
                                gspWaitForVBlank();
                            }
                            return PIN_RESULT_OK;
                        }

                        // Wrong PIN or lockout
                        attempts_left = new_attempts;

                        if (new_locked) {
                            locked_out   = true;
                            lockout_secs = 60;
                            lockout_start = osGetTime();
                            message   = "Locked out";
                            msg_color = THEME_STATUS_DISCONNECTED;
                        } else {
                            message   = "Wrong PIN";
                            msg_color = THEME_STATUS_DISCONNECTED;
                        }

                        // Clear digits so user can re-enter
                        digit_count = 0;
                        memset(digits, 0, sizeof(digits));
                    }
                }
            }
        }

        // ── Render ───────────────────────────────────────────────────────────
        cog_render_frame_begin(r);
        draw_pin_screen(r, digits, digit_count,
                        message, msg_color,
                        attempts_left, locked_out, lockout_secs);
        cog_render_frame_end(r);

        gspWaitForVBlank();
    }

    return PIN_RESULT_CANCEL;
}
