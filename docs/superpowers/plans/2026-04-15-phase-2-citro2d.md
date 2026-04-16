# Phase 2 citro2d Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace PrintConsole UI with citro2d-rendered card canvas mirroring the desktop Cog workshop with bidirectional position sync.

**Architecture:** New files (`theme`, `render`, `canvas`, `card`, `detail`) layered on top of existing `main.c` / `http.c` / `config.c`. Desktop `/state` endpoint enhanced with per-agent position/size/color sourced from a new main-process layout cache synced from the renderer.

**Tech Stack:** C + libctru + citro2d (3DS GPU 2D); TypeScript Electron (AgentOrch) on the backend side.

**Spec:** `docs/superpowers/specs/2026-04-15-phase-2-citro2d-design.md`

**Spec simplifications accepted for this push:**
- Card enter/exit fade animations (spec §Animation timing) are **deferred** — cards just pop in/out on poll. Can add in a follow-up if jarring.
- POST-fails-on-release retry + toast + revert (spec §Error handling) is **simplified** to "log, move on". Dropping a move message is acceptable for solo-use; retry logic adds state complexity that isn't worth it right now.
- Frustum culling (spec §Performance) is **skipped** — with MAX_AGENTS=32 there's no perf benefit.

**Testing:** Manual visual verification on 3DS hardware after each task. No unit-test harness available for 3DS code. Backend change has TS type-check.

**Repos touched:**
- `F:\coding\AgentOrch` — Task 0 only (remote server + main process + renderer).
- `F:\coding\cog-3ds` — Tasks 1–12.

---

## Task 0: Enhance `/state` with agent positions (AgentOrch repo)

**Files:**
- Modify: `F:\coding\AgentOrch\src\shared\ipc.ts` (add IPC channel constant)
- Modify: `F:\coding\AgentOrch\src\main\index.ts` (add layout cache + IPC handler + deps callback)
- Modify: `F:\coding\AgentOrch\src\main\remote\remote-server.ts` (wire deps getter into `/state`)
- Modify: `F:\coding\AgentOrch\src\preload\index.ts` (expose sync method to renderer)
- Modify: `F:\coding\AgentOrch\src\renderer\components\Workspace.tsx` (push layout snapshots on window changes)

- [ ] **Step 1: Add the IPC channel constant**

Open `F:\coding\AgentOrch\src\shared\ipc.ts`, find the `IPC` object, and add after the existing `WORKSHOP_WINDOW_UPDATE` entry:

```typescript
WORKSHOP_LAYOUT_SYNC: 'workshop:layout-sync',
```

- [ ] **Step 2: Add layout cache in main process**

Open `F:\coding\AgentOrch\src\main\index.ts`. Near the top, after the `workshopPasscodeHash` declaration (around line 70):

```typescript
// Mirror of the renderer's workshop window layout, kept current via IPC
// from the Workspace component. Consumed by /state so remote clients
// (mobile, 3DS) can render cards at desktop positions.
interface WindowLayoutEntry {
  x: number
  y: number
  width: number
  height: number
  color: string
}
const workshopLayoutCache = new Map<string, WindowLayoutEntry>()
```

- [ ] **Step 3: Add IPC handler for layout sync**

In the same file, find where other `ipcMain.on` / `ipcMain.handle` calls are registered (search for `IPC.WORKSHOP_PANEL_TOGGLE` or similar). Add a new handler:

```typescript
ipcMain.on(IPC.WORKSHOP_LAYOUT_SYNC, (_event, payload: Array<{ id: string } & WindowLayoutEntry>) => {
  workshopLayoutCache.clear()
  for (const entry of payload) {
    if (!entry || typeof entry.id !== 'string') continue
    const { id, x, y, width, height, color } = entry
    if ([x, y, width, height].some(v => typeof v !== 'number' || !isFinite(v))) continue
    workshopLayoutCache.set(id, { x, y, width, height, color: color ?? '#888888' })
  }
})
```

- [ ] **Step 4: Expose layout cache via remote server deps**

Same file, find the `remoteServer = new RemoteServer({ ... deps ... })` block (around line 280). Add a new deps callback:

```typescript
getAgentLayouts: () => {
  const out: Record<string, WindowLayoutEntry> = {}
  for (const [id, entry] of workshopLayoutCache.entries()) out[id] = entry
  return out
},
```

- [ ] **Step 5: Declare the new deps type on RemoteServer**

Open `F:\coding\AgentOrch\src\main\remote\remote-server.ts`. Find the `deps` interface (around line 60-70, where `onWorkshopWindowUpdate` lives). Add:

```typescript
getAgentLayouts: () => Record<string, { x: number; y: number; width: number; height: number; color: string }>
```

- [ ] **Step 6: Merge layouts into `/state` response**

Same file, in the `GET /state` handler (around line 260):

```typescript
this.app.get('/r/:token/state', (_req: Request, res: Response) => {
  const layouts = this.deps.getAgentLayouts()
  const agents = this.deps.getAgents().map(a => {
    const layout = layouts[a.id]
    return layout ? { ...a, x: layout.x, y: layout.y, width: layout.width, height: layout.height, color: layout.color } : a
  })
  const snapshot = {
    projectName: this.deps.getProjectName(),
    agents,
    schedules: this.deps.getSchedules(),
    pinboardTasks: this.deps.getPinboardTasks(),
    infoEntries: this.deps.getInfoEntries(),
    connectionCount: this.deps.tokenManager.getConnectionCount(),
    serverTime: Date.now(),
    sessionExpiresAt: this.deps.tokenManager.getExpiresAt(),
    workshopPasscodeSet: this.deps.getWorkshopPasscodeSet()
  }
  res.json(snapshot)
})
```

- [ ] **Step 7: Expose syncLayout on preload API**

Open `F:\coding\AgentOrch\src\preload\index.ts`. Find the `electronAPI` object. Add a method under the workshop section:

```typescript
syncWorkshopLayout: (layouts: Array<{ id: string; x: number; y: number; width: number; height: number; color: string }>) =>
  ipcRenderer.send(IPC.WORKSHOP_LAYOUT_SYNC, layouts),
```

- [ ] **Step 8: Push layouts from renderer whenever workshop state changes**

Open `F:\coding\AgentOrch\src\renderer\components\Workspace.tsx`. Find where `windows` state lives. Add a `useEffect` that runs on windows or groups change:

```tsx
useEffect(() => {
  const layouts = windows.map(w => {
    const agent = agents.find(a => a.id === w.id)
    const group = agent?.groupId ? groups.find(g => g.id === agent.groupId) : null
    return {
      id: w.id,
      x: w.x,
      y: w.y,
      width: w.width,
      height: w.height,
      color: group?.color ?? '#888888'
    }
  })
  window.electronAPI.syncWorkshopLayout(layouts)
}, [windows, agents, groups])
```

If `windows`, `agents`, or `groups` are named differently in the file, match whatever the local names are. The key input list is: workshop windows, agents, groups.

- [ ] **Step 8b: Update renderer type declaration**

Open `F:\coding\AgentOrch\src\renderer\electron.d.ts`. Find the `electronAPI` interface. Add to the workshop section:

```typescript
syncWorkshopLayout: (layouts: Array<{ id: string; x: number; y: number; width: number; height: number; color: string }>) => void
```

- [ ] **Step 9: Type-check the full change**

```bash
cd F:/coding/AgentOrch
npm run typecheck
```

Expected: no errors. If the existing codebase has pre-existing errors unrelated to this change, that's fine — just make sure nothing you added is listed.

- [ ] **Step 10: Restart Cog and verify `/state`**

```bash
# Make sure Cog (Electron app) is running with Remote View + LAN access enabled.
# From a terminal, curl the /state endpoint (replace with your LAN URL):
curl http://192.168.2.10:58324/r/YOUR_TOKEN/state | python -m json.tool | head -40
```

