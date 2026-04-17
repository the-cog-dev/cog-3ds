#include "qr_scan.h"
#include "quirc/quirc.h"
#include "theme.h"
#include "cJSON.h"
#include "http.h"

#include <3ds.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CAM_WIDTH   400
#define CAM_HEIGHT  240
#define CAM_BUF_SIZE (CAM_WIDTH * CAM_HEIGHT * sizeof(u16))

// quirc at full camera resolution — the 3DS 0.3MP camera needs every
// pixel to resolve QR modules. Downsampling to 200×120 left only 1-3px
// per module which is below quirc's reliable threshold.
#define Q_WIDTH   CAM_WIDTH
#define Q_HEIGHT  CAM_HEIGHT

// Tile a linear RGB565 buffer into 3DS GPU Morton order and upload.
// Adapted from FBI's screen_load_texture_untiled — proven to work on
// real hardware with C3D_TexFlush.
static void tile_and_upload(C3D_Tex *tex, const u16 *src, int w, int h) {
    int pw = tex->width;
    u16 *dst = (u16 *)tex->data;
    memset(dst, 0, tex->size);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int block = ((y >> 3) * (pw >> 3) + (x >> 3)) << 6;
            int pos = block + ((x & 1) | ((y & 1) << 1) | ((x & 2) << 1) |
                               ((y & 2) << 2) | ((x & 4) << 2) | ((y & 4) << 3));
            dst[pos] = src[y * w + x];
        }
    }
    C3D_TexFlush(tex);
}

// Extract full-res luma from RGB565 for quirc.
static void rgb565_to_luma(const u16 *src, u8 *out, int w, int h) {
    for (int i = 0; i < w * h; i++) {
        u16 px = src[i];
        int r = (px >> 11) << 3;
        int g = ((px >> 5) & 0x3f) << 2;
        int b = (px & 0x1f) << 3;
        out[i] = (u8)((r * 77 + g * 150 + b * 29) >> 8);
    }
}

static bool payload_is_valid_url(const uint8_t *payload, int len) {
    if (len < 12 || len > 500) return false;
    if (memcmp(payload, "http://", 7) != 0 &&
        memcmp(payload, "https://", 8) != 0) return false;
    return true;
}

// If the scanned URL is a 3ds.thecog.dev short link, GET it and extract
// the real LAN/tunnel URL from the JSON response. Returns true if
// resolved successfully (out_url updated), false if not a short link
// or resolution failed (out_url unchanged).
static bool resolve_short_link(const char *scanned, char *out_url, size_t out_size) {
    if (!strstr(scanned, "3ds.thecog.dev/")) return false;

    char *body = NULL;
    size_t body_len = 0;
    int code = cog_http_get(scanned, &body, &body_len);
    if (code != 200 || !body) { if (body) free(body); return false; }

    // Parse JSON: {"lan":"...", "tunnel":"..."}
    cJSON *root = cJSON_Parse(body);
    free(body);
    if (!root) return false;

    // Prefer LAN (HTTP, direct connection) over tunnel.
    // If only tunnel (HTTPS) is available, use the Worker as an
    // HTTP-to-HTTPS proxy: http://3ds.thecog.dev/p/CODE/
    // so the 3DS never needs to speak HTTPS itself.
    cJSON *lan = cJSON_GetObjectItemCaseSensitive(root, "lan");
    cJSON *tunnel = cJSON_GetObjectItemCaseSensitive(root, "tunnel");

    bool ok = false;
    if (cJSON_IsString(lan) && lan->valuestring[0]) {
        // LAN available — use directly (HTTP, fast)
        strncpy(out_url, lan->valuestring, out_size - 1);
        out_url[out_size - 1] = '\0';
        ok = true;
    } else if (cJSON_IsString(tunnel) && tunnel->valuestring[0]) {
        // No LAN — build proxy URL from the short code
        // Extract code from scanned URL: "http://3ds.thecog.dev/XXXXXXXX"
        const char *code_start = strrchr(scanned, '/');
        if (code_start && code_start[1]) {
            snprintf(out_url, out_size, "http://3ds.thecog.dev/p/%s/", code_start + 1);
            ok = true;
        }
    }
    cJSON_Delete(root);
    return ok;
}

