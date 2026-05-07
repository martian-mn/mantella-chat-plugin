// Minimal SKSE plugin ABI definitions.
//
// CommonLibSSE-NG depends on MSVC-only DirectX libs and breaks cross-compile
// from Linux. For a plugin that only needs SKSE plugin-loader hooks +
// messaging interface, the full library is overkill. These structs are the
// subset of SKSE's public API we actually use, transcribed from skse64's
// PluginAPI.h. Layout matches what SKSE expects — do not change without
// cross-referencing skse64 source.

#pragma once

#include <cstdint>

#define SKSE_API extern "C" __declspec(dllexport)
#define SKSE_API_VERSION_3 3
#define SKSE_VERSION_RUNTIME_RELEASE 0  // any runtime version

namespace SKSE
{
    // Layout matches skse64's SKSEPluginVersionData (PluginAPI.h).
    struct PluginVersionData
    {
        enum
        {
            kVersion = 1,
        };
        enum
        {
            kVersionIndependent_AddressLibraryPostAE = 1 << 0,
            kVersionIndependent_Signatures           = 1 << 1,
            kVersionIndependent_StructsPost629       = 1 << 2,
        };
        enum
        {
            kVersionIndependentEx_NoStructUse = 1 << 0,
        };

        std::uint32_t dataVersion;            // kVersion
        std::uint32_t pluginVersion;          // plugin's own version
        char          name[256];              // null-terminated
        char          author[256];            // null-terminated
        char          supportEmail[252];      // null-terminated
        std::uint32_t versionIndependenceEx;  // bitmask
        std::uint32_t versionIndependence;    // bitmask
        std::uint32_t compatibleVersions[16]; // 0-terminated list; 0 alone = any
        std::uint32_t seVersionRequired;      // minimum SKSE runtime (0 = any)
    };

    static_assert(sizeof(PluginVersionData) == 848, "PluginVersionData size mismatch");

    // ---------- LoadInterface ----------
    // Layout matches skse64's SKSEInterface: four UInt32 fields followed by
    // three function pointers. Do NOT change the field order or types.
    struct PluginInfo
    {
        std::uint32_t infoVersion;
        const char*   name;
        std::uint32_t version;
    };

    class LoadInterface
    {
    public:
        enum
        {
            kInvalid       = 0,
            kScaleform     = 1,
            kPapyrus       = 2,
            kSerialization = 3,
            kTask          = 4,
            kMessaging     = 5,
            kObject        = 6,
            kTrampoline    = 7,
        };

        std::uint32_t skseVersion;
        std::uint32_t runtimeVersion;
        std::uint32_t editorVersion;
        std::uint32_t isEditor;
        void*         (*QueryInterface)(std::uint32_t id);
        std::uint32_t (*GetPluginHandle)();
        const PluginInfo* (*GetPluginInfo)(const char* name);
    };

    // ---------- TaskInterface ----------
    // Used to dispatch work back onto Skyrim's main update / UI thread.
    // PrismaUI's Focus()/Invoke() expect main-thread invocation; calling them
    // from a worker thread silently misbehaves (queued but not properly
    // routed under some Wine + gamescope combinations).
    class TaskDelegate
    {
    public:
        virtual ~TaskDelegate() = default;
        virtual void Run() = 0;
        virtual void Dispose() = 0;  // SKSE calls this after Run returns.
    };

    class TaskInterface
    {
    public:
        std::uint32_t interfaceVersion;
        void (*AddTask)(TaskDelegate* task);
        void (*AddUITask)(TaskDelegate* task);
    };

    // ---------- MessagingInterface ----------
    class MessagingInterface
    {
    public:
        struct Message
        {
            const char*    sender;
            std::uint32_t  type;
            std::uint32_t  dataLen;
            void*          data;
        };

        using EventCallback = void(*)(Message* msg);

        enum
        {
            kPostLoad        = 0,
            kPostPostLoad    = 1,
            kPreLoadGame     = 2,
            kPostLoadGame    = 3,
            kSaveGame        = 4,
            kDeleteGame      = 5,
            kInputLoaded     = 6,
            kNewGame         = 7,
            kDataLoaded      = 8,
        };

        std::uint32_t interfaceVersion;
        bool (*RegisterListener)(std::uint32_t pluginHandle, const char* sender, EventCallback handler);
        bool (*Dispatch)(std::uint32_t pluginHandle, std::uint32_t messageType, void* data, std::uint32_t dataLen, const char* receiver);
        void* (*GetEventDispatcher)(std::uint32_t dispatcher);
    };
}
