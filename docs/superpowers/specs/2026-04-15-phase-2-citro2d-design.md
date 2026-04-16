# Phase 2 — citro2d Visual Overhaul + Workshop Canvas

**Status:** Approved design, ready for plan.
**Scope:** Phase 2a (visual upgrade) + Phase 2b (canvas with pan/zoom + bidirectional position sync). Phase 2c (live camera preview in QR scanner) is **out of scope** — deferred to a follow-up.

## Goal

Replace the terminal-style `PrintConsole` UI with a proper GPU-rendered interface that mirrors the desktop Cog workshop. Agents appear as cards positioned at their actual desktop workshop coordinates; the user can pan, zoom, and long-press-to-lift cards to drag them, with moves syncing back to the desktop.

## Why

- Phase 1 proved the 3DS can be a real client for Cog. The terminal UI works but feels like a debug viewer.
- Desktop Cog uses spatial layout as a primary mental model (workshop canvas). The 3DS should reflect that, not flatten it to a list.
- Bidirectional position sync is the "wow" feature: a user reorganizing their workshop from the 3DS sees the desktop update live, and vice versa.

## Scope across repos

This spec touches **two repos**:

1. **cog-3ds** (this repo) — all 3DS-side changes. Main work.
2. **AgentOrch** — ~15 LOC in `src/main/remote/remote-server.ts` to enhance `/state` with per-agent position data. This lands as a small separate commit to the main repo but is a hard prereq for the 3DS canvas to render at desktop positions.

The implementation plan will include both as Task 0 (desktop) and Tasks 1–N (3DS).

## Non-goals

- Camera preview during QR scan (Phase 2c, deferred).
- Groups/panels/pinboard as first-class canvas entities (Phase 3+).
- Spawn/kill agents from 3DS (Phase 3).
- Theme switching (hardcoded Sunshine palette for now).
- Smooth animated zoom (L/R steps instantly).
- Multi-touch gestures (pinch zoom etc). Single-touch only.

## Architecture

### Rendering pipeline

Swap `PrintConsole` for **citro2d** (libctru's GPU 2D library). Each frame:

```
C2D_SceneBegin(top_target)
  draw_header()          // "⚙ The Cog" + project name
  draw_detail_card()     // selected agent full info
  draw_footer_hints()    // [A] refresh [X] rescan [L/R] zoom
C2D_SceneBegin(bottom_target)
  draw_canvas_background()
  for each agent card: draw_card(card, camera_transform)
  draw_selection_ring()  // if any
  draw_lifted_card()     // on top if drag in progress
```

Both screens render at native 60fps. Polling (every 5s) runs out-of-band and swaps a shared `CogState` struct between frames.

### File structure

```
source/
├── main.c          (~150 LOC)  entry, event loop, state transitions
├── http.c/h                    (unchanged)
├── config.c/h                  (unchanged)
├── qr_scan.c/h                 (unchanged; swap PrintConsole for citro2d status later)
├── cJSON.c/h                   (unchanged)
├── quirc/                      (unchanged)
├── theme.c/h       (~80  LOC)  color palette, font sizes, static consts
├── render.c/h      (~120 LOC)  citro2d init, frame begin/end, text helpers
├── canvas.c/h      (~250 LOC)  bottom screen: pan/zoom/hit-test/draw cards
├── card.c/h        (~100 LOC)  single agent card draw + lift animation state
└── detail.c/h      (~80  LOC)  top screen: selected agent info layout
```

Total new code: ~780 LOC across 6 new files. Each file under 300 LOC, clear boundaries.

### State model

```c
typedef struct {
    char id[64];
    char name[64];
    char cli[24];
    char model[32];
    char role[24];
    char status[16];
    float x, y;          // canvas position (from desktop)
    float width, height; // card dimensions (from desktop)
    uint32_t color;      // RGBA card tint (from desktop)
} AgentInfo;

typedef struct {
    float cam_x, cam_y;  // canvas pan offset
    float cam_zoom;      // 0.5 .. 2.0
    int selected_idx;    // -1 = none
    int lifted_idx;      // -1 = not dragging
    float lift_scale;    // 1.0 .. 1.2 during lift animation
    uint32_t lift_start_ms;
    float touch_grab_dx, touch_grab_dy; // where on card user grabbed
} ViewState;
```

## Data flow

### Pull: desktop → 3DS

`/state` endpoint gets 5 new fields per agent — `x`, `y`, `width`, `height`, `color`. These mirror what the desktop workshop already tracks in its `windows` array. Backend change is ~15 LOC in `src/main/remote/remote-server.ts`: merge window position into the existing agent objects.

3DS polls every 5 seconds as today. On successful poll, reconcile: any agent whose `id` matches an existing card keeps the card's local state (so an in-progress drag isn't yanked); new agents get added; missing agents get removed.

