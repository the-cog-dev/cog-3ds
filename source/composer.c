#include "composer.h"
#include "keyboard.h"
#include "theme.h"
#include <string.h>
#include <stdio.h>

ComposerResult cog_composer_run(CogRender *r, const char *prompt,
                                const char *initial_text) {
    ComposerResult result;
    memset(&result, 0, sizeof(result));

    CogKeyboard kb;
    cog_keyboard_init(&kb, true, initial_text);

    while (aptMainLoop() && cog_keyboard_update(&kb)) {
        cog_render_frame_begin(r);
        cog_render_target_top(r, THEME_BG_DARK);
        cog_keyboard_draw_top(&kb, r, prompt, THEME_GOLD);
        cog_render_target_bottom(r, THEME_BG_CANVAS);
        cog_keyboard_draw_bottom(&kb, r);
        cog_render_frame_end(r);
    }

    if (kb.submitted) {
        result.submitted = true;
        strncpy(result.text, cog_keyboard_text(&kb), COMPOSER_MAX_LEN - 1);
        result.text[COMPOSER_MAX_LEN - 1] = '\0';
    } else {
        result.cancelled = true;
    }

    return result;
}

// Top screen layout for chat mode:
//   [0-22]   Header bar: prompt (gold)
//   [24-190] Context lines (agent output, dimmed)
//   [192]    Divider
//   [194-208] Current message being typed (bright, small)
//   [212-228] Hints
#define CHAT_HEADER_H  22
#define CHAT_LINE_H    14
#define CHAT_MAX_LINES 12   // context lines that fit on top screen
#define CHAT_MSG_Y     194
#define CHAT_HINT_Y    214

ComposerResult cog_composer_run_chat(CogRender *r, const char *prompt,
                                     const char *initial_text,
                                     const char **context_lines,
                                     int context_count) {
    ComposerResult result;
    memset(&result, 0, sizeof(result));

    CogKeyboard kb;
    cog_keyboard_init(&kb, true, initial_text);

    // Show the last CHAT_MAX_LINES of context (scroll to bottom)
    int ctx_start = context_count > CHAT_MAX_LINES
                    ? context_count - CHAT_MAX_LINES : 0;

    while (aptMainLoop() && cog_keyboard_update(&kb)) {
        // L/R scroll through output context on top screen
        u32 kd = hidKeysDown();
        if ((kd & KEY_L) && ctx_start > 0) ctx_start--;
        if ((kd & KEY_R) && ctx_start < context_count - CHAT_MAX_LINES) ctx_start++;
        // D-pad up/down also scroll context
        if ((kd & KEY_DUP) && ctx_start > 0) ctx_start--;
        if ((kd & KEY_DDOWN) && ctx_start < context_count - CHAT_MAX_LINES) ctx_start++;
        if (ctx_start < 0) ctx_start = 0;

        cog_render_frame_begin(r);
        cog_render_target_top(r, THEME_BG_DARK);

        // Header
        cog_render_rect(0, 0, 400, CHAT_HEADER_H, THEME_BG_CANVAS);
        cog_render_text(r, prompt, 12, 3, THEME_FONT_LABEL, THEME_GOLD);
        // Scroll indicator
        if (context_count > CHAT_MAX_LINES) {
            char scroll_info[16];
            snprintf(scroll_info, sizeof(scroll_info), "%d/%d",
                     ctx_start + CHAT_MAX_LINES, context_count);
            cog_render_text_right(r, scroll_info, 392, 5,
                                  THEME_FONT_FOOTER, THEME_TEXT_DIMMED);
        }
        cog_render_rect(0, CHAT_HEADER_H, 400, 1, THEME_DIVIDER);

        // Context lines (agent output)
        float cy = CHAT_HEADER_H + 4;
        for (int i = ctx_start; i < context_count && (i - ctx_start) < CHAT_MAX_LINES; i++) {
            if (context_lines[i]) {
                cog_render_text(r, context_lines[i], 6, cy,
                                THEME_FONT_FOOTER, THEME_TEXT_DIMMED);
            }
            cy += CHAT_LINE_H;
        }

        // Divider before message
        cog_render_rect(0, CHAT_MSG_Y - 4, 400, 1, THEME_DIVIDER);

        // Current message being typed (show last ~60 chars)
        const char *msg = cog_keyboard_text(&kb);
        int mlen = (int)strlen(msg);
        const char *show_msg = mlen > 60 ? msg + (mlen - 60) : msg;
        char msg_line[80];
        snprintf(msg_line, sizeof(msg_line), "> %s_", show_msg);
        cog_render_text(r, msg_line, 6, CHAT_MSG_Y,
                        THEME_FONT_LABEL, THEME_TEXT_PRIMARY);

        // Hints
        cog_render_text(r, "[A] send [B] bksp [Y] newline [L/R] scroll [START] cancel",
                        6, CHAT_HINT_Y, THEME_FONT_FOOTER, THEME_TEXT_DIMMED);

        cog_render_target_bottom(r, THEME_BG_CANVAS);
        cog_keyboard_draw_bottom(&kb, r);
        cog_render_frame_end(r);
    }

    if (kb.submitted) {
        result.submitted = true;
        strncpy(result.text, cog_keyboard_text(&kb), COMPOSER_MAX_LEN - 1);
        result.text[COMPOSER_MAX_LEN - 1] = '\0';
    } else {
        result.cancelled = true;
    }

    return result;
}
