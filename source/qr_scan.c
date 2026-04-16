#include "qr_scan.h"
#include "quirc/quirc.h"
#include "theme.h"

#include <3ds.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Camera capture parameters — pinned to 400x240 YUV422 from the outer left
// sensor. That's the top screen's native resolution, and the sensor supports
// this size in hardware without scaling.
#define CAM_WIDTH   400
#define CAM_HEIGHT  240
// YUV422: each 2 pixels share 4 bytes [Y0, U, Y1, V] — so 2 bytes per pixel.
#define CAM_BYTES   (CAM_WIDTH * CAM_HEIGHT * 2)
// Raw byte size per line for CAMU_SetTransferBytes
#define LINE_BYTES  (CAM_WIDTH * 2)

// quirc works best on moderate-resolution grayscale. We downsample the camera's
// 400x240 luma to 200x120 (2x2 box filter) — good enough for a full-screen QR.
#define Q_WIDTH   200
#define Q_HEIGHT  120

// Extract full-res luma from YUV422.  In YUYV layout, Y is at every
// even byte: [Y0, U, Y1, V, Y0, U, Y1, V, ...].
static void yuv422_extract_luma(const uint8_t *yuv, uint8_t *luma, int w, int h) {
    for (int i = 0; i < w * h; i++) {
        luma[i] = yuv[i * 2];
    }
}

// Tile a linear L8 buffer into 3DS GPU Morton-order (8x8 Z-curve tiles).
// tex_w and tex_h must be powers of 2. The source image (src_w x src_h)
// is placed at the top-left of the texture; remaining texels are zeroed.
// The 3DS GPU lays tiles left-to-right then BOTTOM-to-top.
static void tile_l8_morton(const uint8_t *src, uint8_t *dst,
                           int src_w, int src_h,
                           int tex_w, int tex_h) {
    // Morton lookup for a 3-bit coordinate (0..7) → interleaved bits.
    // x bits go to even positions, y bits to odd positions.
    static const uint8_t morton[8] = { 0,1,4,5,16,17,20,21 };

    memset(dst, 0, tex_w * tex_h);  // clear padding region

    int tiles_x = tex_w / 8;
    for (int y = 0; y < src_h; y++) {
        // GPU tiles go bottom-to-top
        int gy = (tex_h - 1 - y);
        int tile_y = gy / 8;
        int sub_y  = gy % 8;
        for (int x = 0; x < src_w; x++) {
            int tile_x = x / 8;
            int sub_x  = x % 8;
            int tile_idx = tile_y * tiles_x + tile_x;
            int offset   = tile_idx * 64 + (morton[sub_x] | (morton[sub_y] << 1));
            dst[offset] = src[y * src_w + x];
        }
    }
}

// Extract the luma plane from a YUV422 camera frame and downsample 2x.
// buf is CAM_WIDTH * CAM_HEIGHT * 2 bytes (YUYV interleaved per pair).
// Writes CAM_WIDTH/2 * CAM_HEIGHT/2 luma bytes into out.
static void yuv422_to_luma_half(const uint8_t *buf, uint8_t *out) {
    for (int y = 0; y < Q_HEIGHT; y++) {
        for (int x = 0; x < Q_WIDTH; x++) {
            // Source: 2 rows x 2 cols in the original, averaging Y samples.
            // Each source pair of pixels (2 bytes) is [Y, Cb/Cr] alternating
            // in YUYV layout. Y is at even byte offsets within the pair.
            int src_x0 = x * 2;
            int src_y0 = y * 2;
            // Row 0 luma at src_x0 and src_x0+1
            int r0 = src_y0 * CAM_WIDTH * 2;
            int r1 = (src_y0 + 1) * CAM_WIDTH * 2;
            uint8_t a = buf[r0 + src_x0 * 2];
            uint8_t b = buf[r0 + (src_x0 + 1) * 2];
            uint8_t c = buf[r1 + src_x0 * 2];
            uint8_t d = buf[r1 + (src_x0 + 1) * 2];
            out[y * Q_WIDTH + x] = (a + b + c + d) >> 2;
        }
    }
}

// Check if the decoded payload looks like a Cog URL we can use.
static bool payload_is_valid_url(const uint8_t *payload, int len) {
    // Minimum "http://x/r/y/" is ~14 chars — be permissive, just require
    // http:// prefix and reasonable length.
    if (len < 12 || len > 500) return false;
    if (memcmp(payload, "http://", 7) != 0 &&
        memcmp(payload, "https://", 8) != 0) return false;
    return true;
}

