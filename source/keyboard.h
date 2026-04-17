// Reusable touch keyboard for text input. Extracted from the inline
// URL keyboard in main.c. Supports single-line (URL) and multi-line
// (message/task) modes.

#ifndef COG_KEYBOARD_H
#define COG_KEYBOARD_H

#include "render.h"
#include <stdbool.h>

#define KB_MAX_LEN 1024

typedef struct {
    char buffer[KB_MAX_LEN];
    int cursor;           // insertion point (== strlen for append-only)
    bool multiline;       // true = allow newlines, show multi-line preview
    bool cancelled;       // set true if user pressed START
    bool submitted;       // set true if user pressed A
} CogKeyboard;

// Initialize keyboard state. If initial_text is non-NULL, copies it in.
void cog_keyboard_init(CogKeyboard *kb, bool multiline, const char *initial_text);

// Process one frame of keyboard input. Call inside your aptMainLoop.
// Handles KEY_TOUCH for key taps, KEY_B for backspace, KEY_A for submit,
// KEY_START for cancel. Returns true while keyboard is still active.
// Returns false when user submitted (A) or cancelled (START).
bool cog_keyboard_update(CogKeyboard *kb);

// Draw the keyboard on the bottom screen. Call after cog_render_target_bottom.
// Draws a text preview area at the top of bottom screen (first 3 lines of buffer),
// then the 5-row QWERTY layout below.
void cog_keyboard_draw_bottom(CogKeyboard *kb, CogRender *r);

// Draw a full text preview on the top screen. Call after cog_render_target_top.
// Shows the prompt string at the top, then the full buffer text below it.
void cog_keyboard_draw_top(CogKeyboard *kb, CogRender *r,
                           const char *prompt, u32 prompt_color);

// Get the current text. Returns pointer to internal buffer (do not free).
const char *cog_keyboard_text(const CogKeyboard *kb);

#endif
