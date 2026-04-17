#ifndef COG_SPAWN_H
#define COG_SPAWN_H

#include "render.h"
#include <stdbool.h>

#define SPAWN_MAX_PRESETS 16
#define SPAWN_MAX_AGENTS  16
#define SPAWN_NAME_LEN    64

typedef struct {
    char name[SPAWN_NAME_LEN];
    char cli[24];
    char role[24];
} SpawnAgent;

typedef struct {
    char name[SPAWN_NAME_LEN];
    int agent_count;
    SpawnAgent agents[SPAWN_MAX_AGENTS];
} SpawnPreset;

typedef struct {
    SpawnPreset presets[SPAWN_MAX_PRESETS];
    int count;
} SpawnPresetList;

bool cog_spawn_picker(CogRender *r, const SpawnPresetList *presets, const char *base_url);

#endif