// Draw one full scanner status frame (top + bottom) via citro2d. Called
// once per loop iteration — citro2d needs a full frame submission every
// tick or the screen stays stale.
static void draw_status_frame(CogRender *r, const char *top_big,
                              const char *bot_status, int scan_count) {
    u64 now = osGetTime();
    float pulse = 0.5f + 0.5f * sinf((now % 1000) / 1000.0f * 6.28318f);
    u32 pulse_color = C2D_Color32f(1.0f, 0.84f * pulse, 0.43f * pulse, 1.0f);

    cog_render_frame_begin(r);

    // Top: big scan label
    cog_render_target_top(r, THEME_BG_DARK);
    cog_render_text(r, top_big, 80, 80, THEME_FONT_HEADER, pulse_color);
    cog_render_text(r, "Point outer camera at the QR.",
                    60, 120, THEME_FONT_LABEL, THEME_TEXT_DIMMED);
    cog_render_text(r, "Hold steady ~15 cm away.",
                    60, 145, THEME_FONT_LABEL, THEME_TEXT_DIMMED);

    // Bottom: status
    cog_render_target_bottom(r, THEME_BG_CANVAS);
    cog_render_text(r, "QR Scanner", 12, 12, THEME_FONT_HEADER, THEME_GOLD);
    cog_render_text(r, bot_status, 12, 60, THEME_FONT_LABEL, THEME_TEXT_PRIMARY);
    char buf[32];
    snprintf(buf, sizeof(buf), "Frames: %d", scan_count);
    cog_render_text(r, buf, 12, 90, THEME_FONT_LABEL, THEME_TEXT_DIMMED);
    cog_render_text(r, "[B] cancel", 12, 210, THEME_FONT_FOOTER, THEME_TEXT_DIMMED);

    cog_render_frame_end(r);
}

// Draw a frame with the live camera preview on the top screen and status
// text on the bottom. Falls back to a dark background if the preview
// texture wasn't allocated.
static void draw_preview_frame(CogRender *r, C2D_Image *preview,
                               bool has_preview, const char *bot_status,
                               int scan_count) {
    cog_render_frame_begin(r);

    // Top: camera preview (or dark bg if preview unavailable)
    cog_render_target_top(r, THEME_BG_DARK);
    if (has_preview) {
        C2D_DrawImageAt(*preview, 0, 0, 0.5f, NULL, 1.0f, 1.0f);
    }
    // Overlay: semi-transparent bar at bottom of preview
    cog_render_rect(0, 200, 400, 40, C2D_Color32(0x0d, 0x0d, 0x0d, 0xbb));
    cog_render_text(r, "Aim at QR code", 120, 210, THEME_FONT_LABEL, THEME_GOLD);

    // Bottom: status
    cog_render_target_bottom(r, THEME_BG_CANVAS);
    cog_render_text(r, "QR Scanner", 12, 12, THEME_FONT_HEADER, THEME_GOLD);
    cog_render_text(r, bot_status, 12, 60, THEME_FONT_LABEL, THEME_TEXT_PRIMARY);
    char buf[32];
    snprintf(buf, sizeof(buf), "Frames: %d", scan_count);
    cog_render_text(r, buf, 12, 90, THEME_FONT_LABEL, THEME_TEXT_DIMMED);
    cog_render_text(r, "[B] cancel", 12, 210, THEME_FONT_FOOTER, THEME_TEXT_DIMMED);

    cog_render_frame_end(r);
}

