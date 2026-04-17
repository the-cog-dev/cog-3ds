#include "composer.h"
#include "keyboard.h"
#include "theme.h"
#include <string.h>

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