static void draw_status_frame(CogRender *r, const char *top_big,
                              const char *bot_status, int scan_count) {
    u64 now = osGetTime();
    float pulse = 0.5f + 0.5f * sinf((now % 1000) / 1000.0f * 6.28318f);
    u32 pulse_color = C2D_Color32f(1.0f, 0.84f * pulse, 0.43f * pulse, 1.0f);

    cog_render_frame_begin(r);
    cog_render_target_top(r, THEME_BG_DARK);
    cog_render_text(r, top_big, 80, 80, THEME_FONT_HEADER, pulse_color);
    cog_render_text(r, "Point outer camera at the QR.",
                    60, 120, THEME_FONT_LABEL, THEME_TEXT_DIMMED);
    cog_render_text(r, "Hold steady ~15 cm away.",
                    60, 145, THEME_FONT_LABEL, THEME_TEXT_DIMMED);
    cog_render_target_bottom(r, THEME_BG_CANVAS);
    cog_render_text(r, "QR Scanner", 12, 12, THEME_FONT_HEADER, THEME_GOLD);
    cog_render_text(r, bot_status, 12, 60, THEME_FONT_LABEL, THEME_TEXT_PRIMARY);
    char buf[32];
    snprintf(buf, sizeof(buf), "Frames: %d", scan_count);
    cog_render_text(r, buf, 12, 90, THEME_FONT_LABEL, THEME_TEXT_DIMMED);
    cog_render_text(r, "[B] cancel", 12, 210, THEME_FONT_FOOTER, THEME_TEXT_DIMMED);
    cog_render_frame_end(r);
}

