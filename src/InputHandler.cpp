#include "InputHandler.h"

namespace mantella_chat
{
    namespace
    {
        // DirectInput keyboard scancodes for keys we care about.
        constexpr std::uint32_t kScanLShift     = 0x2A;
        constexpr std::uint32_t kScanRShift     = 0x36;
        constexpr std::uint32_t kScanLCtrl      = 0x1D;
        constexpr std::uint32_t kScanRCtrl      = 0x9D;  // extended
        constexpr std::uint32_t kScanLAlt       = 0x38;
        constexpr std::uint32_t kScanRAlt       = 0xB8;  // extended

        // Map a DInput scancode to a JavaScript-style KeyboardEvent.key value.
        // Shifted column is used when either shift key is down.
        //
        // We hand-roll this rather than calling CLib-NG's GetKeyboardKeyName()
        // because that returns display names ("Apostrophe", "OEM_5") instead
        // of single-character key values that the DOM consumer expects.
        //
        // Layout assumption: US QWERTY. Other layouts will get nonsense
        // characters; that's a follow-up if we ever care.
        const char* scanCodeToJSKey(std::uint32_t scan, bool shift)
        {
            // Single-character printable keys, indexed [unshifted][shifted].
            struct Entry { const char* lower; const char* upper; };
            static constexpr Entry table[0x60] = {
                /* 0x00 */ { "", "" },
                /* 0x01 */ { "Escape", "Escape" },
                /* 0x02 */ { "1", "!" },
                /* 0x03 */ { "2", "@" },
                /* 0x04 */ { "3", "#" },
                /* 0x05 */ { "4", "$" },
                /* 0x06 */ { "5", "%" },
                /* 0x07 */ { "6", "^" },
                /* 0x08 */ { "7", "&" },
                /* 0x09 */ { "8", "*" },
                /* 0x0A */ { "9", "(" },
                /* 0x0B */ { "0", ")" },
                /* 0x0C */ { "-", "_" },
                /* 0x0D */ { "=", "+" },
                /* 0x0E */ { "Backspace", "Backspace" },
                /* 0x0F */ { "Tab", "Tab" },
                /* 0x10 */ { "q", "Q" },
                /* 0x11 */ { "w", "W" },
                /* 0x12 */ { "e", "E" },
                /* 0x13 */ { "r", "R" },
                /* 0x14 */ { "t", "T" },
                /* 0x15 */ { "y", "Y" },
                /* 0x16 */ { "u", "U" },
                /* 0x17 */ { "i", "I" },
                /* 0x18 */ { "o", "O" },
                /* 0x19 */ { "p", "P" },
                /* 0x1A */ { "[", "{" },
                /* 0x1B */ { "]", "}" },
                /* 0x1C */ { "Enter", "Enter" },
                /* 0x1D */ { "Control", "Control" },
                /* 0x1E */ { "a", "A" },
                /* 0x1F */ { "s", "S" },
                /* 0x20 */ { "d", "D" },
                /* 0x21 */ { "f", "F" },
                /* 0x22 */ { "g", "G" },
                /* 0x23 */ { "h", "H" },
                /* 0x24 */ { "j", "J" },
                /* 0x25 */ { "k", "K" },
                /* 0x26 */ { "l", "L" },
                /* 0x27 */ { ";", ":" },
                /* 0x28 */ { "'", "\"" },
                /* 0x29 */ { "`", "~" },
                /* 0x2A */ { "Shift", "Shift" },
                /* 0x2B */ { "\\", "|" },
                /* 0x2C */ { "z", "Z" },
                /* 0x2D */ { "x", "X" },
                /* 0x2E */ { "c", "C" },
                /* 0x2F */ { "v", "V" },
                /* 0x30 */ { "b", "B" },
                /* 0x31 */ { "n", "N" },
                /* 0x32 */ { "m", "M" },
                /* 0x33 */ { ",", "<" },
                /* 0x34 */ { ".", ">" },
                /* 0x35 */ { "/", "?" },
                /* 0x36 */ { "Shift", "Shift" },
                /* 0x37 */ { "*", "*" },
                /* 0x38 */ { "Alt", "Alt" },
                /* 0x39 */ { " ", " " },
                /* 0x3A */ { "CapsLock", "CapsLock" },
                /* 0x3B */ { "F1", "F1" },
                /* 0x3C */ { "F2", "F2" },
                /* 0x3D */ { "F3", "F3" },
                /* 0x3E */ { "F4", "F4" },
                /* 0x3F */ { "F5", "F5" },
                /* 0x40 */ { "F6", "F6" },
                /* 0x41 */ { "F7", "F7" },
                /* 0x42 */ { "F8", "F8" },
                /* 0x43 */ { "F9", "F9" },
                /* 0x44 */ { "F10", "F10" },
                /* 0x45 */ { "NumLock", "NumLock" },
                /* 0x46 */ { "ScrollLock", "ScrollLock" },
                /* 0x47 */ { "Home", "Home" },
                /* 0x48 */ { "ArrowUp", "ArrowUp" },
                /* 0x49 */ { "PageUp", "PageUp" },
                /* 0x4A */ { "-", "-" },
                /* 0x4B */ { "ArrowLeft", "ArrowLeft" },
                /* 0x4C */ { "5", "5" },
                /* 0x4D */ { "ArrowRight", "ArrowRight" },
                /* 0x4E */ { "+", "+" },
                /* 0x4F */ { "End", "End" },
                /* 0x50 */ { "ArrowDown", "ArrowDown" },
                /* 0x51 */ { "PageDown", "PageDown" },
                /* 0x52 */ { "Insert", "Insert" },
                /* 0x53 */ { "Delete", "Delete" },
                /* 0x54 */ { "", "" },
                /* 0x55 */ { "", "" },
                /* 0x56 */ { "\\", "|" },
                /* 0x57 */ { "F11", "F11" },
                /* 0x58 */ { "F12", "F12" },
                /* 0x59 */ { "", "" }, /* 0x5A */ { "", "" },
                /* 0x5B */ { "", "" }, /* 0x5C */ { "", "" },
                /* 0x5D */ { "", "" }, /* 0x5E */ { "", "" }, /* 0x5F */ { "", "" },
            };

            if (scan >= 0x60) return "";
            const auto& e = table[scan];
            const char* result = shift ? e.upper : e.lower;
            return (result && *result) ? result : "";
        }

