// Mantella in-game chat overlay — SKSE plugin entry point.
//
// Bridges PrismaUI (HTML/CSS/JS view) <-> Mantella's Python server.
//   * Polls /chat/transcript on a background thread, pushes new entries
//     to the view via Invoke().
//   * Forwards typed input from the view to /chat/input.
//   * F4 hotkey toggles input focus. The toggle MUST run inside Skyrim's
//     input dispatch context (BSInputDeviceManager event sink) — calling
//     PrismaUI->Focus() from any other context only flips the visual flag
//     without engaging Ultralight's keystroke router.

#define CPPHTTPLIB_NO_EXCEPTIONS

#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>

#include "InputHandler.h"
#include "PrismaUI_API.h"
#include "vendor/httplib.h"
#include "vendor/json.hpp"

namespace logger = SKSE::log;

namespace
{
    // Held as IVPrismaUI1* so the rest of the plugin only needs the V1 surface.
    // We try to upgrade to V2 separately so we can register a console callback
    // for in-page diagnostics — V2 is optional, V1 is required.
    PRISMA_UI_API::IVPrismaUI1* g_prisma = nullptr;
    PRISMA_UI_API::IVPrismaUI2* g_prisma_v2 = nullptr;
    PrismaView                  g_view = 0;
    std::atomic<bool>           g_focused{ false };
    std::atomic<bool>           g_conversation_active{ false };

    // Forward decl — defined below MantellaPoller because the poller calls it.
    void syncFocusToConversation();

    // ---- Main-thread dispatch -------------------------------------------
    // PrismaUI->Invoke() must run on Skyrim's UI/main thread or the JS bridge
    // misbehaves under some Wine + gamescope combinations. CLib-NG's
    // TaskInterface accepts a std::function directly, so the LambdaTask
    // wrapper we used previously is gone.
    template <typename F>
    void mainThread(F&& fn)
    {
        if (const auto* task = SKSE::GetTaskInterface()) {
            task->AddUITask(std::forward<F>(fn));
        } else {
            // Fallback: SKSE init failed — better to run inline than drop work.
            fn();
        }
    }

    // ---- HTTP polling ---------------------------------------------------
    class MantellaPoller
    {
    public:
        MantellaPoller(std::string host, int port)
            : _host(std::move(host)), _port(port) {}

        void start()
        {
            if (_running.exchange(true)) return;
            _thread = std::thread([this]() { runLoop(); });
        }

        void stop()
        {
            _running.store(false);
            if (_thread.joinable()) _thread.join();
        }

        bool postPlayerInput(const std::string& text)
        {
            httplib::Client cli(_host, _port);
            cli.set_connection_timeout(2, 0);
            cli.set_read_timeout(5, 0);
            const nlohmann::json body = {{ "text", text }};
            const auto res = cli.Post("/chat/input", body.dump(), "application/json");
            return res && res->status == 200;
        }

    private:
        void runLoop()
        {
            httplib::Client cli(_host, _port);
            cli.set_connection_timeout(2, 0);
            cli.set_read_timeout(5, 0);

            while (_running.load()) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
                if (!_running.load()) break;

                const std::string path = "/chat/transcript?since=" + formatSince();
                const auto res = cli.Get(path);
                if (!res || res->status != 200) continue;

                nlohmann::json doc;
                try {
                    doc = nlohmann::json::parse(res->body);
                } catch (...) {
                    continue;
                }
                if (!doc.contains("entries") || !doc["entries"].is_array()) continue;

                auto& entries = doc["entries"];
                if (entries.empty()) continue;

                double latest = _since.load();
                bool   sawSystemEnd = false;
                bool   sawAnyContent = false;
                for (auto& e : entries) {
                    if (e.contains("timestamp") && e["timestamp"].is_number()) {
                        const double ts = e["timestamp"].get<double>();
                        if (ts > latest) latest = ts;
                    }
                    if (e.contains("kind") && e["kind"].is_string()) {
                        const std::string kind = e["kind"].get<std::string>();
                        const std::string text = e.contains("text") && e["text"].is_string()
                            ? e["text"].get<std::string>() : "";
                        if (kind == "system" && text.find("ended") != std::string::npos) {
                            sawSystemEnd = true;
                        } else {
                            sawAnyContent = true;
                        }
                    }
                }
                _since.store(latest);

                if (sawAnyContent) g_conversation_active.store(true);
                if (sawSystemEnd)  g_conversation_active.store(false);
                syncFocusToConversation();

                if (g_prisma && g_view) {
                    const std::string js = "mantellaUpdate(" + entries.dump() + ")";
                    mainThread([js]() {
                        if (g_prisma && g_view) g_prisma->Invoke(g_view, js.c_str());
                    });
                }
            }
        }

