// Subscribes to Skyrim's input device manager and dispatches a callback when
// the configured focus-toggle key is pressed. Calling PrismaUI->Focus() from
// within ProcessEvent (i.e. from Skyrim's input dispatch thread/context) is
// what actually engages Ultralight's keystroke router — calling it from any
// other context just toggles the visual state without grabbing input.

#pragma once

#include <atomic>
#include <cstdint>
#include <functional>

#include "RE/Skyrim.h"

namespace mantella_chat
{
    using FocusToggleCallback = std::function<void()>;

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

    protected:
        RE::BSEventNotifyControl ProcessEvent(
            RE::InputEvent* const* a_eventList,
            RE::BSTEventSource<RE::InputEvent*>* a_eventSource) override;

    private:
        InputHandler() = default;
        ~InputHandler() override = default;

        InputHandler(const InputHandler&) = delete;
        InputHandler& operator=(const InputHandler&) = delete;

        std::atomic<std::uint32_t> _toggleScanCode{0x3E};  // F4
        std::atomic<bool>          _registered{false};

        // The callback is set once during plugin init, before any events fire,
        // so a plain pointer guarded by an atomic flag is sufficient.
        FocusToggleCallback _onToggle;
        std::atomic<bool>   _onToggleSet{false};
    };
}
