# The Cog — Nintendo 3DS

Native 3DS homebrew client for [The Cog](https://thecog.dev) — AI-native agent
orchestration IDE. Spawn, monitor, and command your AI agents from a Nintendo 3DS.

This is a sister project to the main Cog Electron app. **Not a fork** — it talks
to a running Cog instance via HTTP over LAN (or HTTPS via Cloudflare tunnel,
once we add wolfSSL support).

## Status

**v0 (current)** — toolchain validation. App boots, displays branded title on
both screens, responds to input. No networking yet.

**Planned phases:**
- Phase 1: Two-screen scaffold + touch input
- Phase 2: HTTP client over LAN, real `/state` polling, JSON parsing
- Phase 3: Camera + QR scanner for URL setup
- Phase 4: wolfSSL for HTTPS so it works over the Cloudflare tunnel
- Phase 5: Workshop features — canvas, spawn, panels, themes

## Build

Requires devkitPro with `3ds-dev` installed.

```bash
# From an MSYS2 shell with DEVKITPRO + DEVKITARM env vars set:
make
```

Output: `cog-3ds.3dsx` in the project root.

## Deploy

Copy `cog-3ds.3dsx` to your 3DS SD card under `/3ds/`, then launch via the
Homebrew Launcher.

If you're using `3dslink` over WiFi:
```bash
3dslink cog-3ds.3dsx
```

## Controls

- **START**  — exit to Homebrew Launcher
- **SELECT** — reset heartbeat counter
- **Touch**  — coordinates shown on bottom screen

## Layout

- **Top screen** (400×240) — title card, status, planned: agent detail view
- **Bottom screen** (320×240, touchscreen) — heartbeat now, planned: agent list / canvas

## License

MIT