Expected: at least one agent in `agents` array now has `x`, `y`, `width`, `height`, `color` fields. Values should match what you see in the workshop canvas.

- [ ] **Step 11: Commit**

```bash
cd F:/coding/AgentOrch
git add src/shared/ipc.ts src/main/index.ts src/main/remote/remote-server.ts src/preload/index.ts src/renderer/components/Workspace.tsx
git commit -m "feat(remote): expose agent workshop positions via /state

Adds a main-process layout cache synced from renderer via IPC, exposed
through a new getAgentLayouts() deps callback. Remote /state now merges
per-agent {x,y,width,height,color} so remote clients (3DS especially)
can render the workshop canvas at desktop positions."
```

---

## Task 1: citro2d hello-world (cog-3ds repo)

Establishes the build pipeline with citro2d. Just boots, clears both screens to the dark theme background, and keeps the existing PrintConsole behavior as a fallback if citro2d init fails.

**Files:**
- Modify: `F:\coding\cog-3ds\Makefile`
- Modify: `F:\coding\cog-3ds\source\main.c`

- [ ] **Step 1: Link citro2d and citro3d in Makefile**

Open `F:\coding\cog-3ds\Makefile`, find the `LIBS` line (~line 66):

```makefile
LIBS	:= -lctru -lm
```

Replace with:

```makefile
LIBS	:= -lcitro2d -lcitro3d -lctru -lm
```

- [ ] **Step 2: Boot citro2d before the existing console init**

Open `F:\coding\cog-3ds\source\main.c`. Add near the other includes:

```c
#include <citro2d.h>
```

In `main()`, replace the opening block:

```c
int main(void) {
    gfxInitDefault();

    PrintConsole top, bottom;
    consoleInit(GFX_TOP, &top);
    consoleInit(GFX_BOTTOM, &bottom);
```

With:

```c
int main(void) {
    gfxInitDefault();

    // Try citro2d first. If it fails, fall back to PrintConsole so we
    // never ship a regression from Phase 1.
    bool use_citro2d = false;
    C3D_RenderTarget *top_target = NULL;
    C3D_RenderTarget *bottom_target = NULL;
    if (C3D_Init(C3D_DEFAULT_CMDBUF_SIZE)) {
        if (C2D_Init(C2D_DEFAULT_MAX_OBJECTS)) {
            C2D_Prepare();
            top_target = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
            bottom_target = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
            use_citro2d = (top_target != NULL && bottom_target != NULL);
        }
    }

    PrintConsole top, bottom;
    if (!use_citro2d) {
        consoleInit(GFX_TOP, &top);
        consoleInit(GFX_BOTTOM, &bottom);
    }
```

- [ ] **Step 3: Clear both screens to dark theme color each frame (test render loop)**

Inside the main loop (where you currently call `render_main_screens`), wrap the existing render/print code so citro2d draws both screens when active. Directly before the `if (dirty)` block, insert:

```c
        if (use_citro2d) {
            C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
            C2D_TargetClear(top_target, C2D_Color32(0x0d, 0x0d, 0x0d, 0xff));
            C2D_TargetClear(bottom_target, C2D_Color32(0x0d, 0x0d, 0x0d, 0xff));
            C2D_SceneBegin(top_target);
            // Phase 2a will draw header + detail here
            C2D_SceneBegin(bottom_target);
            // Phase 2b will draw canvas here
            C3D_FrameEnd(0);
            dirty = false;  // citro2d re-renders every frame anyway
        } else if (dirty) {
```

Close the brace at the bottom of the existing `if (dirty) { ... }` block (the whole print-render path now becomes the `else if` branch).

- [ ] **Step 4: Exit citro2d cleanly**

At the end of `main`, before `cog_http_exit()`:

```c
    if (use_citro2d) {
        C2D_Fini();
        C3D_Fini();
    }
```

- [ ] **Step 5: Build and deploy**

```bash
cd F:/coding/cog-3ds
bash build.sh
cp cog-3ds.3dsx H:/3ds/cog-3ds/cog-3ds.3dsx
```

- [ ] **Step 6: Visual verification on hardware**

