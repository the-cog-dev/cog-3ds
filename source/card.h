// A single agent card for the workshop canvas.
//
// The card holds both persistent agent data (name, status, etc) and
// transient view state (lift scale, fade alpha for enter/exit anims).
// The agent-data fields are synced from /state each poll; the view
// state lives only on the 3DS.

#ifndef COG_CARD_H
#define COG_CARD_H

#include "render.h"
#include <stdbool.h>

typedef enum {
    CARD_TYPE_AGENT_CARD,
    CARD_TYPE_PINBOARD_CARD,
    CARD_TYPE_INFO_CARD,
    CARD_TYPE_SCHEDULE_CARD
} CardTypeEnum;

typedef struct {
    // From /state
    char id[64];
    char name[64];
    char cli[24];
    char model[32];
    char role[24];
    char status[16];
    float x, y;
    float width, height;
    u32 color;   // group color, RGBA

    // View state
    float lift_scale;   // 1.0 at rest, 1.2 when lifted
    float enter_alpha;  // 0..1, fades in when card first appears
    bool selected;
    bool lifted;
    CardTypeEnum card_type;
    bool draggable;
} Card;

// Default card size when /state doesn't provide one.
#define CARD_DEFAULT_WIDTH   60.0f
#define CARD_DEFAULT_HEIGHT  40.0f

// Draw a card at its current (x, y) in world coords, transformed by
// camera pan/zoom applied by the caller (pass the already-projected
// screen coords via sx/sy and scale combined into world_to_screen_scale).
// Selection ring and lift glow are drawn by the caller via helpers below.
void card_draw(CogRender *r, const Card *c, float sx, float sy,
               float world_to_screen_scale);

// Selection ring (gold 2px border around the card at its projected rect).
void card_draw_selection_ring(float sx, float sy, float sw, float sh);

// Lift glow under a lifted card — soft gold glow, cheap 3-layer fake blur.
void card_draw_lift_glow(float sx, float sy, float sw, float sh);

#endif
