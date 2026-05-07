-- Mantella in-game chat plugin — build config
set_xmakever("2.8.2")

includes("lib/commonlibsse-ng")

set_project("MantellaChat")
set_version("0.1.0")
set_license("GPL-3.0")

set_languages("c++23")
set_warnings("allextra")

set_policy("package.requires_lock", true)

add_rules("mode.release")
add_rules("plugin.vsxmake.autoupdate")

target("MantellaChat")
    add_deps("commonlibsse-ng")

    add_rules("commonlibsse-ng.plugin", {
       name = "MantellaChat",
       author = "Mantella text-only fork",
       description = "In-game chat overlay for the Mantella text-only fork (PrismaUI)"
    })

    add_files("src/**.cpp")
    add_headerfiles("src/**.h")
    add_includedirs("src")
    set_pcxxheader("src/pch.h")

    -- cpp-httplib needs ws2_32 + crypt32 on Windows for client TCP
    if is_plat("windows", "mingw") then
        add_syslinks("ws2_32", "crypt32")
    end
