# MantellaChat — In-Game Chat Plugin Design

**Status:** Design doc, written 2026-05-07 after a failed first iteration.
**Goal:** A stable, well-engineered in-game chat overlay for Mantella that lets the player see the conversation and type replies without leaving Skyrim's window.

---

## What this is, plainly

A SKSE plugin (.dll) plus a small HTML/CSS/JS bundle that PrismaUI renders inside Skyrim. The plugin is a thin bridge between the in-game UI and the Mantella Python server. The Python server stays unchanged from upstream Mantella's text-only fork — same HTTP endpoints, same conversation flow.

The player sees a small chat panel in the corner of the screen during a Mantella conversation. NPC dialogue appears live. The player presses a hotkey, types a reply, presses Enter. The NPC responds. Repeat.

## What went wrong in the first iteration

We built everything except proper input integration. The first iteration:
- ✅ Python HTTP endpoints (`/chat/transcript` GET, `/chat/input` POST)
- ✅ HTML/CSS/JS chat UI rendering inside Skyrim via PrismaUI
- ✅ Live transcript updates from the Python server
- ❌ Typing into the in-game panel — did not work

**Root cause of the typing failure:** to keep cross-compilation simple, we built without CommonLibSSE-NG (because that library hard-depends on Microsoft DirectXMath/DirectXTK, which mingw can't satisfy). That meant we couldn't subscribe to Skyrim's `RE::BSInputDeviceManager` input event stream. PrismaUI's `Focus()` only properly engages its keystroke router when called from within that event context — we were calling it from a SKSE TaskInterface task, which runs on the main thread but outside the input dispatch chain. Skyrim grabbed keystrokes before they could reach Ultralight (the engine PrismaUI uses).

This iteration accepts the tooling cost and builds with the full CommonLibSSE-NG, using Docker to host MSVC.

## Architecture

### Three components, separated by clear seams

```
┌──────────────────────────────────────────────────────────────┐
│  Skyrim SE (in Proton / Wine)                                │
│  ┌────────────────────────────────────────────────────────┐  │
│  │  SKSE                                                  │  │
│  │  ┌──────────────────────────────────────────────────┐  │  │
│  │  │  PrismaUI.dll (third party, NM mod 148718)       │  │  │
│  │  │     └─ renders HTML/CSS/JS via Ultralight        │  │  │
│  │  └─────────────────▲────────────────────────────────┘  │  │
│  │                    │ RegisterJSListener / Invoke /     │  │
│  │                    │ Focus / Unfocus                   │  │
│  │  ┌─────────────────┴────────────────────────────────┐  │  │
│  │  │  MantellaChat.dll (this plugin)                  │  │  │
│  │  │    - subscribes to BSInputDeviceManager          │  │  │
│  │  │    - calls Focus() from input event context      │  │  │
│  │  │    - polls Python server, pushes updates to view │  │  │
│  │  │    - receives typed text from view, POSTs        │  │  │
│  │  └─────────────────┬────────────────────────────────┘  │  │
│  └────────────────────┼───────────────────────────────────┘  │
└───────────────────────┼──────────────────────────────────────┘
                        │ HTTP (localhost:4999)
                        │
┌───────────────────────▼──────────────────────────────────────┐
│  Mantella Python server (native Linux)                       │
│   /chat/transcript  GET   ← polling                           │
│   /chat/input       POST  ← player text                       │
│   /mantella         POST  ← unchanged Mantella protocol       │
└──────────────────────────────────────────────────────────────┘
```

### Design principles

1. **The Python server is the source of truth** for conversation state. The plugin is a dumb client.
2. **No game-state mutations in the plugin.** No actor manipulation, no quest stage changes. Just input/UI bridging.
3. **Clear seams:** the plugin doesn't know about LLMs, the UI doesn't know about HTTP.
4. **Input integration via Skyrim's own pipeline.** All `Focus()` and `Unfocus()` calls happen from within `RE::BSInputDeviceManager`'s event handler.
5. **Defensive by default:** server unreachable, view not yet ready, conversation already ended — every code path handles these without crashing.

### Threading model

| Thread | Owns | Calls |
|---|---|---|
| Main / game thread | PrismaUI view, Skyrim game state | Focus, Unfocus, Invoke (queued by SKSE TaskInterface) |
| Polling thread | HTTP client to Python server | `/chat/transcript`, `/chat/input` |
| Input event thread | Subscription to BSInputDeviceManager | F4/hotkey → schedules main-thread Focus() |
| Submit worker (short-lived) | Single POST to /chat/input | Schedules JS status update on main thread |

All cross-thread state goes through `std::atomic` flags. UI updates are dispatched via `SKSE::TaskInterface::AddUITask` to ensure they run on the main thread.

## Components

### 1. The SKSE plugin (`MantellaChat.dll`)

C++23, built with CommonLibSSE-NG against Skyrim AE 1.6.x runtime. Uses CommonLibSSE-NG's `RE::` namespace types for input integration. Embeds `cpp-httplib` (single-header HTTP client) and `nlohmann/json` (single-header JSON).

Sourced files:
- `src/main.cpp` — plugin entry, message handler, initialization
- `src/InputHandler.{h,cpp}` — subscribes to `RE::BSInputDeviceManager`, dispatches focus toggle on hotkey
- `src/MantellaPoller.{h,cpp}` — background HTTP polling, transcript delta delivery
- `src/Bridge.{h,cpp}` — JS↔C++ wiring (RegisterJSListener calls, Invoke wrappers)
- `src/Logger.{h,cpp}` — file-based logger (no spdlog dependency)

### 2. The view (`view/`)

- `index.html` — chat panel layout (header, transcript, input form)
- `styles.css` — Skyrim-themed dark transparent panel
- `app.js` — bridge stubs for SKSE plugin, transcript rendering, input form submit

The JS exposes:
- `window.mantellaUpdate(entries)` — called by C++ to add transcript entries
- `window.mantellaStatus(text)` — called by C++ to update the status indicator
- `window.mantellaFocus(focused)` — called by C++ when focus state changes
- `window.mantellaClear()` — called by C++ to clear transcript

The JS calls into C++ via:
- `window.mantellaSubmitInput(text)` — registered by C++ as JS listener
- `window.mantellaRequestUnfocus()` — registered by C++ for in-panel ESC

### 3. Python server endpoints (already done)

In `mantella-fork`, `src/http/routes/chat_route.py` already adds `/chat/transcript` and `/chat/input` routes wired to a `ConversationBus` singleton. No changes needed for this iteration.

## Build environment

Docker + msvc-wine. The msvc-wine project (mstorsjo/msvc-wine) packages MSVC's compiler in a Wine container, runnable from Linux. xmake supports MSVC builds; we'll point it at the wine-wrapped MSVC.

Setup steps (executed in this order, verified at each stage):
1. Pull or build the msvc-wine Docker image.
2. Install MSVC components inside the image (~3GB).
3. Verify the image can compile a "hello world" Windows DLL.
4. Configure xmake to use it (`xmake.lua` toolchain pointer).
5. Build the original PrismaUI example template (with full CommonLibSSE-NG) to validate the toolchain end-to-end. This is the reference: if the example builds and runs, our plugin will too.

## Hotkey strategy

Default toggle key: **F4**. Configurable via a small `MantellaChat.ini` file the plugin reads at load time. The key is registered via `RE::BSInputDeviceManager` so it works through Skyrim's input pipeline (the same pipeline that captures keystrokes for camera/movement).

When the chat panel is focused, all keystrokes go to the panel (Skyrim freezes input — by design, conversation paused). The panel's HTML `<input>` element receives them via Ultralight's normal event dispatch (because we're now properly integrated with Skyrim's input system — `Focus()` actually engages routing).

To unfocus: press F4 again, or press Escape from inside the panel (handled by JS document-level keydown).

## Robustness

Every async operation tolerates failure:
- Server unreachable: poller backs off (1s → 5s → 30s), retries.
- View not ready when conversation starts: queue the update, deliver when DOM ready fires.
- User submits while server is slow: input field disables, status changes to "sending...", re-enables on response or timeout.
- Conversation ends while user is typing: panel auto-unfocuses, retains the in-progress text (don't lose user work).
- Plugin loaded before PrismaUI: defer view creation, retry on next message.

## What we're not doing in this iteration

- No multi-channel chat (Tot!Chat has channels; we don't need them).
- No speech bubbles over NPC heads (would need a separate native rendering path; out of scope).
- No font customization, no opacity slider — defaults are reasonable, polish later.
- No save/persist of chat history beyond what Mantella's own conversation log already does.

## Success criteria

End-to-end test passes when, on the user's CachyOS + Skyrim AE + Proton 9.0 + gamescope setup:
1. Plugin loads cleanly (visible in `skse64.log`).
2. Chat panel renders in-game in the bottom-right corner.
3. NPC dialogue appears in the panel within ~1s of being generated by the LLM.
4. Pressing F4 focuses the panel (visual brighten + Skyrim input pause).
5. Typing letters appears in the input field.
6. Pressing Enter submits the text; NPC responds within a few seconds.
7. Pressing F4 or Escape returns control to Skyrim.
8. Conversation ends cleanly when player walks away or NPC dismisses.
9. No crashes after 30 minutes of typical play.
