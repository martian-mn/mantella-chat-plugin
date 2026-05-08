cmake_minimum_required(VERSION 3.21)

# Top-level build for the MantellaChat SKSE plugin.
#
# Builds via GitHub Actions on a windows-latest runner with Visual Studio
# 2022 + vcpkg manifest mode. CommonLibSSE-NG depends on DirectXTK (Microsoft-
# only), so cross-compiling from Linux isn't viable — see CHAT_PLUGIN_DESIGN.md
# for the migration history.

project(MantellaChat
    LANGUAGES CXX
    VERSION 0.1.0
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Static-link the CRT for everything in this build, including the
# CommonLibSSE-NG subdirectory. CMP0091=NEW makes MSVC_RUNTIME_LIBRARY the
# canonical knob (instead of CMAKE_CXX_FLAGS_RELEASE manipulations).
cmake_policy(SET CMP0091 NEW)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    CACHE STRING "Static CRT for everything in this build" FORCE)

# CommonLibSSE-NG. Disable its built-in tests and the runtimes we don't
# target. We're running on Skyrim AE 1.6.x, but leaving SE+VR enabled keeps
# the plugin's address-library lookup tolerant of saves migrated across
# runtimes.
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_VR OFF CACHE BOOL "" FORCE)
add_subdirectory(lib/commonlibsse-ng)

add_library(${PROJECT_NAME} SHARED
    src/main.cpp
    src/InputHandler.cpp
)

# WIN32_LEAN_AND_MEAN/NOMINMAX trim windows.h. _WIN32_WINNT comes from
# CommonLibSSE-NG's PUBLIC compile_definitions (0x0601 — Windows 7, the
# baseline Skyrim SE supports), so we don't redefine it here.
target_compile_definitions(${PROJECT_NAME} PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    CPPHTTPLIB_NO_EXCEPTIONS
)

# Per-target runtime-library setting was redundant once we set
# CMAKE_MSVC_RUNTIME_LIBRARY globally above (and CMake honours that on every
# target via CMP0091=NEW). Keeping the comment as a breadcrumb.
#
# ws2_32 is the only Windows SDK library we link directly — httplib uses it
# for socket calls. CommonLibSSE-NG transitively brings in the rest of what
# its own implementation needs (kernel32, user32, etc.).
target_link_libraries(${PROJECT_NAME} PRIVATE
    CommonLibSSE::CommonLibSSE
    ws2_32
)