### Push: 3DS → desktop

Existing endpoint `/workshop/window/:id` accepts `{x, y, width?, height?}`. On **drag release only** (not per drag tick), 3DS POSTs the final `{x, y}` for the dragged card. This reuses infrastructure built for the mobile remote view.

Debounce: no per-frame posts; only on release. This keeps LAN traffic quiet and respects desktop's reconciliation logic.

### Conflict handling

Last-write-wins per axis. Between polls, 3DS local visual state is authoritative for cards the 3DS has touched; next poll reconciles. If desktop moves a card while 3DS is idle, next poll shows the new position. If both move simultaneously, next poll's value wins on the 3DS side and vice versa — jitter is acceptable for solo-use.

## Theme palette

All colors defined in `theme.c` as `C2D_Color32f` constants:

| Name                | Hex        | Use |
|---------------------|------------|-----|
| `THEME_BG_DARK`     | `#0d0d0d`  | Main background, both screens |
| `THEME_BG_CANVAS`   | `#141414`  | Bottom canvas background (slight lift from main bg) |
| `THEME_GOLD`        | `#f5d76e`  | Primary accent, title, selection ring |
| `THEME_GOLD_DIM`    | `#a88e42`  | Dimmed accent, borders |
| `THEME_TEXT_PRIMARY`| `#f5f5f5`  | Body text |
| `THEME_TEXT_DIMMED` | `#888888`  | Label text, hints |
| `THEME_DIVIDER`     | `#2a2a2a`  | Hairline dividers |
| `THEME_STATUS_WORKING`      | `#f5d76e` (gold/yellow) | agent status=working |
| `THEME_STATUS_ACTIVE`       | `#6ed76e` (green)       | agent status=active |
| `THEME_STATUS_IDLE`         | `#888888` (grey)        | agent status=idle |
| `THEME_STATUS_DISCONNECTED` | `#d76e6e` (red)         | agent status=disconnected |
| `THEME_LIFT_GLOW`    | `#f5d76e` at 40% alpha  | Lift animation glow |

Font sizes (citro2d system font scale factor):

| Use          | Scale | ~Pixel size |
|--------------|-------|-------------|
| Header title | 0.75  | ~24px       |
| Card name    | 0.5   | ~16px       |
| Detail label | 0.45  | ~14px       |
| Footer hints | 0.4   | ~12px       |

## UI

### Top screen (400×240)

```
┌───────────────────────────────────────┐
│  ⚙ The Cog            [project name]  │  header bar (24px, gold)
├───────────────────────────────────────┤
│                                       │
│   ● CoderCharlie                      │  status dot + name (32px)
│                                       │
│   CLI       Claude Code               │
│   Model     opus-4-6                  │  label/value pairs (16px)
│   Role      Code reviewer             │
│   Status    working                   │
│                                       │
│   ──────────────────────              │  divider
│   (Phase 3: live output here)         │
│                                       │
├───────────────────────────────────────┤
│  [A] refresh  [X] rescan  [L/R] zoom  │  footer (14px, dimmed)
└───────────────────────────────────────┘
```

If no agent is selected, detail area shows "Tap a card" hint with arrow pointing down.

### Bottom screen (320×240): Workshop canvas

Canvas is the mini-mirror of the desktop workshop. Cards drawn at `(x, y)` translated by `(cam_x, cam_y)` and scaled by `cam_zoom`. Cards clip to viewport to save draw calls.

Card anatomy (at 1.0 zoom):

