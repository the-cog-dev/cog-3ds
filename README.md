# The Cog — Nintendo 3DS

Native 3DS homebrew client for [The Cog](https://thecog.dev) — AI-native agent
orchestration IDE. Spawn, monitor, and command your AI agents from a Nintendo 3DS.

This is a sister project to the main Cog Electron app. **Not a fork** — it talks
to a running Cog instance via HTTP over LAN (or HTTPS via Cloudflare tunnel,
once we add wolfSSL support).

## Status

**Phase 1 — shipped.** Real connection to a running Cog instance over LAN.
Top screen shows selected agent detail (name / CLI / model / role / status),
bottom screen lists all agents with color-coded status dots. Polls `/state`
every 5 seconds. D-pad to navigate, A to refresh, Y to view the configured URL.

**Phase 1.5 — shipped.** In-app QR code scanner. Open The Cog in Remote View,
set Settings → Remote View → Enable LAN access, hit the QR icon, point your
3DS camera at the screen. URL auto-saves to `sdmc:/3ds/cog-3ds/config.txt`.
No more typing IPs.

**Planned:**
- Phase 2 — migrate from `PrintConsole` to `citro2d`: real card UI, theme colors, pan/zoom
- Phase 3 — workshop actions: send messages, spawn/kill agents, pinboard detail
- Phase 4 — wolfSSL for HTTPS so it works over the Cloudflare tunnel (away from home)

## Install

### Option A — `.3dsx` via Homebrew Launcher

1. Copy `cog-3ds.3dsx` to `sdmc:/3ds/cog-3ds/cog-3ds.3dsx` on your SD card
2. Boot Homebrew Launcher → launch The Cog

### Option B — `.cia` on the HOME menu (recommended)

Lives on your HOME menu like any other app. No Homebrew Launcher needed after install.

1. `bash build.sh` (builds `.3dsx` / `.elf`)
2. `bash build-cia.sh` (packages everything into `cog-3ds.cia`)
3. Copy `cog-3ds.cia` to SD card root
4. Boot Homebrew Launcher → **FBI** → SD → `cog-3ds.cia` → A → **Install and delete CIA**
5. Exit to HOME — golden gear icon is now an app

## First-time setup

On first launch, The Cog shows a setup screen if `sdmc:/3ds/cog-3ds/config.txt` is missing.

**Easy path (QR scan):**
1. On your PC, open The Cog → Settings → Remote View → toggle **Enable LAN access**
2. Copy the LAN URL that appears (ends in `/r/<token>/`)
3. Pick **Plain QR** in the QR selector (less ornate, easier for the 3DS camera)
4. On the 3DS app's setup screen, press **X** to scan
5. Point the outer camera at your PC screen; the URL saves automatically

**Fallback (manual):**
1. Create `sdmc:/3ds/cog-3ds/config.txt`
2. Paste the full LAN URL on a single line (e.g. `http://192.168.2.10:58324/r/abc123/`)
3. Relaunch the app

## Controls

**Main screen:**
- **D-pad UP/DOWN** — navigate agent list
- **A**             — refresh now
- **Y**             — show configured URL
- **START**         — exit

**Setup screen:**
- **X**             — open QR scanner
- **START**         — exit

**QR scan screen:**
- Point outer camera at a QR code on your PC screen
- Scanner auto-decodes and saves the URL
- **B** to cancel back to setup

## Build

Requires devkitPro with `3ds-dev` installed. On Windows, use devkitPro's bundled
msys2 bash (`bash build.sh` handles loading the profile for you).

```bash
# Build the .3dsx
bash build.sh

# Build + deploy over WiFi (3dslink)
bash build.sh deploy

# Package as .cia for HOME menu install
bash build-cia.sh
```

Output files:
- `cog-3ds.3dsx` — Homebrew Launcher executable
- `cog-3ds.elf`  — unstripped ELF (input for CIA build)
- `cog-3ds.cia`  — HOME menu installable

## Project layout

```
cog-3ds/
├── source/
│   ├── main.c           # entry point, UI, main loop
│   ├── http.c/.h        # libctru httpc wrapper
│   ├── config.c/.h      # sdmc:/3ds/cog-3ds/config.txt persistence
│   ├── qr_scan.c/.h     # camera capture + QR decode
│   ├── cJSON.c/.h       # vendored JSON parser
│   └── quirc/           # vendored QR decoder
├── meta/
│   ├── icon.png         # 48x48 gear — SMDH icon
│   ├── banner.png       # 256x128 — HOME menu banner
│   ├── silence.wav      # 3s silence — banner sound
│   └── cog.rsf          # makerom spec for CIA build
├── tools/
│   ├── bannertool.exe   # 256x128 PNG -> .bnr
│   └── makerom.exe      # .elf + .bnr + .icn -> .cia
├── build.sh             # .3dsx build wrapper
└── build-cia.sh         # .cia packaging pipeline
```

## Credits

- **libctru** — devkitPro 3DS C library
- **cJSON** — Dave Gamble's JSON parser
- **quirc** — Daniel Beer's pure-C QR decoder
- **bannertool** (Steveice10, diasurgical mirror) + **makerom** (3DSGuy) — CIA pipeline

## License

MIT
