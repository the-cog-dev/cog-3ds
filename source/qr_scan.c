#include "qr_scan.h"
#include "quirc/quirc.h"

#include <3ds.h>
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

// Status message on the bottom (PrintConsole) screen. Repaints the whole
// bottom; caller has already consoleSelect'd it.
static void draw_bottom(const char *status, int scan_count) {
    printf("\x1b[2J\x1b[1;1H");
    printf("\n  \x1b[33mQR Scanner\x1b[0m\n\n");
    printf("  \x1b[37mPoint the outer camera\n");
    printf("  at the QR code on your PC.\n\n");
    printf("  Status: %s\n", status);
    printf("  Frames: %d\n\n", scan_count);
    printf("  \x1b[37m[B]\x1b[0m cancel\n");
}

// Big "SCANNING" text on top for blind-aim feedback.
static void draw_top(const char *big) {
    printf("\x1b[2J\x1b[1;1H");
    printf("\n\n\n");
    printf("    \x1b[33m%s\x1b[0m\n\n", big);
    printf("    Point outer camera at the\n");
    printf("    QR code shown in Cog's\n");
    printf("    Settings -> Remote View.\n\n");
    printf("    Hold steady ~15 cm away.\n");
}

bool cog_qr_scan(char *out_url, size_t out_size) {
    if (!out_url || out_size == 0) return false;
    out_url[0] = '\0';

    PrintConsole *top = consoleGetDefault();  // save whatever was active
    (void)top;

    // Get console handles — both already initialized by main().
    PrintConsole top_c, bot_c;
    consoleInit(GFX_TOP, &top_c);
    consoleInit(GFX_BOTTOM, &bot_c);

    consoleSelect(&top_c);
    draw_top("SCANNING...");
    consoleSelect(&bot_c);
    draw_bottom("initializing camera", 0);

    bool success = false;
    uint8_t *frame = NULL;
    struct quirc *q = NULL;

    // ── Init camera ──────────────────────────────────────────────────────
    Result rc = camInit();
    if (R_FAILED(rc)) {
        consoleSelect(&bot_c);
        draw_bottom("camInit failed", 0);
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
        consoleSelect(&bot_c);
        draw_bottom("frame alloc failed", 0);
        goto cleanup;
    }

    CAMU_Activate(SELECT_OUT1);
    CAMU_ClearBuffer(PORT_CAM1);
    CAMU_StartCapture(PORT_CAM1);

    // ── Init quirc ───────────────────────────────────────────────────────
    q = quirc_new();
    if (!q || quirc_resize(q, Q_WIDTH, Q_HEIGHT) < 0) {
        consoleSelect(&bot_c);
        draw_bottom("quirc init failed", 0);
        goto cleanup;
    }

    consoleSelect(&bot_c);
    draw_bottom("scanning...", 0);

    // ── Capture + decode loop ────────────────────────────────────────────
    int scan_count = 0;
    const char *last_error = "";

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
            last_error = "capture timeout";
            consoleSelect(&bot_c);
            draw_bottom(last_error, scan_count);
            continue;
        }

        scan_count++;

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
                        consoleSelect(&top_c);
                        draw_top("GOT IT!");
                        consoleSelect(&bot_c);
                        draw_bottom("decoded URL", scan_count);
                        // short pause so user sees confirmation
                        for (int f = 0; f < 45; f++) gspWaitForVBlank();
                        goto cleanup;
                    } else {
                        last_error = "QR found but not a URL";
                    }
                } else if (err != QUIRC_ERROR_DATA_ECC && err != QUIRC_ERROR_FORMAT_ECC) {
                    last_error = quirc_strerror(err);
                }
            }
        }

        // Update status every ~5 frames to avoid console flicker.
        if (scan_count % 5 == 0) {
            consoleSelect(&bot_c);
            draw_bottom(last_error[0] ? last_error : "scanning...", scan_count);
        }

        gspWaitForVBlank();
    }

cleanup:
    if (q) quirc_destroy(q);
    CAMU_StopCapture(PORT_CAM1);
    CAMU_Activate(SELECT_NONE);
    camExit();
    if (frame) linearFree(frame);
    return success;
}
