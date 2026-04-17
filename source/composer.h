#ifndef COG_COMPOSER_H
#define COG_COMPOSER_H

#include "render.h"
#include <stdbool.h>

#define COMPOSER_MAX_LEN 1024

typedef struct {
    char text[COMPOSER_MAX_LEN];
    bool submitted;
    bool cancelled;
} ComposerResult;

ComposerResult cog_composer_run(CogRender *r, const char *prompt, const char *initial_text);

// Chat-style composer: shows context lines (e.g. agent output) on the
// top screen while typing on the bottom. The current message being
// typed shows in a small bar between the context and keyboard hints.
ComposerResult cog_composer_run_chat(CogRender *r, const char *prompt,
                                     const char *initial_text,
                                     const char **context_lines,
                                     int context_count);

#endif
