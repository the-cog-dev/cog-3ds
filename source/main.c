// The Cog — Nintendo 3DS prototype (v0)
//
// Boots into a two-screen UI:
//   - Top screen (400x240): branded title card + version + controls hint
//   - Bottom screen (320x240, touchscreen): status pane with a heartbeat counter
//     so you can confirm the app is alive
//
// This is the toolchain-validation step. No networking yet. Once this builds,
// runs on the 3DS, and exits cleanly, we know our entire build → deploy →
// execute pipeline works. Then we add the real features.
//
// Controls:
//   START   exit
//   SELECT  reset heartbeat counter

#include <3ds.h>
#include <stdio.h>

int main(void) {
    // Boot core 3DS services. gfxInitDefault() spins up framebuffers for both
    // screens. consoleInit hooks printf to a screen-rendered text console — fast
    // path to "I can see what my code is doing" without dragging in citro2d yet.
    gfxInitDefault();

    PrintConsole topScreen, bottomScreen;
    consoleInit(GFX_TOP, &topScreen);
    consoleInit(GFX_BOTTOM, &bottomScreen);

    // ── Top screen: branding ─────────────────────────────────────────────────
    consoleSelect(&topScreen);
    printf("\x1b[2J");  // clear screen
    printf("\x1b[1;1H"); // move cursor to top-left

    printf("\n");
    printf("    \x1b[33m  ___  _____  ___\x1b[0m\n");
    printf("    \x1b[33m / _ \\| ____|/ _ \\\x1b[0m       The Cog\n");
    printf("    \x1b[33m| (_) | |    | (_) |\x1b[0m       v0.1 prototype\n");
    printf("    \x1b[33m \\___/|_|     \\___/\x1b[0m\n");
    printf("\n");
    printf("    \x1b[36mAI-native agent orchestration\x1b[0m\n");
    printf("    \x1b[36mfrom your Nintendo 3DS\x1b[0m\n");
    printf("\n");
    printf("    --------------------------------\n");
    printf("\n");
    printf("    \x1b[32mTOOLCHAIN OK\x1b[0m  Build pipeline verified.\n");
    printf("\n");
    printf("    Next: HTTP client + agent list.\n");
    printf("\n");
    printf("    \x1b[37m[START]\x1b[0m   exit to Homebrew Launcher\n");
    printf("    \x1b[37m[SELECT]\x1b[0m  reset heartbeat\n");

    // ── Bottom screen: heartbeat ─────────────────────────────────────────────
    consoleSelect(&bottomScreen);
    printf("\x1b[2J");

    u32 frame = 0;
    u32 lastTouch = 0;

    // Main loop. aptMainLoop returns false when the system asks the app to exit
    // (e.g. user pressed HOME and chose Close). Always honor it.
    while (aptMainLoop()) {
        hidScanInput();
        u32 down = hidKeysDown();
        u32 held = hidKeysHeld();

        if (down & KEY_START) break;
        if (down & KEY_SELECT) frame = 0;

        // Track latest touch for the bottom-screen status display
        if (held & KEY_TOUCH) {
            touchPosition touch;
            hidTouchRead(&touch);
            lastTouch = (touch.px << 16) | touch.py;
        }

        // Refresh bottom screen ~10 fps so the heartbeat number is visible
        // without flickering.
        if (frame % 6 == 0) {
            consoleSelect(&bottomScreen);
            printf("\x1b[1;1H");
            printf("  \x1b[33m== STATUS ==\x1b[0m\n\n");
            printf("  Heartbeat:    \x1b[32m%lu\x1b[0m       \n", frame);
            printf("  Last touch:   \x1b[36m%lu, %lu\x1b[0m       \n",
                   (lastTouch >> 16) & 0xFFFF, lastTouch & 0xFFFF);
            printf("\n");
            printf("  \x1b[37mTap the bottom screen.\x1b[0m\n");
            printf("  \x1b[37mIf you see this counting,\x1b[0m\n");
            printf("  \x1b[37mthe runtime is good.\x1b[0m\n");
        }
        frame++;

        // Push framebuffers to the LCD and wait for VSync (~60 fps cap).
        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }

    gfxExit();
    return 0;
}