        bool isModifierScan(std::uint32_t scan)
        {
            return scan == kScanLShift || scan == kScanRShift ||
                   scan == kScanLCtrl  || scan == kScanRCtrl  ||
                   scan == kScanLAlt   || scan == kScanRAlt;
        }
    }

    InputHandler& InputHandler::GetSingleton()
    {
        static InputHandler instance;
        return instance;
    }

    bool InputHandler::RegisterSink()
    {
        if (_registered.exchange(true)) return true;

        auto* manager = RE::BSInputDeviceManager::GetSingleton();
        if (!manager) {
            _registered.store(false);
            return false;
        }
        manager->AddEventSink(this);
        return true;
    }

    void InputHandler::SetToggleScanCode(std::uint32_t scancode)
    {
        _toggleScanCode.store(scancode);
    }

    void InputHandler::SetOnTogglePressed(FocusToggleCallback callback)
    {
        _onToggle = std::move(callback);
        _onToggleSet.store(_onToggle != nullptr);
    }

    void InputHandler::SetOnKeyTyped(KeyTypedCallback callback)
    {
        _onKeyTyped = std::move(callback);
        _onKeyTypedSet.store(_onKeyTyped != nullptr);
    }

    void InputHandler::SetConsumeMode(bool enabled)
    {
        _consumeMode.store(enabled);
        // Reset modifier state on transition so a stale Shift held during
        // toggle doesn't bleed across mode boundaries.
        if (!enabled) {
            _shiftDown.store(false);
            _ctrlDown.store(false);
        }
    }

    RE::BSEventNotifyControl InputHandler::ProcessEvent(
        RE::InputEvent* const* a_eventList,
        RE::BSTEventSource<RE::InputEvent*>* /*a_eventSource*/)
    {
        if (!a_eventList) {
            return RE::BSEventNotifyControl::kContinue;
        }

        const std::uint32_t toggle    = _toggleScanCode.load();
        const bool          consume   = _consumeMode.load();
        const bool          haveTyped = _onKeyTypedSet.load();

        for (auto* event = *a_eventList; event != nullptr; event = event->next) {
            if (event->GetEventType() != RE::INPUT_EVENT_TYPE::kButton) continue;

            auto* button = event->AsButtonEvent();
            if (!button) continue;
            if (button->GetDevice() != RE::INPUT_DEVICE::kKeyboard) continue;

            const std::uint32_t scan = button->GetIDCode();

            // Track modifier state on every press/release, even outside
            // consume mode, so the first Shift after focusing has the
            // right value.
            if (scan == kScanLShift || scan == kScanRShift) {
                _shiftDown.store(button->IsPressed());
            } else if (scan == kScanLCtrl || scan == kScanRCtrl) {
                _ctrlDown.store(button->IsPressed());
            }

            if (!button->IsDown()) continue;  // press-edge only

            // Toggle key always fires its callback, regardless of mode.
            // We don't pass it to the key-typed callback.
            if (scan == toggle) {
                if (_onToggleSet.load()) _onToggle();
                continue;
            }

            if (consume && haveTyped && !isModifierScan(scan)) {
                const char* key = scanCodeToJSKey(scan, _shiftDown.load());
                if (key && *key) {
                    _onKeyTyped(key, _shiftDown.load(), _ctrlDown.load());
                }
            }
        }

        // Consume mode swallows the entire batch — downstream sinks (player
        // controller, hotkey handlers) don't see the events, so the player
        // doesn't move/jump/cast spells while typing.
        return consume ? RE::BSEventNotifyControl::kStop
                       : RE::BSEventNotifyControl::kContinue;
    }
}
