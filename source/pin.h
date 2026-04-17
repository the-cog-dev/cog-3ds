#ifndef COG_PIN_H
#define COG_PIN_H

#include "render.h"
#include <stdbool.h>

typedef enum {
    PIN_RESULT_OK,
    PIN_RESULT_CANCEL,
    PIN_RESULT_SKIP
} PinResult;

PinResult cog_pin_entry(CogRender *r, const char *base_url);

#endif
