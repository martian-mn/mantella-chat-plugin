// Subscribes to Skyrim's input device manager.
//
// Two responsibilities:
//
//   1. Edge-detect a configured "toggle" key (default: F4) and fire the
//      registered callback. Calling PrismaUI->Focus() from inside this
//      callback's call-stack (i.e. on Skyrim's input dispatch thread) is
//      what reliably engages Ultralight's keystroke router on Windows —
//      anywhere else just toggles the visual state.
//
//   2. While in "consume mode" (turned on when our chat panel is focused),
//      capture every keyboard event, translate it into a JavaScript-style
//      "key" string ("a", "Enter", "Backspace", "Shift", etc.), and forward
//      it via the registered key-typed callback. Returning kStop from
//      ProcessEvent stops downstream sinks (including the player controller)
//      from seeing the event, so the character doesn't move while typing.
//
//      This second path exists because PrismaUI relies on a Win32 WndProc
//      subclass for keystrokes, and under Wine + Proton + gamescope those
//      WM_KEYDOWN messages don't reach the hook even when Focus() succeeds.
//      BSInputDeviceManager is the only input route that's reliable on this
//      stack — it's what receives WASD/E/F4/etc. for the game itself.

#pragma once

#include <atomic>
#include <cstdint>
#include <functional>

#include "RE/Skyrim.h"

namespace mantella_chat
{
    using FocusToggleCallback = std::function<void()>;

    // Mirrors a JavaScript KeyboardEvent.key value: "a", "A", "Enter",
    // "Backspace", "Escape", "ArrowLeft", " " (space), etc. The string is
    // a static literal owned by the InputHandler — callers can store the
    // pointer indefinitely.
    using KeyTypedCallback = std::function<void(const char* key, bool shift, bool ctrl)>;

    class InputHandler : public RE::BSTEventSink<RE::InputEvent*>
    {
    public:
        static InputHandler& GetSingleton();

        // Subscribe to the game's input device manager. Idempotent.
        bool RegisterSink();

        // Configure which DirectInput scancode toggles focus.
        // Default: 0x3E (F4). Persisted in MantellaChat.ini if present.
        void SetToggleScanCode(std::uint32_t scancode);

        // Invoked on the input dispatch thread when the toggle key is pressed
        // down. Callers must do their work synchronously inside this callback
        // or schedule it via SKSE's task interface — but all Focus()/Unfocus()
        // calls should happen *here*, in this thread, for input routing to
        // engage correctly.
        void SetOnTogglePressed(FocusToggleCallback callback);

        // Invoked on the input dispatch thread for every printable / control
        // key pressed while consume mode is on. The toggle key is excluded.
        void SetOnKeyTyped(KeyTypedCallback callback);

        // Turn consume mode on/off. While on, ProcessEvent forwards every
        // keyboard event to the key-typed callback and returns kStop so
        // the rest of the input dispatch pipeline (player controller etc.)
        // never sees the events.
        void SetConsumeMode(bool enabled);

    protected:
        RE::BSEventNotifyControl ProcessEvent(
            RE::InputEvent* const* a_eventList,
            RE::BSTEventSource<RE::InputEvent*>* a_eventSource) override;

    private:
        InputHandler() = default;
        ~InputHandler() override = default;

        InputHandler(const InputHandler&) = delete;
        InputHandler& operator=(const InputHandler&) = delete;

        std::atomic<std::uint32_t> _toggleScanCode{ 0x3E };  // F4
        std::atomic<bool>          _registered{ false };

        // Callbacks are set once during plugin init, before any events fire,
        // so an atomic flag plus a plain function-object is sufficient.
        FocusToggleCallback _onToggle;
        std::atomic<bool>   _onToggleSet{ false };

        KeyTypedCallback  _onKeyTyped;
        std::atomic<bool> _onKeyTypedSet{ false };

        std::atomic<bool> _consumeMode{ false };
        std::atomic<bool> _shiftDown{ false };
        std::atomic<bool> _ctrlDown{ false };
    };
}