bool cog_qr_scan(CogRender *render, char *out_url, size_t out_size) {
    if (!out_url || out_size == 0) return false;
    out_url[0] = '\0';

    draw_status_frame(render, "SCANNING...", "initializing camera", 0);

    bool success = false;
    uint8_t *frame = NULL;
    struct quirc *q = NULL;
    C3D_Tex preview_tex;
    bool has_preview = false;
    u8 *preview_luma = NULL;

    // ── Init camera ──────────────────────────────────────────────────────
    Result rc = camInit();
    if (R_FAILED(rc)) {
        draw_status_frame(render, "SCANNING...", "camInit failed", 0);
        goto cleanup;
    }

    // Outer left sensor on port 1 (CAM1). SELECT_OUT1 is the single outer
    // camera output.
    CAMU_SetSize(SELECT_OUT1, SIZE_CTR_TOP_LCD, CONTEXT_A);
    CAMU_SetOutputFormat(SELECT_OUT1, OUTPUT_YUV_422, CONTEXT_A);
    CAMU_SetFrameRate(SELECT_OUT1, FRAME_RATE_15);
    CAMU_SetNoiseFilter(SELECT_OUT1, true);
    CAMU_SetAutoExposure(SELECT_OUT1, true);
    CAMU_SetAutoWhiteBalance(SELECT_OUT1, true);
    CAMU_SetContrast(SELECT_OUT1, CONTRAST_HIGH);
    CAMU_SetTrimming(PORT_CAM1, false);

    u32 cam_bytes_needed = 0;
    CAMU_GetMaxBytes(&cam_bytes_needed, CAM_WIDTH, CAM_HEIGHT);
    CAMU_SetTransferBytes(PORT_CAM1, cam_bytes_needed, CAM_WIDTH, CAM_HEIGHT);

    frame = (uint8_t *)linearAlloc(CAM_BYTES);
    if (!frame) {
        draw_status_frame(render, "SCANNING...", "frame alloc failed", 0);
        goto cleanup;
    }

    CAMU_Activate(SELECT_OUT1);
    CAMU_ClearBuffer(PORT_CAM1);
    CAMU_StartCapture(PORT_CAM1);

    // ── Camera preview texture ───────────────────────────────────────────
    // GPU textures must be power-of-2.  We use 512x256 L8 for the 400x240
    // camera frame.  Tiling is done in software (Morton order) since the
    // GX transfer engine doesn't support L8.
    if (C3D_TexInit(&preview_tex, 512, 256, GPU_L8)) {
        C3D_TexSetFilter(&preview_tex, GPU_NEAREST, GPU_NEAREST);
        preview_luma = (u8 *)malloc(CAM_WIDTH * CAM_HEIGHT);
        if (preview_luma) {
            has_preview = true;
        } else {
            C3D_TexDelete(&preview_tex);  // don't leak GPU mem
        }
    }

    // Subtexture: the 400x240 region within the 512x256 texture.
    Tex3DS_SubTexture preview_subtex = {
        .width  = 400,
        .height = 240,
        .left   = 0.0f,
        .top    = 1.0f,
        .right  = 400.0f / 512.0f,
        .bottom = 1.0f - (240.0f / 256.0f),
    };
    C2D_Image preview_img = { &preview_tex, &preview_subtex };

    // ── Init quirc ───────────────────────────────────────────────────────
    q = quirc_new();
    if (!q || quirc_resize(q, Q_WIDTH, Q_HEIGHT) < 0) {
        draw_status_frame(render, "SCANNING...", "quirc init failed", 0);
        goto cleanup;
    }

    draw_status_frame(render, "SCANNING...", "scanning...", 0);

    // ── Capture + decode loop ────────────────────────────────────────────
    int scan_count = 0;
    char last_error[64] = "";

    while (aptMainLoop()) {
        hidScanInput();
        u32 down = hidKeysDown();
        if (down & KEY_B) break;

        // Async capture of one frame.
        Handle receive_event = 0;
        CAMU_SetReceiving(&receive_event, frame, PORT_CAM1, CAM_BYTES, (s16)LINE_BYTES);
        Result wait_rc = svcWaitSynchronization(receive_event, 300000000LL); // 300ms
        if (receive_event) { svcCloseHandle(receive_event); receive_event = 0; }
        if (R_FAILED(wait_rc)) {
            strncpy(last_error, "capture timeout", sizeof(last_error) - 1);
            last_error[sizeof(last_error) - 1] = '\0';
            draw_status_frame(render, "SCANNING...", last_error, scan_count);
            continue;
        }

        scan_count++;

        // Upload camera preview to GPU texture (software Morton tiling)
        if (has_preview) {
            yuv422_extract_luma(frame, preview_luma, CAM_WIDTH, CAM_HEIGHT);
            tile_l8_morton(preview_luma, (uint8_t *)preview_tex.data,
                          CAM_WIDTH, CAM_HEIGHT, 512, 256);
            C3D_TexFlush(&preview_tex);
        }

        // Feed luma to quirc.
        int qw = 0, qh = 0;
        uint8_t *qbuf = quirc_begin(q, &qw, &qh);
        if (qbuf && qw == Q_WIDTH && qh == Q_HEIGHT) {
            yuv422_to_luma_half(frame, qbuf);
            quirc_end(q);

            int n = quirc_count(q);
            for (int i = 0; i < n; i++) {
                struct quirc_code code;
                struct quirc_data data;
                quirc_extract(q, i, &code);
                quirc_decode_error_t err = quirc_decode(&code, &data);
                if (err == QUIRC_SUCCESS) {
                    if (payload_is_valid_url(data.payload, data.payload_len)) {
                        size_t copy_len = (size_t)data.payload_len;
                        if (copy_len >= out_size) copy_len = out_size - 1;
                        memcpy(out_url, data.payload, copy_len);
                        out_url[copy_len] = '\0';
                        success = true;
                        draw_preview_frame(render, &preview_img, has_preview,
                                           "decoded URL!", scan_count);
                        // short pause so user sees confirmation
                        for (int f = 0; f < 45; f++) gspWaitForVBlank();
                        goto cleanup;
                    } else {
                        strncpy(last_error, "QR found but not a URL", sizeof(last_error) - 1);
                        last_error[sizeof(last_error) - 1] = '\0';
                    }
                } else if (err != QUIRC_ERROR_DATA_ECC && err != QUIRC_ERROR_FORMAT_ECC) {
                    strncpy(last_error, quirc_strerror(err), sizeof(last_error) - 1);
                    last_error[sizeof(last_error) - 1] = '\0';
                }
            }
        }

        draw_preview_frame(render, &preview_img, has_preview,
                           last_error[0] ? last_error : "scanning...",
                           scan_count);
    }

cleanup:
    if (preview_luma) free(preview_luma);
    if (has_preview) C3D_TexDelete(&preview_tex);
    if (q) quirc_destroy(q);
    CAMU_StopCapture(PORT_CAM1);
    CAMU_Activate(SELECT_NONE);
    camExit();
    if (frame) linearFree(frame);
    return success;
}
