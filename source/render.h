// citro2d render helpers. Hides the per-frame boilerplate (frame begin,
// target clear, scene select, frame end) and wraps the C2D text API
// in a saner function-per-string helper that handles allocation.

#ifndef COG_RENDER_H
#define COG_RENDER_H

#include <citro2d.h>
#include <stdbool.h>

typedef struct {
    C3D_RenderTarget *top;
    C3D_RenderTarget *bottom;
    C2D_TextBuf text_buf;     // scratch buffer re-cleared each frame
    bool ready;
} CogRender;

// Init citro2d + create both screen targets + text buffer. Returns
// false if any step failed; caller should fall back to PrintConsole.
bool cog_render_init(CogRender *r);
void cog_render_exit(CogRender *r);

// Frame boundary helpers. Call begin once at frame start, then
// target_top / target_bottom to switch scenes, then end.
void cog_render_frame_begin(CogRender *r);
void cog_render_target_top(CogRender *r, u32 clear_color);
void cog_render_target_bottom(CogRender *r, u32 clear_color);
void cog_render_frame_end(CogRender *r);

// Draw UTF-8 text at (x, y) in screen coords, scaled, colored.
// The text buffer is reset each frame so string pointers only need
// to live through one frame.
void cog_render_text(CogRender *r, const char *str, float x, float y,
                     float scale, u32 color);

// Same, but right-aligned so str ends at x.
void cog_render_text_right(CogRender *r, const char *str, float x, float y,
                           float scale, u32 color);

// Measure text width in screen pixels at the given scale. Useful for
// layout decisions (truncation, centering).
float cog_render_text_width(CogRender *r, const char *str, float scale);

// Solid rect (axis-aligned, no rotation).
void cog_render_rect(float x, float y, float w, float h, u32 color);

// Rounded rect — cheap approximation with one rect + two stadium caps.
// Corner radius clamped to min(w, h) / 2.
void cog_render_rounded_rect(float x, float y, float w, float h,
                             float radius, u32 color);

// Outline (border) of a rect with given thickness, drawn inside the rect.
void cog_render_rect_outline(float x, float y, float w, float h,
                             float thickness, u32 color);

#endif
