// Mantella in-game chat overlay — SKSE plugin entry point.
//
// Bridges PrismaUI (HTML/CSS/JS view) <-> Mantella's Python server.
//
// - Polls /chat/transcript on a background thread, pushes new entries to the view.
// - Forwards the user's typed input from the view to /chat/input.
// - F4 hotkey toggles input focus.
//
// Built without CommonLibSSE-NG to keep cross-compile from Linux straightforward.
// The few SKSE ABI definitions we need live in skse_minimal.h.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define CPPHTTPLIB_NO_EXCEPTIONS

#include <windows.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

#include "PrismaUI_API.h"
#include "skse_minimal.h"
#include "vendor/httplib.h"
#include "vendor/json.hpp"

namespace
{
    PRISMA_UI_API::IVPrismaUI1* g_prisma = nullptr;
    PrismaView g_view = 0;
    std::atomic<bool> g_focused{false};
    std::atomic<bool> g_conversation_active{false};
    SKSE::TaskInterface* g_task = nullptr;

    void syncFocusToConversation();  // forward decl — defined below the MantellaPoller class

    // ---- SKSE main-thread task dispatcher --------------------------------
    // PrismaUI->Focus / ->Invoke must run on the main game thread to actually
    // capture input. We use SKSE's UI task queue to schedule a lambda there.
    template <typename F>
    class LambdaTask : public SKSE::TaskDelegate
    {
    public:
        explicit LambdaTask(F&& fn) : _fn(std::move(fn)) {}
        void Run() override { _fn(); }
        void Dispose() override { delete this; }
    private:
        F _fn;
    };

    template <typename F>
    void mainThread(F&& fn)
    {
        if (g_task && g_task->AddUITask) {
            g_task->AddUITask(new LambdaTask<F>(std::forward<F>(fn)));
        } else {
            // Fallback: call inline if we don't have the task interface.
            fn();
        }
    }

    // ---- Logging --------------------------------------------------------
    // CommonLibSSE-NG provides spdlog; we'll write to a simple file in
    // %USERPROFILE%/My Games/Skyrim Special Edition/SKSE/MantellaChat.log.
    void log_line(const char* level, const std::string& msg)
    {
        static std::mutex log_mu;
        static FILE* log_fp = nullptr;
        std::lock_guard<std::mutex> lock(log_mu);
        if (!log_fp) {
            char path[MAX_PATH] = {0};
            const char* userprofile = std::getenv("USERPROFILE");
            if (userprofile) {
                std::snprintf(path, sizeof(path),
                    "%s\\Documents\\My Games\\Skyrim Special Edition\\SKSE\\MantellaChat.log",
                    userprofile);
                log_fp = std::fopen(path, "w");
            }
            if (!log_fp) {
                log_fp = std::fopen("MantellaChat.log", "w");
            }
        }
        if (log_fp) {
            std::fprintf(log_fp, "[%s] %s\n", level, msg.c_str());
            std::fflush(log_fp);
        }
    }

    #define LOG_INFO(msg)  log_line("info",  std::string(msg))
    #define LOG_ERROR(msg) log_line("error", std::string(msg))

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
            nlohmann::json body = {{"text", text}};
            auto res = cli.Post("/chat/input", body.dump(), "application/json");
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

