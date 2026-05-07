#include "InputHandler.h"

namespace mantella_chat
{
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

    RE::BSEventNotifyControl InputHandler::ProcessEvent(
        RE::InputEvent* const* a_eventList,
        RE::BSTEventSource<RE::InputEvent*>* /*a_eventSource*/)
    {
        if (!a_eventList || !_onToggleSet.load()) {
            return RE::BSEventNotifyControl::kContinue;
        }

        const std::uint32_t target = _toggleScanCode.load();

        for (auto* event = *a_eventList; event != nullptr; event = event->next) {
            if (event->GetEventType() != RE::INPUT_EVENT_TYPE::kButton) continue;

            auto* button = event->AsButtonEvent();
            if (!button) continue;

            // Only fire on key-down edge (IsDown is true on the press frame).
            if (!button->IsDown()) continue;

            // Filter to keyboard events. Mouse / gamepad scancodes overlap
            // with keyboard scancodes and would otherwise produce false positives.
            if (button->GetDevice() != RE::INPUT_DEVICE::kKeyboard) continue;

            if (button->GetIDCode() == target) {
                _onToggle();
                // Don't return kStop — other plugins / vanilla controls may
                // also want to see this event. The conversation script is
                // already paused on player input, so propagating is harmless.
            }
        }

        return RE::BSEventNotifyControl::kContinue;
    }
}
