#include "canvas.h"
#include "theme.h"
#include <string.h>
#include <math.h>
#include <stdbool.h>

void canvas_init(Canvas *cv) {
    memset(cv, 0, sizeof(*cv));
    cv->cam_zoom = 1.0f;
    cv->selected_idx = -1;
    cv->lifted_idx = -1;
}

void canvas_world_to_screen(const Canvas *cv, float wx, float wy,
                            float *sx, float *sy) {
    *sx = (wx - cv->cam_x) * cv->cam_zoom + CANVAS_SCREEN_W / 2.0f;
    *sy = (wy - cv->cam_y) * cv->cam_zoom + CANVAS_SCREEN_H / 2.0f;
}

void canvas_screen_to_world(const Canvas *cv, float sx, float sy,
                            float *wx, float *wy) {
    *wx = (sx - CANVAS_SCREEN_W / 2.0f) / cv->cam_zoom + cv->cam_x;
    *wy = (sy - CANVAS_SCREEN_H / 2.0f) / cv->cam_zoom + cv->cam_y;
}

int canvas_hit_test(const Canvas *cv, float sx, float sy) {
    // Back-to-front so topmost card wins
    for (int i = cv->card_count - 1; i >= 0; i--) {
        const Card *c = &cv->cards[i];
        float csx, csy;
        canvas_world_to_screen(cv, c->x, c->y, &csx, &csy);
        float csw = c->width * cv->cam_zoom * c->lift_scale;
        float csh = c->height * cv->cam_zoom * c->lift_scale;
        if (sx >= csx && sx <= csx + csw && sy >= csy && sy <= csy + csh) {
            return i;
        }
    }
    return -1;
}

void canvas_pan(Canvas *cv, float dsx, float dsy) {
    cv->cam_x -= dsx / cv->cam_zoom;
    cv->cam_y -= dsy / cv->cam_zoom;
}

void canvas_zoom(Canvas *cv, float delta) {
    cv->cam_zoom += delta;
    if (cv->cam_zoom < CANVAS_ZOOM_MIN) cv->cam_zoom = CANVAS_ZOOM_MIN;
    if (cv->cam_zoom > CANVAS_ZOOM_MAX) cv->cam_zoom = CANVAS_ZOOM_MAX;
}

void canvas_draw(CogRender *r, const Canvas *cv) {
    // Background already cleared by caller to THEME_BG_CANVAS.
    // Draw cards in order so higher-indexed cards end up on top.
    int lifted = cv->lifted_idx;
    for (int i = 0; i < cv->card_count; i++) {
        if (i == lifted) continue;  // draw lifted last
        const Card *c = &cv->cards[i];
        float sx, sy;
        canvas_world_to_screen(cv, c->x, c->y, &sx, &sy);
        card_draw(r, c, sx, sy, cv->cam_zoom);
    }
    if (lifted >= 0 && lifted < cv->card_count) {
        const Card *c = &cv->cards[lifted];
        float sx, sy;
        canvas_world_to_screen(cv, c->x, c->y, &sx, &sy);
        card_draw(r, c, sx, sy, cv->cam_zoom);
    }
}

void canvas_frame_all(Canvas *cv) {
    if (cv->card_count == 0) {
        cv->cam_x = 0; cv->cam_y = 0; cv->cam_zoom = 1.0f;
        return;
    }
    float minx = cv->cards[0].x, maxx = cv->cards[0].x + cv->cards[0].width;
    float miny = cv->cards[0].y, maxy = cv->cards[0].y + cv->cards[0].height;
    for (int i = 1; i < cv->card_count; i++) {
        const Card *c = &cv->cards[i];
        if (c->x < minx) minx = c->x;
        if (c->y < miny) miny = c->y;
        if (c->x + c->width > maxx) maxx = c->x + c->width;
        if (c->y + c->height > maxy) maxy = c->y + c->height;
    }
    float w = maxx - minx, h = maxy - miny;
    cv->cam_x = minx + w / 2;
    cv->cam_y = miny + h / 2;
    // Fit with 10% padding
    float zx = (CANVAS_SCREEN_W * 0.9f) / (w > 1 ? w : 1);
    float zy = (CANVAS_SCREEN_H * 0.9f) / (h > 1 ? h : 1);
    cv->cam_zoom = zx < zy ? zx : zy;
    if (cv->cam_zoom < CANVAS_ZOOM_MIN) cv->cam_zoom = CANVAS_ZOOM_MIN;
    if (cv->cam_zoom > CANVAS_ZOOM_MAX) cv->cam_zoom = CANVAS_ZOOM_MAX;
}

int canvas_nav_nearest(const Canvas *cv, CanvasNavDir dir) {
    if (cv->card_count == 0) return -1;
    if (cv->selected_idx < 0) return 0;
    const Card *from = &cv->cards[cv->selected_idx];
    float fx = from->x + from->width / 2, fy = from->y + from->height / 2;
    int best = -1;
    float best_dist = 1e30f;
    for (int i = 0; i < cv->card_count; i++) {
        if (i == cv->selected_idx) continue;
        const Card *c = &cv->cards[i];
        float cx = c->x + c->width / 2, cy = c->y + c->height / 2;
        float dx = cx - fx, dy = cy - fy;
        bool in_dir = false;
        switch (dir) {
            case CANVAS_NAV_LEFT:  in_dir = dx < 0 && (-dx) > (dy > 0 ? dy : -dy); break;
            case CANVAS_NAV_RIGHT: in_dir = dx > 0 && dx > (dy > 0 ? dy : -dy);    break;
            case CANVAS_NAV_UP:    in_dir = dy < 0 && (-dy) > (dx > 0 ? dx : -dx); break;
            case CANVAS_NAV_DOWN:  in_dir = dy > 0 && dy > (dx > 0 ? dx : -dx);    break;
        }
        if (!in_dir) continue;
        float dist = dx * dx + dy * dy;
        if (dist < best_dist) { best_dist = dist; best = i; }
    }
    return best;
}