                std::string path = "/chat/transcript?since=" + formatSince();
                auto res = cli.Get(path);
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
                bool sawSystemEnd = false;
                bool sawAnyContent = false;
                for (auto& e : entries) {
                    if (e.contains("timestamp") && e["timestamp"].is_number()) {
                        double ts = e["timestamp"].get<double>();
                        if (ts > latest) latest = ts;
                    }
                    if (e.contains("kind") && e["kind"].is_string()) {
                        std::string kind = e["kind"].get<std::string>();
                        std::string text = e.contains("text") && e["text"].is_string()
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
                    std::string js = "mantellaUpdate(" + entries.dump() + ")";
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

        std::string _host;
        int _port;
        std::atomic<bool> _running{false};
        std::atomic<double> _since{0.0};
        std::thread _thread;
    };

    MantellaPoller g_poller("127.0.0.1", 4999);

    // ---- Auto-focus on conversation activity ---------------------------
    // GetAsyncKeyState polling is unreliable under Wine + gamescope + Skyrim's
    // exclusive input grab — keypress events never bubble up to our background
    // thread. Instead of fighting the input system, focus the panel
    // automatically whenever a conversation is active and unfocus when it
    // ends. The player is talking to an NPC — character movement is paused
    // by the conversation script anyway, so grabbing input is acceptable.
    void syncFocusToConversation()
    {
        if (!g_prisma || !g_view) return;
        bool should_focus = g_conversation_active.load();
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

    // ---- View ready callback -------------------------------------------
    void onViewReady(PrismaView view)
    {
        LOG_INFO("PrismaUI view DOM ready");

        if (g_prisma) {
            g_prisma->Invoke(view, "mantellaStatus('idle')");
        }

        g_prisma->RegisterJSListener(view, "mantellaSubmitInput", [](const char* data) -> void {
            if (!data) return;
            std::string text(data);
            LOG_INFO("submit: " + text);
            std::thread([text]() {
                bool ok = g_poller.postPlayerInput(text);
                if (g_prisma && g_view) {
                    const char* js = ok
                        ? "mantellaStatus('thinking')"
                        : "mantellaStatus('error: server unreachable')";
                    g_prisma->Invoke(g_view, js);
                }
            }).detach();
        });

        // Listener for the user pressing Escape inside the chat panel —
        // returns input control to Skyrim.
        g_prisma->RegisterJSListener(view, "mantellaRequestUnfocus", [](const char*) -> void {
            mainThread([]() {
                if (!g_prisma || !g_view) return;
                g_prisma->Unfocus(g_view);
                g_prisma->Invoke(g_view, "mantellaFocus(false)");
                g_focused.store(false);
            });
        });

        // Listener for clicking the chat panel — grants focus from a
        // mouse-event context which seems to engage Ultralight's input
        // routing more reliably than our auto-focus on transcript activity.
        g_prisma->RegisterJSListener(view, "mantellaRequestFocus", [](const char*) -> void {
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

        g_prisma = static_cast<PRISMA_UI_API::IVPrismaUI1*>(
            PRISMA_UI_API::RequestPluginAPI(PRISMA_UI_API::InterfaceVersion::V1));
        if (!g_prisma) {
            LOG_ERROR("Failed to acquire PrismaUI API; chat overlay disabled");
            return;
        }

        g_view = g_prisma->CreateView("Mantella-Chat-UI/index.html", &onViewReady);
    }
}

// ---- SKSE plugin descriptors --------------------------------------------
// AE-era SKSE looks up the SKSEPlugin_Version struct by symbol name.
SKSE_API SKSE::PluginVersionData SKSEPlugin_Version = {
    SKSE::PluginVersionData::kVersion,    // dataVersion
    1,                                    // pluginVersion
    "MantellaChat",                       // name
    "Mantella text-only fork",            // author
    "",                                   // supportEmail
    0,                                    // versionIndependenceEx
    SKSE::PluginVersionData::kVersionIndependent_AddressLibraryPostAE
        | SKSE::PluginVersionData::kVersionIndependent_Signatures
        | SKSE::PluginVersionData::kVersionIndependent_StructsPost629, // versionIndependence
    {0},                                  // compatibleVersions (any)
    0,                                    // seVersionRequired
};

SKSE_API bool SKSEPlugin_Load(const SKSE::LoadInterface* a_skse)
{
    auto messaging = static_cast<SKSE::MessagingInterface*>(
        a_skse->QueryInterface(SKSE::LoadInterface::kMessaging));
    if (!messaging) {
        LOG_ERROR("Failed to acquire SKSE messaging interface");
        return false;
    }

    std::uint32_t handle = a_skse->GetPluginHandle();
    messaging->RegisterListener(handle, "SKSE", SKSEMessageHandler);

    g_task = static_cast<SKSE::TaskInterface*>(
        a_skse->QueryInterface(SKSE::LoadInterface::kTask));
    if (!g_task) {
        LOG_ERROR("Failed to acquire SKSE TaskInterface — focus calls will be inline");
    }

    LOG_INFO("MantellaChat plugin loaded");
    return true;
}
