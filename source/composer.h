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

#endif
