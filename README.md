# The Cog — Nintendo 3DS

Native 3DS homebrew client for [The Cog](https://thecog.dev) — an AI-native agent orchestration IDE. Monitor, message, and control your AI agents from a Nintendo 3DS.

Connects to a running Cog desktop instance over LAN or Cloudflare tunnel.

## Features

- **Agent cards on an infinite canvas** — pan with the circle pad, zoom with L/R, tap to select
- **Full workshop controls** — message agents, view output, kill agents, spawn from presets
- **Pinboard** — view tasks in 3 tabs (Open / In Progress / Completed), claim and complete tasks
- **Info channel** — browse notes posted by agents
- **Schedules** — view and pause/resume scheduled prompts
- **QR code setup** — scan a QR from your desktop to connect instantly
- **Drag and snap** — long-press to lift cards, snap to grid on drop
- **Theme colors** — agent cards match your desktop workspace theme
- **PIN authentication** — workshop passcode gate for security

## Install

### Option A — FBI QR Install (easiest)

Scan this QR code with FBI to download and install directly:

> Go to **FBI** → **Remote Install** → **Scan QR Code** → point at the QR below

![CIA Download QR](https://api.qrserver.com/v1/create-qr-code/?size=200x200&data=https://github.com/the-cog-dev/cog-3ds/releases/latest/download/cog-3ds.cia)

### Option B — Manual CIA Install

1. Download `cog-3ds.cia` from [the latest release](https://github.com/the-cog-dev/cog-3ds/releases/latest)
2. Copy it to your SD card
3. Open **FBI** → **SD** → select `cog-3ds.cia` → **Install and delete CIA**
4. Exit to HOME menu — the golden gear icon is now an app

### Option C — Homebrew Launcher

1. Download `cog-3ds.3dsx` from [the latest release](https://github.com/the-cog-dev/cog-3ds/releases/latest)
2. Copy to `sdmc:/3ds/cog-3ds/cog-3ds.3dsx`
3. Open Homebrew Launcher → launch The Cog

## Setup

On first launch you'll see a setup screen.

**QR scan (recommended):**
1. On your PC: open The Cog → Settings → Remote View → enable **LAN access**
2. Click **Show QR** → check **Plain QR** (easier for the 3DS camera)
3. On the 3DS: press **X** to scan → point the outer camera at your screen
4. URL saves automatically — you're connected

**Manual:**
1. Create `sdmc:/3ds/cog-3ds/config.txt`
2. Paste the full LAN URL on one line (e.g. `http://192.168.2.10:58324/r/abc123/`)
3. Relaunch the app

## Controls

| Button | Action |
|--------|--------|
| **Circle Pad** | Pan the canvas |
| **D-pad** | Navigate between cards / scroll detail views |
| **A** | Open action menu (message, kill, view output, etc.) |
| **B** | Deselect current card |
| **L / R** | Zoom in/out (or switch pinboard tabs when viewing pinboard) |
| **Y** | Fit all cards on screen |
| **X** | Scan QR code (setup screen) |
| **Touch** | Tap to select, drag to pan, long-press to lift and move |
| **SELECT** | Return to setup screen |
| **START** | Exit |

## Build from Source

Requires [devkitPro](https://devkitpro.org/) with `3ds-dev` installed.

```bash
# Build .3dsx
bash build.sh

# Build + deploy over WiFi (3dslink)
bash build.sh deploy

# Package as .cia
bash build-cia.sh
```

## Credits

- **libctru** / **citro2d** — devkitPro 3DS libraries
- **cJSON** — Dave Gamble's JSON parser
- **quirc** — Daniel Beer's pure-C QR decoder
- **bannertool** + **makerom** — CIA packaging pipeline

## License

MIT
