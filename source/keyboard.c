#include "keyboard.h"
#include "theme.h"
#include <string.h>
#include <stdio.h>

// Key layout — same rows as the original inline keyboard in main.c
static const char *KB_ROWS[] = {
    "1234567890",
    "qwertyuiop",
    "asdfghjkl:",
    "zxcvbnm-_.",
    "/ "
};
static const int KB_ROW_COUNT = 5;
static const float KB_KEY_W = 28.0f;
static const float KB_KEY_H = 28.0f;
static const float KB_PAD = 2.0f;
static const float KB_TOP_Y = 100.0f;  // keyboard starts here on bottom screen

void cog_keyboard_init(CogKeyboard *kb, bool multiline, const char *initial_text) {
    memset(kb, 0, sizeof(*kb));
    kb->multiline = multiline;
    if (initial_text) {
        strncpy(kb->buffer, initial_text, KB_MAX_LEN - 1);
        kb->buffer[KB_MAX_LEN - 1] = '\0';
        kb->cursor = (int)strlen(kb->buffer);
    }
}

bool cog_keyboard_update(CogKeyboard *kb) {
    hidScanInput();
    u32 kd = hidKeysDown();

    // Backspace
    if ((kd & KEY_B) && kb->cursor > 0) {
        kb->buffer[--kb->cursor] = '\0';
    }

    // Submit
    if (kd & KEY_A) {
        kb->submitted = true;
        return false;
    }

    // Cancel
    if (kd & KEY_START) {
        kb->cancelled = true;
        return false;
    }

    // Newline for multi-line mode (Y button)
    if (kb->multiline && (kd & KEY_Y) && kb->cursor < KB_MAX_LEN - 1) {
        kb->buffer[kb->cursor++] = '\n';
        kb->buffer[kb->cursor] = '\0';
    }

    // Touch input — detect which key was tapped
    if (kd & KEY_TOUCH) {
        touchPosition tp;
        hidTouchRead(&tp);
        for (int row = 0; row < KB_ROW_COUNT; row++) {
            int len = (int)strlen(KB_ROWS[row]);
            float rx = (320.0f - len * (KB_KEY_W + KB_PAD)) / 2.0f;
            float ry = KB_TOP_Y + row * (KB_KEY_H + KB_PAD);
            for (int col = 0; col < len; col++) {
                float kx = rx + col * (KB_KEY_W + KB_PAD);
                if (tp.px >= kx && tp.px < kx + KB_KEY_W &&
                    tp.py >= ry && tp.py < ry + KB_KEY_H) {
                    if (kb->cursor < KB_MAX_LEN - 1) {
                        kb->buffer[kb->cursor++] = KB_ROWS[row][col];
                        kb->buffer[kb->cursor] = '\0';
                    }
                }
            }
        }
    }

    return true;  // still active
}

void cog_keyboard_draw_bottom(CogKeyboard *kb, CogRender *r) {
    // Text preview at top of bottom screen
    cog_render_rect(0, 0, 320, KB_TOP_Y - 4, THEME_BG_DARK);
    int len = kb->cursor;
    const char *show = kb->buffer;
    if (len > 240) show = kb->buffer + (len - 240);
    cog_render_text(r, show, 8, 8, THEME_FONT_LABEL, THEME_TEXT_PRIMARY);

    // Cursor position indicator
    char cursor_line[8];
    snprintf(cursor_line, sizeof(cursor_line), "%d", kb->cursor);
    cog_render_text_right(r, cursor_line, 312, KB_TOP_Y - 18,
                          THEME_FONT_FOOTER, THEME_TEXT_DIMMED);

    // Divider
    cog_render_rect(0, KB_TOP_Y - 4, 320, 1, THEME_DIVIDER);

    // Draw keyboard rows
    for (int row = 0; row < KB_ROW_COUNT; row++) {
        int rlen = (int)strlen(KB_ROWS[row]);
        float rx = (320.0f - rlen * (KB_KEY_W + KB_PAD)) / 2.0f;
        float ry = KB_TOP_Y + row * (KB_KEY_H + KB_PAD);
        for (int col = 0; col < rlen; col++) {
            float kx = rx + col * (KB_KEY_W + KB_PAD);
            cog_render_rounded_rect(kx, ry, KB_KEY_W, KB_KEY_H, 3.0f, THEME_DIVIDER);
            char ch[2] = { KB_ROWS[row][col], '\0' };
            cog_render_text(r, ch, kx + 8, ry + 4, THEME_FONT_CARD, THEME_TEXT_PRIMARY);
        }
    }
}

void cog_keyboard_draw_top(CogKeyboard *kb, CogRender *r,
                           const char *prompt, u32 prompt_color) {
    cog_render_text(r, prompt, 12, 20, THEME_FONT_HEADER, prompt_color);
    cog_render_text(r, kb->buffer, 12, 60, THEME_FONT_LABEL, THEME_TEXT_PRIMARY);
    const char *hint = kb->multiline
        ? "[A] send  [B] backspace  [Y] newline  [START] cancel"
        : "[A] save  [B] backspace  [START] cancel";
    cog_render_text(r, hint, 12, 210, THEME_FONT_FOOTER, THEME_TEXT_DIMMED);
}

const char *cog_keyboard_text(const CogKeyboard *kb) {
    return kb->buffer;
}