```
┌──────────────┐
│ ●            │  ← status dot (colored per status)
│ CoderCharlie │  ← name (bold, truncated 12 chars)
│ CC · Reviewer│  ← CLI badge + role (dim)
└──────────────┘
    60×40 px
```

Selected card has a 2px gold border. Lifted card renders at 1.2× scale with a soft gold glow underneath (simple blur-approximation: 3 overlaid alpha-faded rectangles).

### Interactions

Priority order — first match wins for a given input event:

1. **L button (held or pressed)** → `cam_zoom = max(0.5, cam_zoom - 0.1)`, anchor at canvas center
2. **R button** → `cam_zoom = min(2.0, cam_zoom + 0.1)`, anchor at canvas center
3. **D-pad** → cycle-select to nearest card in pressed direction (fallback precision nav)
4. **Touch press (tpDown) inside a card** → if that card is `selected_idx`, start long-press timer; otherwise `selected_idx = this card`
5. **Touch held 500ms on selected card** → begin lift: set `lifted_idx`, start lift animation (150ms ease-out scale 1.0→1.2)
6. **Touch drag while lifted** → update that card's `(x, y)` in world coords (account for zoom when translating screen delta to world delta)
7. **Touch release while lifted** → drop animation (150ms ease-in scale 1.2→1.0), POST `{x, y}` to `/workshop/window/:id`
8. **Touch drag without a lifted card** → pan: `cam_x += dx / cam_zoom`, `cam_y += dy / cam_zoom`
9. **A button** → force immediate re-poll
10. **B button** → deselect (`selected_idx = -1`), or cancel lift if mid-drag (snap card back to pre-drag position)
11. **Y button** → show URL dialog (existing)
12. **X button** → open QR rescan (existing)
13. **START** → exit

### Animation timing

- **Lift**: 150ms ease-out, scale 1.0 → 1.2, glow fades in
- **Drop**: 150ms ease-in, scale back to 1.0, glow fades out
- **Zoom**: instant (L/R buttons step discretely)
- **Selection ring**: no animation, just appears/disappears
- **Card enter (on first poll)**: 200ms fade-in alpha 0 → 1 for new agents
- **Card exit (removed agent)**: 200ms fade-out alpha 1 → 0

All animations use `osGetTime()` (milliseconds since boot) for elapsed time; no per-frame delta accumulation needed.

## Error handling

- **citro2d init fails**: fall back to existing PrintConsole UI with an error banner.
- **Poll fails (network)**: canvas keeps showing last-known cards, footer banner shows "offline", status msg updates. Resume on next successful poll.
- **POST fails on drag release**: log locally, retry once; if still fails, show "sync failed" toast for 2s and revert card position to pre-drag. Don't block UI.
- **Agent has invalid position (NaN, < 0, > 10000)**: clamp to `(0, 0)` and size to default.
- **Too many agents (>MAX_AGENTS = 32)**: already handled; excess silently dropped at parse time.

## Performance budget

- 60 fps target on both screens
- Max 32 cards visible at full zoom — negligible GPU cost
- Culling: skip draw for cards whose AABB is fully outside the viewport after camera transform
- Font rendering: citro2d's built-in system font, pre-loaded once at init
- No texture loading per frame; status colors are solid-fill quads

## Rollback plan

If citro2d integration has showstoppers (build fails, libctru version mismatch, GPU crashes), we keep the PrintConsole UI from Phase 1 intact. No commits removing the existing render path until citro2d replacement is proven working on hardware.

## Testing approach

- **Incremental visual verification on hardware** — this is the primary test. Build, deploy to SD, visually confirm each layer works before moving on.
- No unit tests (3DS has no test harness we can easily run; devkitARM has no built-in test runner).
- Manual test matrix:
  1. citro2d draws on both screens (green rectangle test)
  2. Text renders with font
  3. Poll still works, state populates
  4. Cards render at correct positions
  5. Pan works
  6. Zoom works
  7. Card selection on tap
  8. Long-press lift animation
  9. Drag updates position locally
  10. Release POSTs and desktop updates
  11. Desktop move reflects on 3DS next poll
  12. D-pad fallback nav

## Open questions

None — all design decisions locked during brainstorming.