Launch The Cog from Homebrew Launcher. Expected: both screens are solid dark (#0d0d0d) after setup/load. No PrintConsole text visible. App runs without crashing. D-pad navigation works but you won't see feedback yet. START still exits.

If both screens stay black and the app doesn't crash, citro2d is live.

- [ ] **Step 7: Commit**

```bash
git add Makefile source/main.c
git commit -m "feat(phase-2a): boot citro2d with PrintConsole fallback"
```

---

## Task 2: Theme module

Extract colors and font scales to a central module so later tasks can reference them by name.

**Files:**
- Create: `F:\coding\cog-3ds\source\theme.h`
- Create: `F:\coding\cog-3ds\source\theme.c`

- [ ] **Step 1: Create theme.h**

```c
// Color palette + font scales for The Cog 3DS UI. Matches the
// "Sunshine" theme on desktop (gold on dark). All colors are
// 32-bit RGBA little-endian values suitable for C2D_Color32.

#ifndef COG_THEME_H
#define COG_THEME_H

#include <citro2d.h>

// Backgrounds
extern const u32 THEME_BG_DARK;       // #0d0d0d main bg
extern const u32 THEME_BG_CANVAS;     // #141414 canvas bg (slight lift)
extern const u32 THEME_DIVIDER;       // #2a2a2a hairline

// Accents
extern const u32 THEME_GOLD;          // #f5d76e primary accent
extern const u32 THEME_GOLD_DIM;      // #a88e42 dim accent
extern const u32 THEME_LIFT_GLOW;     // #f5d76e @40% alpha

// Text
extern const u32 THEME_TEXT_PRIMARY;  // #f5f5f5
extern const u32 THEME_TEXT_DIMMED;   // #888888

// Status colors (match the desktop agent status palette)
extern const u32 THEME_STATUS_WORKING;      // #f5d76e gold
extern const u32 THEME_STATUS_ACTIVE;       // #6ed76e green
extern const u32 THEME_STATUS_IDLE;         // #888888 grey
extern const u32 THEME_STATUS_DISCONNECTED; // #d76e6e red

// Pick a status color by string name. Defaults to idle.
u32 theme_status_color(const char *status);

// Font scale factors (citro2d scales the built-in system font)
#define THEME_FONT_HEADER   0.75f
#define THEME_FONT_CARD     0.5f
#define THEME_FONT_LABEL    0.45f
#define THEME_FONT_FOOTER   0.4f

#endif
```

- [ ] **Step 2: Create theme.c**

```c
#include "theme.h"
#include <string.h>

// C2D_Color32 packs RGBA as 0xAABBGGRR — the byte order looks swapped
// but that's citro2d's native layout. Using C2D_Color32(r,g,b,a) is
// clearest. We preconvert here at static init so render code is cheap.

const u32 THEME_BG_DARK       = 0xff0d0d0d;
const u32 THEME_BG_CANVAS     = 0xff141414;
const u32 THEME_DIVIDER       = 0xff2a2a2a;
const u32 THEME_GOLD          = 0xff6ed7f5;  // 0xAABBGGRR of #f5d76e
const u32 THEME_GOLD_DIM      = 0xff428ea8;  // #a88e42
const u32 THEME_LIFT_GLOW     = 0x666ed7f5;  // gold @ 40% alpha
const u32 THEME_TEXT_PRIMARY  = 0xfff5f5f5;
const u32 THEME_TEXT_DIMMED   = 0xff888888;
const u32 THEME_STATUS_WORKING      = 0xff6ed7f5;
const u32 THEME_STATUS_ACTIVE       = 0xff6ed76e;
const u32 THEME_STATUS_IDLE         = 0xff888888;
const u32 THEME_STATUS_DISCONNECTED = 0xff6e6ed7;

u32 theme_status_color(const char *status) {
    if (!status) return THEME_STATUS_IDLE;
    if (strcmp(status, "working") == 0) return THEME_STATUS_WORKING;
    if (strcmp(status, "active") == 0)  return THEME_STATUS_ACTIVE;
    if (strcmp(status, "disconnected") == 0) return THEME_STATUS_DISCONNECTED;
    return THEME_STATUS_IDLE;
}
```

- [ ] **Step 3: Build**

```bash
cd F:/coding/cog-3ds
bash build.sh
```

Expected: clean build, no warnings.

- [ ] **Step 4: Commit**

```bash
git add source/theme.c source/theme.h
git commit -m "feat(phase-2a): theme module with Sunshine palette"
```

---

## Task 3: Render helpers

Wraps citro2d frame lifecycle and exposes text-drawing helpers so other modules don't duplicate font-staging boilerplate.

**Files:**
- Create: `F:\coding\cog-3ds\source\render.h`
- Create: `F:\coding\cog-3ds\source\render.c`
- Modify: `F:\coding\cog-3ds\source\main.c`

- [ ] **Step 1: Create render.h**

```c
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
```

- [ ] **Step 2: Create render.c**

```c
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
```

- [ ] **Step 3: Replace inline citro2d setup in main.c with cog_render**

Open `F:\coding\cog-3ds\source\main.c`. Replace the `#include <citro2d.h>` added in Task 1 with:

```c
#include "render.h"
#include "theme.h"
```

In `main()`, replace the Task 1 citro2d init block with:

```c
    CogRender render = {0};
    bool use_citro2d = cog_render_init(&render);

    PrintConsole top, bottom;
    if (!use_citro2d) {
        consoleInit(GFX_TOP, &top);
        consoleInit(GFX_BOTTOM, &bottom);
    }
```

In the main loop, replace the Task 1 frame block with:

```c
        if (use_citro2d) {
            cog_render_frame_begin(&render);
            cog_render_target_top(&render, THEME_BG_DARK);
            cog_render_text(&render, "The Cog", 10, 10, THEME_FONT_HEADER, THEME_GOLD);
            cog_render_target_bottom(&render, THEME_BG_CANVAS);
            cog_render_text(&render, "canvas", 10, 10, THEME_FONT_FOOTER, THEME_TEXT_DIMMED);
            cog_render_frame_end(&render);
            dirty = false;
        } else if (dirty) {
```

At the end of main, replace the Task 1 cleanup with:

```c
    if (use_citro2d) cog_render_exit(&render);
```

- [ ] **Step 4: Build and deploy**

```bash
bash build.sh && cp cog-3ds.3dsx H:/3ds/cog-3ds/cog-3ds.3dsx
```

- [ ] **Step 5: Visual verification**

Launch on hardware. Top screen shows "The Cog" in gold, top-left. Bottom shows dim "canvas" label, top-left. Background is dark. No crash.

- [ ] **Step 6: Commit**

```bash
git add source/render.c source/render.h source/main.c
git commit -m "feat(phase-2a): render module with citro2d frame + text helpers"
```

---

## Task 4: Card component (draw only)

Defines the `Card` type and how to draw one. No hit-testing or animation yet.

**Files:**
- Create: `F:\coding\cog-3ds\source\card.h`
- Create: `F:\coding\cog-3ds\source\card.c`

- [ ] **Step 1: Create card.h**

```c
// A single agent card for the workshop canvas.
//
// The card holds both persistent agent data (name, status, etc) and
// transient view state (lift scale, fade alpha for enter/exit anims).
// The agent-data fields are synced from /state each poll; the view
// state lives only on the 3DS.

#ifndef COG_CARD_H
#define COG_CARD_H

#include "render.h"
#include <stdbool.h>

typedef struct {
    // From /state
    char id[64];
    char name[64];
    char cli[24];
    char model[32];
    char role[24];
    char status[16];
    float x, y;
    float width, height;
    u32 color;   // group color, RGBA

    // View state
    float lift_scale;   // 1.0 at rest, 1.2 when lifted
    float enter_alpha;  // 0..1, fades in when card first appears
    bool selected;
    bool lifted;
} Card;

// Default card size when /state doesn't provide one.
#define CARD_DEFAULT_WIDTH   60.0f
#define CARD_DEFAULT_HEIGHT  40.0f

// Draw a card at its current (x, y) in world coords, transformed by
// camera pan/zoom applied by the caller (pass the already-projected
// screen coords via sx/sy and scale combined into world_to_screen_scale).
// Selection ring and lift glow are drawn by the caller via helpers below.
void card_draw(CogRender *r, const Card *c, float sx, float sy,
               float world_to_screen_scale);

// Selection ring (gold 2px border around the card at its projected rect).
void card_draw_selection_ring(float sx, float sy, float sw, float sh);

// Lift glow under a lifted card — soft gold glow, cheap 3-layer fake blur.
void card_draw_lift_glow(float sx, float sy, float sw, float sh);

#endif
```

- [ ] **Step 2: Create card.c**

```c
#include "card.h"
#include "theme.h"
#include <string.h>
#include <stdio.h>

static void truncate_copy(char *dest, size_t dest_size, const char *src, int max_chars) {
    int copy = max_chars < (int)(dest_size - 1) ? max_chars : (int)(dest_size - 1);
    int n = 0;
    while (src && src[n] && n < copy) { dest[n] = src[n]; n++; }
    dest[n] = '\0';
    if (src && src[n]) {
        // Replace last char with ellipsis marker "."
        if (n >= 1) dest[n - 1] = '.';
    }
}

void card_draw(CogRender *r, const Card *c, float sx, float sy,
               float world_to_screen_scale) {
    float sw = c->width * world_to_screen_scale * c->lift_scale;
    float sh = c->height * world_to_screen_scale * c->lift_scale;

    // Lift glow (if lifted)
    if (c->lift_scale > 1.0f) {
        card_draw_lift_glow(sx, sy, sw, sh);
    }

    // Card background — tinted by group color but with fixed dark alpha
    // so text stays legible even with bright group colors.
    u32 bg = (c->color & 0x00ffffff) | 0xcc000000;  // 80% alpha
    cog_render_rounded_rect(sx, sy, sw, sh, 4.0f, bg);

    // Status dot (top-left)
    u32 dot_color = theme_status_color(c->status);
    C2D_DrawCircleSolid(sx + 6, sy + 6, 0.5f, 3.0f, dot_color);

    // Name (truncated to 12 chars, placed to the right of the dot)
    char name_trunc[16];
    truncate_copy(name_trunc, sizeof(name_trunc), c->name, 12);
    cog_render_text(r, name_trunc, sx + 14, sy + 2, THEME_FONT_CARD, THEME_TEXT_PRIMARY);

    // CLI badge + role on second line (truncated)
    char line2[32];
    const char *cli_short = c->cli[0] ? c->cli : "?";
    const char *role = c->role[0] ? c->role : "";
    snprintf(line2, sizeof(line2), "%.3s %s%.*s", cli_short, role[0] ? "· " : "",
             role[0] ? (int)sizeof(line2) - 8 : 0, role);
    cog_render_text(r, line2, sx + 4, sy + 20, THEME_FONT_FOOTER, THEME_TEXT_DIMMED);

    // Selection ring overlay
    if (c->selected) {
        card_draw_selection_ring(sx, sy, sw, sh);
    }
}

void card_draw_selection_ring(float sx, float sy, float sw, float sh) {
    cog_render_rect_outline(sx - 1, sy - 1, sw + 2, sh + 2, 2.0f, THEME_GOLD);
}

void card_draw_lift_glow(float sx, float sy, float sw, float sh) {
    // Three concentric faded rects to fake a soft glow — cheap + readable.
    u32 g1 = (THEME_LIFT_GLOW & 0x00ffffff) | 0x66000000;
    u32 g2 = (THEME_LIFT_GLOW & 0x00ffffff) | 0x33000000;
    u32 g3 = (THEME_LIFT_GLOW & 0x00ffffff) | 0x15000000;
    cog_render_rounded_rect(sx - 2, sy - 2, sw + 4, sh + 4, 6.0f, g1);
    cog_render_rounded_rect(sx - 4, sy - 4, sw + 8, sh + 8, 8.0f, g2);
    cog_render_rounded_rect(sx - 6, sy - 6, sw + 12, sh + 12, 10.0f, g3);
}
```

- [ ] **Step 3: Build**

```bash
bash build.sh
```

Expected: clean build (main.c doesn't call card_draw yet, but file compiles).

- [ ] **Step 4: Commit**

```bash
git add source/card.c source/card.h
git commit -m "feat(phase-2a): Card type and draw helpers"
```

---

## Task 5: Canvas with pan + zoom + draw all cards

Manages the camera (pan offset + zoom) and draws all cards for the bottom screen.

**Files:**
- Create: `F:\coding\cog-3ds\source\canvas.h`
- Create: `F:\coding\cog-3ds\source\canvas.c`

- [ ] **Step 1: Create canvas.h**

```c
// Workshop canvas — bottom-screen view. Maintains camera state
// (pan + zoom), draws cards through the camera transform, and
// hit-tests screen taps back to card indices.
//
// World coords: desktop-native pixels. 1 world unit = 1 desktop pixel.
// Screen coords: 320x240 3DS bottom screen.

#ifndef COG_CANVAS_H
#define COG_CANVAS_H

#include "card.h"

#define CANVAS_MAX_CARDS 32
#define CANVAS_SCREEN_W  320
#define CANVAS_SCREEN_H  240
#define CANVAS_ZOOM_MIN  0.3f
#define CANVAS_ZOOM_MAX  2.0f
#define CANVAS_ZOOM_STEP 0.15f

typedef struct {
    Card cards[CANVAS_MAX_CARDS];
    int card_count;
    float cam_x, cam_y;
    float cam_zoom;
    int selected_idx;  // -1 = none
    int lifted_idx;    // -1 = not dragging
} Canvas;

void canvas_init(Canvas *cv);

// Apply camera transform: world -> screen.
void canvas_world_to_screen(const Canvas *cv, float wx, float wy,
                            float *sx, float *sy);
void canvas_screen_to_world(const Canvas *cv, float sx, float sy,
                            float *wx, float *wy);

// Hit-test a screen-coord touch against all cards. Returns index of
// topmost card under the point, or -1 if none. Topmost = later in
// array (drawn later = in front).
int canvas_hit_test(const Canvas *cv, float sx, float sy);

// Pan the camera (screen-space delta — divides by zoom internally).
void canvas_pan(Canvas *cv, float dsx, float dsy);

// Zoom toward the center of the screen.
void canvas_zoom(Canvas *cv, float delta);

// Draw the whole canvas (background + all cards in render order).
void canvas_draw(CogRender *r, const Canvas *cv);

// Frame-the-cards helper: center camera on bounding box of all
// cards and pick zoom that fits them on screen. Call after first
// /state poll populates cards.
void canvas_frame_all(Canvas *cv);

#endif
```

- [ ] **Step 2: Create canvas.c**

```c
#include "canvas.h"
#include "theme.h"
#include <string.h>
#include <math.h>

void canvas_init(Canvas *cv) {
    memset(cv, 0, sizeof(*cv));
    cv->cam_zoom = 1.0f;
    cv->selected_idx = -1;
    cv->lifted_idx = -1;
}

void canvas_world_to_screen(const Canvas *cv, float wx, float wy,
                            float *sx, float *sy) {
    *sx = (wx - cv->cam_x) * cv->cam_zoom + CANVAS_SCREEN_W / 2.0f;
    *sy = (wy - cv->cam_y) * cv->cam_zoom + CANVAS_SCREEN_H / 2.0f;
}

void canvas_screen_to_world(const Canvas *cv, float sx, float sy,
                            float *wx, float *wy) {
    *wx = (sx - CANVAS_SCREEN_W / 2.0f) / cv->cam_zoom + cv->cam_x;
    *wy = (sy - CANVAS_SCREEN_H / 2.0f) / cv->cam_zoom + cv->cam_y;
}

int canvas_hit_test(const Canvas *cv, float sx, float sy) {
    // Back-to-front so topmost card wins
    for (int i = cv->card_count - 1; i >= 0; i--) {
        const Card *c = &cv->cards[i];
        float csx, csy;
        canvas_world_to_screen(cv, c->x, c->y, &csx, &csy);
        float csw = c->width * cv->cam_zoom * c->lift_scale;
        float csh = c->height * cv->cam_zoom * c->lift_scale;
        if (sx >= csx && sx <= csx + csw && sy >= csy && sy <= csy + csh) {
            return i;
        }
    }
    return -1;
}

void canvas_pan(Canvas *cv, float dsx, float dsy) {
    cv->cam_x -= dsx / cv->cam_zoom;
    cv->cam_y -= dsy / cv->cam_zoom;
}

void canvas_zoom(Canvas *cv, float delta) {
    cv->cam_zoom += delta;
    if (cv->cam_zoom < CANVAS_ZOOM_MIN) cv->cam_zoom = CANVAS_ZOOM_MIN;
    if (cv->cam_zoom > CANVAS_ZOOM_MAX) cv->cam_zoom = CANVAS_ZOOM_MAX;
}

void canvas_draw(CogRender *r, const Canvas *cv) {
    // Background already cleared by caller to THEME_BG_CANVAS.
    // Draw cards in order so higher-indexed cards end up on top.
    int lifted = cv->lifted_idx;
    for (int i = 0; i < cv->card_count; i++) {
        if (i == lifted) continue;  // draw lifted last
        const Card *c = &cv->cards[i];
        float sx, sy;
        canvas_world_to_screen(cv, c->x, c->y, &sx, &sy);
        card_draw(r, c, sx, sy, cv->cam_zoom);
    }
    if (lifted >= 0 && lifted < cv->card_count) {
        const Card *c = &cv->cards[lifted];
        float sx, sy;
        canvas_world_to_screen(cv, c->x, c->y, &sx, &sy);
        card_draw(r, c, sx, sy, cv->cam_zoom);
    }
}

void canvas_frame_all(Canvas *cv) {
    if (cv->card_count == 0) {
        cv->cam_x = 0; cv->cam_y = 0; cv->cam_zoom = 1.0f;
        return;
    }
    float minx = cv->cards[0].x, maxx = cv->cards[0].x + cv->cards[0].width;
    float miny = cv->cards[0].y, maxy = cv->cards[0].y + cv->cards[0].height;
    for (int i = 1; i < cv->card_count; i++) {
        const Card *c = &cv->cards[i];
        if (c->x < minx) minx = c->x;
        if (c->y < miny) miny = c->y;
        if (c->x + c->width > maxx) maxx = c->x + c->width;
        if (c->y + c->height > maxy) maxy = c->y + c->height;
    }
    float w = maxx - minx, h = maxy - miny;
    cv->cam_x = minx + w / 2;
    cv->cam_y = miny + h / 2;
    // Fit with 10% padding
    float zx = (CANVAS_SCREEN_W * 0.9f) / (w > 1 ? w : 1);
    float zy = (CANVAS_SCREEN_H * 0.9f) / (h > 1 ? h : 1);
    cv->cam_zoom = zx < zy ? zx : zy;
    if (cv->cam_zoom < CANVAS_ZOOM_MIN) cv->cam_zoom = CANVAS_ZOOM_MIN;
    if (cv->cam_zoom > CANVAS_ZOOM_MAX) cv->cam_zoom = CANVAS_ZOOM_MAX;
}
```

- [ ] **Step 3: Build**

```bash
bash build.sh
```

Expected: clean build (canvas not yet wired into main.c).

- [ ] **Step 4: Commit**

```bash
git add source/canvas.c source/canvas.h
git commit -m "feat(phase-2b): Canvas with pan, zoom, hit-test, frame-all"
```

---

## Task 6: Detail view (top screen)

Draws the selected agent's info on the top screen.

**Files:**
- Create: `F:\coding\cog-3ds\source\detail.h`
- Create: `F:\coding\cog-3ds\source\detail.c`

- [ ] **Step 1: Create detail.h**

```c
// Top-screen detail view — header bar + selected agent info.
// Called once per frame after the render target switches to top.

#ifndef COG_DETAIL_H
#define COG_DETAIL_H

#include "render.h"
#include "canvas.h"

// Draw header + footer + body. card_or_null is the selected card
// or NULL for the empty state. project_name shows in the header.
void detail_draw(CogRender *r, const char *project_name,
                 const Card *card_or_null, int agent_count,
                 int connection_count);

#endif
```

- [ ] **Step 2: Create detail.c**

```c
#include "detail.h"
#include "theme.h"
#include <stdio.h>

#define TOP_W 400
#define TOP_H 240
#define HEADER_H 28
#define FOOTER_H 20

static void draw_header(CogRender *r, const char *project_name,
                        int agent_count, int connection_count) {
    // Header bar background
    cog_render_rect(0, 0, TOP_W, HEADER_H, THEME_BG_CANVAS);
    cog_render_rect(0, HEADER_H - 1, TOP_W, 1, THEME_DIVIDER);
    // Title on the left
    cog_render_text(r, "The Cog", 12, 4, THEME_FONT_HEADER, THEME_GOLD);
    // Project name + meta on the right
    char meta[64];
    snprintf(meta, sizeof(meta), "%s  %d agents  %d conn",
             project_name && *project_name ? project_name : "(no project)",
             agent_count, connection_count);
    cog_render_text_right(r, meta, TOP_W - 12, 10, THEME_FONT_FOOTER, THEME_TEXT_DIMMED);
}

static void draw_footer(CogRender *r) {
    float fy = TOP_H - FOOTER_H;
    cog_render_rect(0, fy, TOP_W, 1, THEME_DIVIDER);
    cog_render_text(r, "[A] refresh  [X] rescan  [L/R] zoom  [B] deselect",
                    12, fy + 4, THEME_FONT_FOOTER, THEME_TEXT_DIMMED);
}

static void draw_empty_body(CogRender *r) {
    cog_render_text(r, "Tap a card to see details.",
                    12, HEADER_H + 20, THEME_FONT_CARD, THEME_TEXT_DIMMED);
    cog_render_text(r, "D-pad cycles through agents.",
                    12, HEADER_H + 40, THEME_FONT_FOOTER, THEME_TEXT_DIMMED);
}

static void draw_label_value(CogRender *r, const char *label, const char *value,
                             float y) {
    cog_render_text(r, label, 12, y, THEME_FONT_LABEL, THEME_TEXT_DIMMED);
    cog_render_text(r, value[0] ? value : "—", 90, y, THEME_FONT_LABEL, THEME_TEXT_PRIMARY);
}

static void draw_body(CogRender *r, const Card *c) {
    float y = HEADER_H + 8;
    // Status dot + name as a title
    u32 dot = theme_status_color(c->status);
    C2D_DrawCircleSolid(20, y + 10, 0.5f, 6.0f, dot);
    cog_render_text(r, c->name, 34, y, THEME_FONT_HEADER, THEME_TEXT_PRIMARY);
    y += 34;
    draw_label_value(r, "CLI",    c->cli,    y); y += 22;
    draw_label_value(r, "Model",  c->model,  y); y += 22;
    draw_label_value(r, "Role",   c->role,   y); y += 22;
    draw_label_value(r, "Status", c->status, y); y += 22;
    // Divider
    cog_render_rect(12, y + 4, TOP_W - 24, 1, THEME_DIVIDER);
    y += 16;
    cog_render_text(r, "(Phase 3 will show live output here.)",
                    12, y, THEME_FONT_FOOTER, THEME_TEXT_DIMMED);
}

void detail_draw(CogRender *r, const char *project_name,
                 const Card *card_or_null, int agent_count,
                 int connection_count) {
    draw_header(r, project_name, agent_count, connection_count);
    if (card_or_null) {
        draw_body(r, card_or_null);
    } else {
        draw_empty_body(r);
    }
    draw_footer(r);
}
```

- [ ] **Step 3: Build**

```bash
bash build.sh
```

Expected: clean build.

- [ ] **Step 4: Commit**

```bash
git add source/detail.c source/detail.h
git commit -m "feat(phase-2a): Detail view for top screen"
```

---

## Task 7: Wire canvas + detail into main.c

Replaces the Phase 1 PrintConsole render path with canvas + detail. State polling still populates the agent list; we translate to `Card` entries and draw them.

**Files:**
- Modify: `F:\coding\cog-3ds\source\main.c`

- [ ] **Step 1: Add Canvas ownership + helpers**

In `main.c`, add after the existing includes:

```c
#include "canvas.h"
#include "card.h"
#include "detail.h"
```

Add a static helper near the top of `main.c` (after the `build_state_url` function):

```c
// Parse a hex color string "#RRGGBB" into citro2d's 0xAABBGGRR layout.
// Returns a grey fallback for invalid input.
static u32 parse_hex_color(const char *hex) {
    if (!hex || hex[0] != '#' || !hex[1]) return 0xff888888;
    unsigned int r = 0, g = 0, b = 0;
    if (sscanf(hex + 1, "%2x%2x%2x", &r, &g, &b) != 3) return 0xff888888;
    return 0xff000000 | (b << 16) | (g << 8) | r;
}

// Sync the canvas's cards from the freshly-polled CogState. Preserves
// selection/lift for any card whose id is still present.
static void sync_canvas_from_state(Canvas *cv, const CogState *state) {
    // Stash currently-selected/lifted ids so we can restore
    char prev_selected[64] = "";
    char prev_lifted[64] = "";
    if (cv->selected_idx >= 0 && cv->selected_idx < cv->card_count)
        strncpy(prev_selected, cv->cards[cv->selected_idx].id, sizeof(prev_selected) - 1);
    if (cv->lifted_idx >= 0 && cv->lifted_idx < cv->card_count)
        strncpy(prev_lifted, cv->cards[cv->lifted_idx].id, sizeof(prev_lifted) - 1);

    cv->card_count = state->agent_count < CANVAS_MAX_CARDS ? state->agent_count : CANVAS_MAX_CARDS;
    cv->selected_idx = -1;
    cv->lifted_idx = -1;

    for (int i = 0; i < cv->card_count; i++) {
        Card *c = &cv->cards[i];
        const AgentInfo *a = &state->agents[i];
        strncpy(c->id, a->id, sizeof(c->id) - 1); c->id[sizeof(c->id) - 1] = '\0';
        strncpy(c->name, a->name, sizeof(c->name) - 1); c->name[sizeof(c->name) - 1] = '\0';
        strncpy(c->cli, a->cli, sizeof(c->cli) - 1); c->cli[sizeof(c->cli) - 1] = '\0';
        strncpy(c->model, a->model, sizeof(c->model) - 1); c->model[sizeof(c->model) - 1] = '\0';
        strncpy(c->role, a->role, sizeof(c->role) - 1); c->role[sizeof(c->role) - 1] = '\0';
        strncpy(c->status, a->status, sizeof(c->status) - 1); c->status[sizeof(c->status) - 1] = '\0';

        // Fields from the /state layout enhancement (Task 0). Default if missing.
        c->x = a->x; c->y = a->y;
        c->width = a->width > 0 ? a->width : CARD_DEFAULT_WIDTH;
        c->height = a->height > 0 ? a->height : CARD_DEFAULT_HEIGHT;
        c->color = a->color_rgba;
        c->lift_scale = 1.0f;
        c->enter_alpha = 1.0f;
        c->selected = false;
        c->lifted = false;

        if (prev_selected[0] && strcmp(prev_selected, c->id) == 0) {
            cv->selected_idx = i;
            c->selected = true;
        }
        if (prev_lifted[0] && strcmp(prev_lifted, c->id) == 0) {
            cv->lifted_idx = i;
            c->lifted = true;
            c->lift_scale = 1.2f;
        }
    }
}
```

- [ ] **Step 2: Extend AgentInfo for new fields**

In `main.c`, find the `typedef struct { ... } AgentInfo;` (around line 30). Add these fields:

```c
    float x, y;
    float width, height;
    u32 color_rgba;   // citro2d native
```

In `parse_state()` (the cJSON parsing function), add after the existing `json_get_string` calls:

```c
            cJSON *x = cJSON_GetObjectItemCaseSensitive(agent, "x");
            cJSON *y = cJSON_GetObjectItemCaseSensitive(agent, "y");
            cJSON *w = cJSON_GetObjectItemCaseSensitive(agent, "width");
            cJSON *h = cJSON_GetObjectItemCaseSensitive(agent, "height");
            cJSON *col = cJSON_GetObjectItemCaseSensitive(agent, "color");
            a->x = cJSON_IsNumber(x) ? (float)x->valuedouble : 0.0f;
            a->y = cJSON_IsNumber(y) ? (float)y->valuedouble : 0.0f;
            a->width = cJSON_IsNumber(w) ? (float)w->valuedouble : 0.0f;
            a->height = cJSON_IsNumber(h) ? (float)h->valuedouble : 0.0f;
            a->color_rgba = parse_hex_color(cJSON_IsString(col) ? col->valuestring : NULL);
```

- [ ] **Step 3: Replace the citro2d render block**

In `main.c`, replace the Task 3 temporary "The Cog" / "canvas" render block with:

```c
        if (use_citro2d) {
            // On the first successful state with cards, frame them.
            static bool framed = false;
            if (!framed && canvas.card_count > 0) {
                canvas_frame_all(&canvas);
                framed = true;
            }

            const Card *selected = (canvas.selected_idx >= 0) ? &canvas.cards[canvas.selected_idx] : NULL;

            cog_render_frame_begin(&render);
            cog_render_target_top(&render, THEME_BG_DARK);
            detail_draw(&render, state.project_name, selected,
                        state.agent_count, state.connection_count);
            cog_render_target_bottom(&render, THEME_BG_CANVAS);
            canvas_draw(&render, &canvas);
            cog_render_frame_end(&render);
            dirty = false;
        } else if (dirty) {
```

- [ ] **Step 4: Declare and initialize Canvas in main()**

In `main()`, after `CogState state = {0};`, add:

```c
    Canvas canvas;
    canvas_init(&canvas);
```

After a successful `parse_state`, add `sync_canvas_from_state(&canvas, &state);`. Find this block:

```c
            if (code == 200 && body) {
                if (parse_state(body, &state)) {
```

Insert after `if (parse_state(body, &state)) {`:

```c
                    sync_canvas_from_state(&canvas, &state);
```

- [ ] **Step 5: Wire L/R zoom**

In the main-loop key handling (near existing `if (down & KEY_A)`), add:

```c
        if (down & KEY_L) canvas_zoom(&canvas, -CANVAS_ZOOM_STEP);
        if (down & KEY_R) canvas_zoom(&canvas, +CANVAS_ZOOM_STEP);
```

- [ ] **Step 6: Build and deploy**

```bash
bash build.sh && cp cog-3ds.3dsx H:/3ds/cog-3ds/cog-3ds.3dsx
```

- [ ] **Step 7: Visual verification**

Launch on hardware with the updated desktop running (Task 0 must be live). Expected:
- Top screen: gold "The Cog" header, project name + agent count right-aligned, "Tap a card to see details" placeholder body
- Bottom screen: canvas with cards at their desktop positions, framed to fit
- Press L/R: canvas zooms out / in (visible card size change)
- No crash; D-pad still nav-selects in Phase 1 sense (fallback not yet wired in Phase 2)

- [ ] **Step 8: Commit**

```bash
git add source/main.c
git commit -m "feat(phase-2ab): wire canvas + detail into main render loop"
```

---

## Task 8: Touch selection

Tapping a card on the bottom screen selects it and populates the detail view. Tapping empty canvas starts a pan. Releasing without a card under hit-test leaves selection unchanged.

**Files:**
- Modify: `F:\coding\cog-3ds\source\main.c`

- [ ] **Step 1: Track touch state across frames**

In `main()`, add near the other loop locals:

```c
    bool touching = false;
    touchPosition prev_touch = {0};
    bool did_pan = false;
```

- [ ] **Step 2: Replace the existing touch-handling block with stateful version**

In the main loop, add after the existing `hidScanInput()` line:

```c
        u32 held = hidKeysHeld();
        touchPosition touch;
        if (held & KEY_TOUCH) {
            hidTouchRead(&touch);
            if (!touching) {
                // Touch down — decide: hit a card, or start pan
                int hit = canvas_hit_test(&canvas, touch.px, touch.py);
                if (hit >= 0) {
                    // Select the card (deselect previous)
                    if (canvas.selected_idx >= 0 && canvas.selected_idx < canvas.card_count)
                        canvas.cards[canvas.selected_idx].selected = false;
                    canvas.selected_idx = hit;
                    canvas.cards[hit].selected = true;
                    did_pan = false;
                } else {
                    did_pan = false;  // will start panning on drag
                }
                touching = true;
            } else {
                // Touch held — drag (pan if not on a card)
                int dx = touch.px - prev_touch.px;
                int dy = touch.py - prev_touch.py;
                if (canvas.selected_idx < 0 || did_pan ||
                    canvas_hit_test(&canvas, prev_touch.px, prev_touch.py) < 0) {
                    if (dx != 0 || dy != 0) {
                        canvas_pan(&canvas, (float)dx, (float)dy);
                        did_pan = true;
                    }
                }
            }
            prev_touch = touch;
        } else if (touching) {
            // Touch release
            touching = false;
            did_pan = false;
        }
```

- [ ] **Step 3: Add B to deselect**

Near other key handling:

```c
        if (down & KEY_B) {
            if (canvas.selected_idx >= 0 && canvas.selected_idx < canvas.card_count)
                canvas.cards[canvas.selected_idx].selected = false;
            canvas.selected_idx = -1;
        }
```

- [ ] **Step 4: Build and deploy**

```bash
bash build.sh && cp cog-3ds.3dsx H:/3ds/cog-3ds/cog-3ds.3dsx
```

- [ ] **Step 5: Visual verification**

- Tap a card on bottom → gold ring appears around it, top screen fills with its details.
- Tap another card → selection moves.
- Press B → selection clears, top shows empty hint.
- Tap empty canvas and drag → canvas pans (cards shift together).

- [ ] **Step 6: Commit**

```bash
git add source/main.c
git commit -m "feat(phase-2b): touch-to-select + drag-to-pan"
```

---

## Task 9: Long-press lift animation

Tap-and-hold 500ms on the selected card "lifts" it — animates scale 1.0→1.2 with lift glow.

**Files:**
- Modify: `F:\coding\cog-3ds\source\main.c`

- [ ] **Step 1: Track long-press timer + easing helper**

Add at the top of `main.c` (outside main):

```c
// Simple ease-out cubic: t in [0,1] -> smoothed out
static float ease_out_cubic(float t) {
    if (t <= 0) return 0; if (t >= 1) return 1;
    float x = 1 - t;
    return 1 - x * x * x;
}
```

In `main()`, with the other loop locals:

```c
    u64 touch_start_ms = 0;
    u64 lift_start_ms = 0;
    const u64 LIFT_THRESHOLD_MS = 500;
    const u64 LIFT_ANIM_MS = 150;
```

- [ ] **Step 2: Detect long-press, start the lift**

In the touch-handling block, replace the "Touch down" section with:

```c
            if (!touching) {
                int hit = canvas_hit_test(&canvas, touch.px, touch.py);
                if (hit >= 0) {
                    if (canvas.selected_idx >= 0 && canvas.selected_idx < canvas.card_count)
                        canvas.cards[canvas.selected_idx].selected = false;
                    canvas.selected_idx = hit;
                    canvas.cards[hit].selected = true;
                    touch_start_ms = osGetTime();
                    did_pan = false;
                } else {
                    did_pan = false;
                }
                touching = true;
            }
```

In the same block, replace the "Touch held" section with:

```c
            else {
                int dx = touch.px - prev_touch.px;
                int dy = touch.py - prev_touch.py;

                // Check for long-press lift trigger
                if (canvas.lifted_idx < 0 && canvas.selected_idx >= 0 && !did_pan) {
                    u64 now = osGetTime();
                    if (now - touch_start_ms >= LIFT_THRESHOLD_MS) {
                        canvas.lifted_idx = canvas.selected_idx;
                        canvas.cards[canvas.lifted_idx].lifted = true;
                        lift_start_ms = now;
                    }
                }

                // Drive lift animation
                if (canvas.lifted_idx >= 0) {
                    u64 now = osGetTime();
                    float t = (now - lift_start_ms) / (float)LIFT_ANIM_MS;
                    if (t > 1.0f) t = 1.0f;
                    canvas.cards[canvas.lifted_idx].lift_scale = 1.0f + 0.2f * ease_out_cubic(t);
                }

                // Panning only if not lifted and the initial touch wasn't on a card
                if (canvas.lifted_idx < 0 &&
                    (did_pan || canvas_hit_test(&canvas, prev_touch.px, prev_touch.py) < 0)) {
                    if (dx != 0 || dy != 0) {
                        canvas_pan(&canvas, (float)dx, (float)dy);
                        did_pan = true;
                    }
                }
            }
```

In the touch-release block:

```c
        } else if (touching) {
            // Touch release — drop lifted card back to 1.0 scale
            if (canvas.lifted_idx >= 0 && canvas.lifted_idx < canvas.card_count) {
                canvas.cards[canvas.lifted_idx].lifted = false;
                canvas.cards[canvas.lifted_idx].lift_scale = 1.0f;
                canvas.lifted_idx = -1;
            }
            touching = false;
            did_pan = false;
        }
```

- [ ] **Step 3: Build and deploy**

```bash
bash build.sh && cp cog-3ds.3dsx H:/3ds/cog-3ds/cog-3ds.3dsx
```

- [ ] **Step 4: Visual verification**

- Tap a card (quick release): selects it, no lift animation
- Tap a card and hold ≥500ms: card grows to 1.2× and gold glow appears underneath
- Release while lifted: card snaps back to 1.0× (Task 10 will also fire the position POST)

- [ ] **Step 5: Commit**

```bash
git add source/main.c
git commit -m "feat(phase-2b): long-press lift animation with scale + glow"
```

---

## Task 10: Drag lifted card + POST position on release

**Files:**
- Modify: `F:\coding\cog-3ds\source\http.h` (no change if cog_http_post_json already exists; verify)
- Modify: `F:\coding\cog-3ds\source\main.c`

- [ ] **Step 1: Confirm http.h has POST**

Read `F:\coding\cog-3ds\source\http.h`. Expected: `cog_http_post_json(const char *url, const char *body_json, char **out_body, size_t *out_len)` already exists from Phase 1. No changes needed.

- [ ] **Step 2: Move the lifted card during drag**

In the touch-held block (Task 9's "Drive lift animation" section), add AFTER the lift-animation update:

```c
                // Drag-move the lifted card
                if (canvas.lifted_idx >= 0 && (dx != 0 || dy != 0)) {
                    Card *lc = &canvas.cards[canvas.lifted_idx];
                    // Screen delta -> world delta via zoom
                    lc->x += (float)dx / canvas.cam_zoom;
                    lc->y += (float)dy / canvas.cam_zoom;
                }
```

- [ ] **Step 3: Build the POST URL + body helper**

Add a static helper near `build_state_url`:

```c
static void build_window_url(const char *base_url, const char *agent_id,
                             char *out, size_t out_size) {
    snprintf(out, out_size, "%sworkshop/window/%s", base_url, agent_id);
}

// POST new position for the given card. Fire-and-forget; we don't
// block the render loop on network.
static void post_card_position(const char *base_url, const Card *c) {
    char url[COG_URL_MAX + 96];
    build_window_url(base_url, c->id, url, sizeof(url));
    char body[96];
    snprintf(body, sizeof(body), "{\"x\":%.0f,\"y\":%.0f}", c->x, c->y);
    char *resp = NULL;
    size_t resp_len = 0;
    cog_http_post_json(url, body, &resp, &resp_len);
    if (resp) free(resp);
}
```

- [ ] **Step 4: Fire POST on release**

In the touch-release block, update to:

```c
        } else if (touching) {
            if (canvas.lifted_idx >= 0 && canvas.lifted_idx < canvas.card_count) {
                Card *lc = &canvas.cards[canvas.lifted_idx];
                lc->lifted = false;
                lc->lift_scale = 1.0f;
                post_card_position(url, lc);
                canvas.lifted_idx = -1;
            }
            touching = false;
            did_pan = false;
        }
```

- [ ] **Step 5: Build and deploy**

```bash
bash build.sh && cp cog-3ds.3dsx H:/3ds/cog-3ds/cog-3ds.3dsx
```

- [ ] **Step 6: Visual verification with hardware + desktop**

- Start Cog on desktop, note positions of at least 2 agent windows
- On 3DS, launch, wait for first poll
- Long-press a card until it lifts
- Drag it to a new spot, release
- Watch desktop: the corresponding window should jump to the new position within ~1 second

- [ ] **Step 7: Commit**

```bash
git add source/main.c
git commit -m "feat(phase-2b): drag lifted card + POST position on release"
```

---

## Task 11: D-pad nav fallback

Tap-precision is hard on a small 3DS screen with lots of cards. D-pad selects the nearest card in the pressed direction.

**Files:**
- Modify: `F:\coding\cog-3ds\source\canvas.h`
- Modify: `F:\coding\cog-3ds\source\canvas.c`
- Modify: `F:\coding\cog-3ds\source\main.c`

- [ ] **Step 1: Add navigation helper**

In `canvas.h`, add:

```c
// Returns the index of the card nearest to the currently-selected
// card in the given direction (-1 left, +1 right, -1/+1 for up/down on y).
// Returns -1 if no card exists in that direction. If nothing selected,
// returns index 0 (or -1 if empty).
typedef enum { CANVAS_NAV_UP, CANVAS_NAV_DOWN, CANVAS_NAV_LEFT, CANVAS_NAV_RIGHT } CanvasNavDir;
int canvas_nav_nearest(const Canvas *cv, CanvasNavDir dir);
```

- [ ] **Step 2: Implement**

In `canvas.c`:

```c
int canvas_nav_nearest(const Canvas *cv, CanvasNavDir dir) {
    if (cv->card_count == 0) return -1;
    if (cv->selected_idx < 0) return 0;
    const Card *from = &cv->cards[cv->selected_idx];
    float fx = from->x + from->width / 2, fy = from->y + from->height / 2;
    int best = -1;
    float best_dist = 1e30f;
    for (int i = 0; i < cv->card_count; i++) {
        if (i == cv->selected_idx) continue;
        const Card *c = &cv->cards[i];
        float cx = c->x + c->width / 2, cy = c->y + c->height / 2;
        float dx = cx - fx, dy = cy - fy;
        bool in_dir = false;
        switch (dir) {
            case CANVAS_NAV_LEFT:  in_dir = dx < 0 && (-dx) > (dy > 0 ? dy : -dy); break;
            case CANVAS_NAV_RIGHT: in_dir = dx > 0 && dx > (dy > 0 ? dy : -dy);    break;
            case CANVAS_NAV_UP:    in_dir = dy < 0 && (-dy) > (dx > 0 ? dx : -dx); break;
            case CANVAS_NAV_DOWN:  in_dir = dy > 0 && dy > (dx > 0 ? dx : -dx);    break;
        }
        if (!in_dir) continue;
        float dist = dx * dx + dy * dy;
        if (dist < best_dist) { best_dist = dist; best = i; }
    }
    return best;
}
```

- [ ] **Step 3: Wire into main loop**

In `main.c`, replace the old `KEY_DUP` / `KEY_DDOWN` handlers (and add LEFT/RIGHT):

```c
        CanvasNavDir nav = (CanvasNavDir)-1;
        if (down & KEY_DUP)    nav = CANVAS_NAV_UP;
        if (down & KEY_DDOWN)  nav = CANVAS_NAV_DOWN;
        if (down & KEY_DLEFT)  nav = CANVAS_NAV_LEFT;
        if (down & KEY_DRIGHT) nav = CANVAS_NAV_RIGHT;
        if ((int)nav >= 0) {
            int ni = canvas_nav_nearest(&canvas, nav);
            if (ni >= 0) {
                if (canvas.selected_idx >= 0 && canvas.selected_idx < canvas.card_count)
                    canvas.cards[canvas.selected_idx].selected = false;
                canvas.selected_idx = ni;
                canvas.cards[ni].selected = true;
            }
        }
```

- [ ] **Step 4: Build and deploy**

```bash
bash build.sh && cp cog-3ds.3dsx H:/3ds/cog-3ds/cog-3ds.3dsx
```

- [ ] **Step 5: Visual verification**

- With no selection, press any D-pad → selects card index 0.
- With a selected card, D-pad UP/DOWN/LEFT/RIGHT moves selection to the nearest card in that direction.
- If nothing is in the pressed direction, selection stays.

- [ ] **Step 6: Commit**

```bash
git add source/canvas.c source/canvas.h source/main.c
git commit -m "feat(phase-2b): D-pad cycle-select to nearest card"
```

---

## Task 12: QR scanner citro2d reskin

The existing QR scanner uses PrintConsole. With citro2d active we need to briefly switch the top screen back to console mode during scan (or reskin the scan status in citro2d). Simpler: reskin status with citro2d — no camera preview yet, just a dark screen with pulsing "SCANNING…" text.

**Files:**
- Modify: `F:\coding\cog-3ds\source\qr_scan.c`

- [ ] **Step 1: Accept a CogRender pointer parameter**

Open `F:\coding\cog-3ds\source\qr_scan.h`, change the signature:

```c
bool cog_qr_scan(struct CogRender_ *render, char *out_url, size_t out_size);
```

Forward-declare the struct at the top of the header (to avoid including render.h and creating a tangle):

```c
struct CogRender_;
```

Actually, since `CogRender` is `typedef`'d in `render.h`, just include it:

```c
#include "render.h"

bool cog_qr_scan(CogRender *render, char *out_url, size_t out_size);
```

- [ ] **Step 2: Replace PrintConsole status helpers with citro2d draw**

In `qr_scan.c`, replace the existing `draw_top` and `draw_bottom` helpers with:

```c
#include "theme.h"
#include <3ds.h>

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
```

- [ ] **Step 3: Replace draw calls in the scan loop**

In `cog_qr_scan`, replace all `consoleSelect(...) / draw_top(...) / draw_bottom(...)` calls with single calls:

```c
    draw_status_frame(render, "SCANNING...", "scanning...", 0);
```

And inside the loop, `draw_status_frame(render, "SCANNING...", last_error[0] ? last_error : "scanning...", scan_count);`

Delete the two `PrintConsole` declarations and `consoleInit` calls at the top of the function (they're no longer needed).

- [ ] **Step 4: Update callers in main.c**

Search for `cog_qr_scan(` in `main.c` (two call sites — setup screen and X-rescan). Update both:

```c
if (cog_qr_scan(&render, scanned, sizeof(scanned))) {
```

Note: the setup screen path uses PrintConsole. For Phase 2 we'll keep that simple — the setup path only runs when no config exists. Check if `use_citro2d` is true before calling; if not, the old PrintConsole-era `cog_qr_scan` won't work. Simplest fix: the setup screen path also uses citro2d if available.

Add a citro2d-rendered setup screen. Replace `render_setup_screen` contents with:

```c
static void render_setup_screen(CogRender *r) {
    cog_render_frame_begin(r);
    cog_render_target_top(r, THEME_BG_DARK);
    cog_render_text(r, "The Cog", 150, 40, THEME_FONT_HEADER, THEME_GOLD);
    cog_render_text(r, "No Remote View URL saved.",
                    80, 90, THEME_FONT_LABEL, THEME_TEXT_PRIMARY);
    cog_render_text(r, "Press X to scan a QR code.",
                    80, 115, THEME_FONT_LABEL, THEME_TEXT_DIMMED);
    cog_render_text(r, "Or save the URL to:", 80, 145, THEME_FONT_LABEL, THEME_TEXT_DIMMED);
    cog_render_text(r, "sdmc:/3ds/cog-3ds/config.txt", 80, 165, THEME_FONT_FOOTER, THEME_GOLD_DIM);
    cog_render_target_bottom(r, THEME_BG_CANVAS);
    cog_render_text(r, "Setup needed", 80, 100, THEME_FONT_HEADER, THEME_GOLD);
    cog_render_text(r, "[X] scan   [START] exit",
                    80, 160, THEME_FONT_LABEL, THEME_TEXT_DIMMED);
    cog_render_frame_end(r);
}
```

Update the setup-loop call to `render_setup_screen(&render)` (pass the CogRender*).

- [ ] **Step 5: Build and deploy**

```bash
bash build.sh && cp cog-3ds.3dsx H:/3ds/cog-3ds/cog-3ds.3dsx
```

- [ ] **Step 6: Visual verification**

- Rename `sdmc:/3ds/cog-3ds/config.txt` → `config.txt.bak` on SD
- Launch: setup screen appears in citro2d with dark bg, gold title, hints
- Press X → QR scanner with pulsing gold "SCANNING..." text
- On successful scan → returns to main canvas view with the newly-configured URL working

- [ ] **Step 7: Restore config and commit**

```bash
# Restore config.txt (either via SD swap or just let QR scanner overwrite it)
git add source/qr_scan.c source/qr_scan.h source/main.c
git commit -m "feat(phase-2a): citro2d reskin for QR scanner + setup screens"
```

---

## Post-task: Rebuild CIA + final smoke test

- [ ] **Step 1: Rebuild the CIA with all changes**

```bash
cd F:/coding/cog-3ds
bash build-cia.sh
cp cog-3ds.cia H:/cog-3ds.cia
```

- [ ] **Step 2: Install the new CIA**

Pop SD into 3DS → FBI → SD → `cog-3ds.cia` → Install and delete CIA → exit to HOME.

- [ ] **Step 3: End-to-end smoke test**

- Launch from HOME menu (gear icon)
- Verify: canvas renders with desktop positions
- Drag a card from 3DS → desktop window moves within 1s
- Move a desktop window → next 3DS poll (5s) updates the canvas
- L/R zoom works
- D-pad cycles select across cards
- B deselects

- [ ] **Step 4: Push everything**

```bash
cd F:/coding/cog-3ds
git push origin main
cd F:/coding/AgentOrch
git push origin main
```

- [ ] **Step 5: Tag release**

```bash
cd F:/coding/cog-3ds
git tag -a v0.2-phase-2 -m "Phase 2: citro2d visual + workshop canvas"
git push origin v0.2-phase-2
```
