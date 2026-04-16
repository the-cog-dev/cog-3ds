#include "render.h"
#include <string.h>

bool cog_render_init(CogRender *r) {
    memset(r, 0, sizeof(*r));
    if (!C3D_Init(C3D_DEFAULT_CMDBUF_SIZE)) return false;
    if (!C2D_Init(C2D_DEFAULT_MAX_OBJECTS)) { C3D_Fini(); return false; }
    C2D_Prepare();
    r->top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    r->bottom = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    r->text_buf = C2D_TextBufNew(4096);
    if (!r->top || !r->bottom || !r->text_buf) {
        cog_render_exit(r);
        return false;
    }
    r->ready = true;
    return true;
}

void cog_render_exit(CogRender *r) {
    if (r->text_buf) { C2D_TextBufDelete(r->text_buf); r->text_buf = NULL; }
    // Targets and citro2d resources are cleaned up by C2D_Fini / C3D_Fini
    if (r->ready) {
        C2D_Fini();
        C3D_Fini();
        r->ready = false;
    }
}

void cog_render_frame_begin(CogRender *r) {
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    C2D_TextBufClear(r->text_buf);
}

void cog_render_target_top(CogRender *r, u32 clear_color) {
    C2D_TargetClear(r->top, clear_color);
    C2D_SceneBegin(r->top);
}

void cog_render_target_bottom(CogRender *r, u32 clear_color) {
    C2D_TargetClear(r->bottom, clear_color);
    C2D_SceneBegin(r->bottom);
}

void cog_render_frame_end(CogRender *r) {
    (void)r;
    C3D_FrameEnd(0);
}

void cog_render_text(CogRender *r, const char *str, float x, float y,
                     float scale, u32 color) {
    if (!str || !*str) return;
    C2D_Text txt;
    C2D_TextParse(&txt, r->text_buf, str);
    C2D_TextOptimize(&txt);
    C2D_DrawText(&txt, C2D_WithColor, x, y, 0.5f, scale, scale, color);
}

void cog_render_text_right(CogRender *r, const char *str, float x, float y,
                           float scale, u32 color) {
    if (!str || !*str) return;
    float w = cog_render_text_width(r, str, scale);
    cog_render_text(r, str, x - w, y, scale, color);
}

float cog_render_text_width(CogRender *r, const char *str, float scale) {
    if (!str || !*str) return 0.0f;
    C2D_Text txt;
    C2D_TextParse(&txt, r->text_buf, str);
    C2D_TextOptimize(&txt);
    float w = 0.0f, h = 0.0f;
    C2D_TextGetDimensions(&txt, scale, scale, &w, &h);
    return w;
}

void cog_render_rect(float x, float y, float w, float h, u32 color) {
    C2D_DrawRectSolid(x, y, 0.5f, w, h, color);
}

void cog_render_rounded_rect(float x, float y, float w, float h,
                             float radius, u32 color) {
    if (radius * 2 > w) radius = w / 2;
    if (radius * 2 > h) radius = h / 2;
    // Center rect
    cog_render_rect(x + radius, y, w - 2 * radius, h, color);
    // Left cap
    C2D_DrawCircleSolid(x + radius, y + h / 2, 0.5f, radius, color);
    // Right cap
    C2D_DrawCircleSolid(x + w - radius, y + h / 2, 0.5f, radius, color);
    // Vertical fillers (top/bottom of side caps where the circle doesn't cover)
    cog_render_rect(x, y + radius, radius, h - 2 * radius, color);
    cog_render_rect(x + w - radius, y + radius, radius, h - 2 * radius, color);
}

void cog_render_rect_outline(float x, float y, float w, float h,
                             float thickness, u32 color) {
    cog_render_rect(x, y, w, thickness, color);                     // top
    cog_render_rect(x, y + h - thickness, w, thickness, color);     // bottom
    cog_render_rect(x, y, thickness, h, color);                     // left
    cog_render_rect(x + w - thickness, y, thickness, h, color);     // right
}