static void draw_preview_frame(CogRender *r, C2D_Image *preview,
                               const char *bot_status, int scan_count) {
    cog_render_frame_begin(r);
    cog_render_target_top(r, THEME_BG_DARK);
    C2D_DrawImageAt(*preview, 0, 0, 0.5f, NULL, 1.0f, 1.0f);
    cog_render_rect(0, 210, 400, 30, C2D_Color32(0x0d, 0x0d, 0x0d, 0xcc));
    cog_render_text(r, "Aim at QR code", 140, 215, THEME_FONT_LABEL, THEME_GOLD);
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
    u16 *cam_buf = NULL;
    struct quirc *q = NULL;
    C3D_Tex preview_tex;
    bool has_preview = false;

    // ── Init camera (RGB565 output, same as FBI) ─────────────────────────
    Result rc = camInit();
    if (R_FAILED(rc)) {
        draw_status_frame(render, "SCANNING...", "camInit failed", 0);
        goto cleanup;
    }

    CAMU_SetSize(SELECT_OUT1, SIZE_CTR_TOP_LCD, CONTEXT_A);
    CAMU_SetOutputFormat(SELECT_OUT1, OUTPUT_RGB_565, CONTEXT_A);
    CAMU_SetFrameRate(SELECT_OUT1, FRAME_RATE_30);
    CAMU_SetNoiseFilter(SELECT_OUT1, true);
    CAMU_SetAutoExposure(SELECT_OUT1, true);
    CAMU_SetAutoWhiteBalance(SELECT_OUT1, true);

    u32 transferUnit = 0;
    CAMU_GetMaxBytes(&transferUnit, CAM_WIDTH, CAM_HEIGHT);
    CAMU_SetTransferBytes(PORT_CAM1, transferUnit, CAM_WIDTH, CAM_HEIGHT);
    CAMU_SetTrimming(PORT_CAM1, false);
    CAMU_Activate(SELECT_OUT1);
    CAMU_ClearBuffer(PORT_CAM1);

    cam_buf = (u16 *)linearAlloc(CAM_BUF_SIZE);
    if (!cam_buf) {
        draw_status_frame(render, "SCANNING...", "alloc failed", 0);
        goto cleanup;
    }

    CAMU_StartCapture(PORT_CAM1);

    // ── Preview texture (512x256 RGB565) ─────────────────────────────────
    // Force linearAlloc (not VRAM) so CPU writes + C3D_TexFlush actually
    // update the GPU's view each frame. C3D_TexInit prefers VRAM which
    // makes C3D_TexFlush a no-op after the first write.
    memset(&preview_tex, 0, sizeof(preview_tex));
    {
        u32 tex_size = 512 * 256 * sizeof(u16);
        void *tex_data = linearAlloc(tex_size);
        if (tex_data) {
            memset(tex_data, 0, tex_size);
            preview_tex.data = tex_data;
            preview_tex.fmt = (u16)GPU_RGB565;
            preview_tex.size = tex_size;
            preview_tex.width = 512;
            preview_tex.height = 256;
            preview_tex.param = GPU_TEXTURE_MAG_FILTER(GPU_NEAREST) |
                                GPU_TEXTURE_MIN_FILTER(GPU_NEAREST);
            has_preview = true;
        }
    }

    Tex3DS_SubTexture subtex = {
        .width  = CAM_WIDTH,
        .height = CAM_HEIGHT,
        .left   = 0.0f,
        .top    = 1.0f,
        .right  = (float)CAM_WIDTH / 512.0f,
        .bottom = 1.0f - (float)CAM_HEIGHT / 256.0f,
    };
    C2D_Image preview_img = { &preview_tex, &subtex };

    // ── Init quirc ───────────────────────────────────────────────────────
    q = quirc_new();
    if (!q || quirc_resize(q, Q_WIDTH, Q_HEIGHT) < 0) {
        draw_status_frame(render, "SCANNING...", "quirc init failed", 0);
        goto cleanup;
    }

    // ── Capture + decode loop ────────────────────────────────────────────
    int scan_count = 0;
    char last_error[64] = "";

    while (aptMainLoop()) {
        hidScanInput();
        if (hidKeysDown() & KEY_B) break;

        Handle recv_event = 0;
        CAMU_SetReceiving(&recv_event, cam_buf, PORT_CAM1,
                          CAM_BUF_SIZE, (s16)transferUnit);
        Result wait_rc = svcWaitSynchronization(recv_event, 300000000LL);
        if (recv_event) { svcCloseHandle(recv_event); recv_event = 0; }
        if (R_FAILED(wait_rc)) {
            strncpy(last_error, "capture timeout", sizeof(last_error) - 1);
            draw_status_frame(render, "SCANNING...", last_error, scan_count);
            continue;
        }

        scan_count++;

        // Upload to GPU texture (FBI-style software tiling + cache flush)
        if (has_preview) {
            GSPGPU_FlushDataCache(cam_buf, CAM_BUF_SIZE);
            tile_and_upload(&preview_tex, cam_buf, CAM_WIDTH, CAM_HEIGHT);
        }

        // Feed downsampled luma to quirc
        int qw = 0, qh = 0;
        u8 *qbuf = quirc_begin(q, &qw, &qh);
        if (qbuf && qw == Q_WIDTH && qh == Q_HEIGHT) {
            rgb565_to_luma(cam_buf, qbuf, CAM_WIDTH, CAM_HEIGHT);
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
                        // If it's a 3ds.thecog.dev short link, resolve it
                        resolve_short_link(out_url, out_url, out_size);
                        success = true;
                        if (has_preview)
                            draw_preview_frame(render, &preview_img,
                                               "GOT IT!", scan_count);
                        else
                            draw_status_frame(render, "GOT IT!",
                                              "decoded URL!", scan_count);
                        for (int f = 0; f < 45; f++) gspWaitForVBlank();
                        goto cleanup;
                    } else {
                        strncpy(last_error, "QR found but not a URL",
                                sizeof(last_error) - 1);
                    }
                } else if (err != QUIRC_ERROR_DATA_ECC &&
                           err != QUIRC_ERROR_FORMAT_ECC) {
                    strncpy(last_error, quirc_strerror(err),
                            sizeof(last_error) - 1);
                }
            }
        }

        const char *status = last_error[0] ? last_error : "scanning...";
        if (has_preview)
            draw_preview_frame(render, &preview_img, status, scan_count);
        else
            draw_status_frame(render, "SCANNING...", status, scan_count);
    }

cleanup:
    if (has_preview && preview_tex.data) linearFree(preview_tex.data);
    if (q) quirc_destroy(q);
    CAMU_StopCapture(PORT_CAM1);
    bool busy = false;
    while (R_SUCCEEDED(CAMU_IsBusy(&busy, PORT_CAM1)) && busy)
        svcSleepThread(1000000);
    CAMU_ClearBuffer(PORT_CAM1);
    CAMU_Activate(SELECT_NONE);
    camExit();
    if (cam_buf) linearFree(cam_buf);
    return success;
}
