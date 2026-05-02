// Workshop canvas — bottom-screen view. Maintains camera state
// (pan + zoom), draws cards through the camera transform, and
// hit-tests screen taps back to card indices.
//
// World coords: desktop-native pixels. 1 world unit = 1 desktop pixel.
// Screen coords: 320x240 3DS bottom screen.

#ifndef COG_CANVAS_H
#define COG_CANVAS_H

#include "card.h"
#include <stdbool.h>

#define CANVAS_MAX_CARDS 35
#define CANVAS_SCREEN_W  320
#define CANVAS_SCREEN_H  240
#define CANVAS_ZOOM_MIN  0.1f
#define CANVAS_ZOOM_MAX  2.0f
#define CANVAS_ZOOM_STEP 0.15f

typedef struct {
    Card cards[CANVAS_MAX_CARDS];
    int card_count;
    float cam_x, cam_y;
    float cam_zoom;
    int selected_idx;  // -1 = none
    int lifted_idx;    // -1 = not dragging
} Canvas;

void canvas_init(Canvas *cv);

// Apply camera transform: world -> screen.
void canvas_world_to_screen(const Canvas *cv, float wx, float wy,
                            float *sx, float *sy);
void canvas_screen_to_world(const Canvas *cv, float sx, float sy,
                            float *wx, float *wy);

// Hit-test a screen-coord touch against all cards. Returns index of
// topmost card under the point, or -1 if none. Topmost = later in
// array (drawn later = in front).
int canvas_hit_test(const Canvas *cv, float sx, float sy);

// Pan the camera (screen-space delta — divides by zoom internally).
void canvas_pan(Canvas *cv, float dsx, float dsy);

// Zoom toward the center of the screen.
void canvas_zoom(Canvas *cv, float delta);

// Draw the whole canvas (background + all cards in render order).
void canvas_draw(CogRender *r, const Canvas *cv);

// Frame-the-cards helper: center camera on bounding box of all
// cards and pick zoom that fits them on screen. Call after first
// /state poll populates cards.
void canvas_frame_all(Canvas *cv);

// Returns the index of the card nearest to the currently-selected
// card in the given direction (-1 left, +1 right, -1/+1 for up/down on y).
// Returns -1 if no card exists in that direction. If nothing selected,
// returns index 0 (or -1 if empty).
typedef enum { CANVAS_NAV_UP, CANVAS_NAV_DOWN, CANVAS_NAV_LEFT, CANVAS_NAV_RIGHT } CanvasNavDir;
int canvas_nav_nearest(const Canvas *cv, CanvasNavDir dir);

typedef struct {
    bool open;
    float x, y, w, h;
} PanelState;

void canvas_add_panel_cards(Canvas *cv, int task_count, int info_count, int schedule_count,
                            const PanelState *pinboard, const PanelState *info,
                            const PanelState *schedules);

// Inbox is a synthetic panel — no equivalent desktop window — that the 3DS
// only spawns when the user has unread orchestrator messages or a pending
// team proposal. unread is shown as the badge text. Position is fixed in
// world space so the user can find it via D-pad/zoom.
void canvas_add_inbox_panel(Canvas *cv, int unread, int total,
                            float world_x, float world_y);

#endif