        std::string formatSince() const
        {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%.6f", _since.load());
            return std::string(buf);
        }

        std::string         _host;
        int                 _port;
        std::atomic<bool>   _running{ false };
        std::atomic<double> _since{ 0.0 };
        std::thread         _thread;
    };

    MantellaPoller g_poller("127.0.0.1", 4999);

    // ---- Auto-focus on conversation activity ----------------------------
    // Convenience for transcript-driven focus changes. The user can also
    // toggle focus manually via F4, which goes through InputHandler so the
    // call originates from the input-dispatch thread.
    //
    // Note: focus changes scheduled here run on the UI task queue, which
    // does engage Ultralight's input router under gamescope as long as a
    // game frame is in flight. For the F4 path we still go straight through
    // InputHandler — that's the most reliable path.
    void syncFocusToConversation()
    {
        if (!g_prisma || !g_view) return;
        const bool should_focus = g_conversation_active.load();
        if (should_focus && !g_focused.load()) {
            mainThread([]() {
                if (!g_prisma || !g_view) return;
                if (g_prisma->Focus(g_view)) {
                    g_prisma->Invoke(g_view, "mantellaFocus(true)");
                    g_focused.store(true);
                }
            });
        } else if (!should_focus && g_focused.load()) {
            mainThread([]() {
                if (!g_prisma || !g_view) return;
                g_prisma->Unfocus(g_view);
                g_prisma->Invoke(g_view, "mantellaFocus(false)");
                g_focused.store(false);
            });
        }
    }

    // ---- F4 toggle handler (runs inside BSInputDeviceManager dispatch) --
    // This is the only path that reliably engages Ultralight's keystroke
    // router. We must call Focus()/Unfocus() synchronously from inside
    // ProcessEvent — NOT via AddUITask. See InputHandler.h for context.
    void onF4Pressed()
    {
        if (!g_prisma || !g_view) return;
        if (g_focused.load()) {
            g_prisma->Unfocus(g_view);
            g_prisma->Invoke(g_view, "mantellaFocus(false)");
            g_focused.store(false);
            logger::info("F4: focus released");
        } else {
            if (g_prisma->Focus(g_view)) {
                g_prisma->Invoke(g_view, "mantellaFocus(true)");
                g_focused.store(true);
                logger::info("F4: focus engaged");
            } else {
                logger::warn("F4: PrismaUI->Focus() returned false");
            }
        }
    }

    // ---- View ready callback -------------------------------------------
    void onViewReady(PrismaView view)
    {
        logger::info("PrismaUI view DOM ready");

        if (g_prisma) {
            g_prisma->Invoke(view, "mantellaStatus('idle')");
        }

        g_prisma->RegisterJSListener(view, "mantellaSubmitInput", [](const char* data) {
            if (!data) return;
            const std::string text(data);
            logger::info("submit: {}", text);
            std::thread([text]() {
                const bool ok = g_poller.postPlayerInput(text);
                if (g_prisma && g_view) {
                    const char* js = ok
                        ? "mantellaStatus('thinking')"
                        : "mantellaStatus('error: server unreachable')";
                    g_prisma->Invoke(g_view, js);
                }
            }).detach();
        });

        // Pressing Escape inside the chat panel returns control to Skyrim.
        g_prisma->RegisterJSListener(view, "mantellaRequestUnfocus", [](const char*) {
            mainThread([]() {
                if (!g_prisma || !g_view) return;
                g_prisma->Unfocus(g_view);
                g_prisma->Invoke(g_view, "mantellaFocus(false)");
                g_focused.store(false);
            });
        });

        // Clicking the chat panel grants focus from a mouse-event context,
        // which (like F4 via InputHandler) reliably engages input routing.
        g_prisma->RegisterJSListener(view, "mantellaRequestFocus", [](const char*) {
            mainThread([]() {
                if (!g_prisma || !g_view) return;
                if (g_prisma->Focus(g_view)) {
                    g_prisma->Invoke(g_view, "mantellaFocus(true)");
                    g_focused.store(true);
                }
            });
        });

        g_poller.start();
    }

    // ---- SKSE message handler ------------------------------------------
    void SKSEMessageHandler(SKSE::MessagingInterface::Message* msg)
    {
        if (msg->type != SKSE::MessagingInterface::kDataLoaded) return;

        // Acquire PrismaUI's plugin API. Loaded by SKSE before us, so this
        // can't fail under normal conditions; if it does, we just disable
        // the overlay rather than crash the process.
        g_prisma = static_cast<PRISMA_UI_API::IVPrismaUI1*>(
            PRISMA_UI_API::RequestPluginAPI(PRISMA_UI_API::InterfaceVersion::V1));
        if (!g_prisma) {
            logger::error("Failed to acquire PrismaUI API; chat overlay disabled");
            return;
        }

        g_view = g_prisma->CreateView("Mantella-Chat-UI/index.html", &onViewReady);

        // Try to upgrade to the V2 interface for console-log forwarding.
        // V2 is purely diagnostic — if PrismaUI is older / V2 isn't available
        // we just skip console forwarding without disabling the overlay.
        g_prisma_v2 = PRISMA_UI_API::RequestPluginAPI<PRISMA_UI_API::IVPrismaUI2>();
        if (g_prisma_v2) {
            g_prisma_v2->RegisterConsoleCallback(g_view,
                [](PrismaView, PRISMA_UI_API::ConsoleMessageLevel level, const char* message) {
                    if (!message) return;
                    using L = PRISMA_UI_API::ConsoleMessageLevel;
                    switch (level) {
                        case L::Error:   logger::error("JS console: {}", message); break;
                        case L::Warning: logger::warn("JS console: {}", message);  break;
                        default:         logger::info("JS console: {}", message);  break;
                    }
                });
            logger::info("PrismaUI V2 console callback registered");
        } else {
            logger::warn("PrismaUI V2 not available; console messages will not be forwarded");
        }

        // Register the F4 input handler against Skyrim's input device manager.
        // RegisterSink() pulls BSInputDeviceManager::GetSingleton() — must
        // happen at-or-after kDataLoaded so the singleton exists.
        auto& input = mantella_chat::InputHandler::GetSingleton();
        input.SetOnTogglePressed(&onF4Pressed);
        if (!input.RegisterSink()) {
            logger::error("Failed to register input event sink — F4 hotkey disabled");
        } else {
            logger::info("Input sink registered, F4 toggle armed");
        }
    }
}

// ---- SKSE plugin descriptors --------------------------------------------
// CLib-NG's SKSEPluginVersion macro emits the constinit struct in the right
// section so SKSE's plugin loader can read it.
SKSEPluginVersion = []() noexcept {
    SKSE::PluginVersionData v{};
    v.PluginVersion(REL::Version{ 0, 1, 0 });
    v.PluginName("MantellaChat");
    v.AuthorName("Mantella text-only fork");
    v.UsesAddressLibrary();
    v.UsesUpdatedStructs();
    return v;
}();

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
    // SKSE::Init wires up the plugin handle, interface accessors, and
    // spdlog file sink. After this call, logger::info(...) writes to
    // %USERPROFILE%/Documents/My Games/Skyrim Special Edition/SKSE/MantellaChat.log
    // automatically — no more hand-rolled log_line.
    SKSE::Init(a_skse);

    auto* messaging = SKSE::GetMessagingInterface();
    if (!messaging) {
        logger::error("Failed to acquire SKSE messaging interface");
        return false;
    }
    messaging->RegisterListener("SKSE", SKSEMessageHandler);

    logger::info("MantellaChat plugin loaded");
    return true;
}
