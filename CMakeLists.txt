cmake_minimum_required(VERSION 3.21)

# Top-level build for the MantellaChat SKSE plugin.
# Cross-compiled from Linux via the msvc-wine Docker image (see Dockerfile.build
# and CHAT_PLUGIN_DESIGN.md for the full setup). vcpkg is bootstrapped inside
# the container at /opt/vcpkg and supplies CommonLibSSE-NG's dependencies
# (spdlog, directxtk, rapidcsv) when the toolchain file is set.

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

target_include_directories(${PROJECT_NAME} PRIVATE src)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _WIN32_WINNT=0x0A00
    CPPHTTPLIB_NO_EXCEPTIONS
)

# Fully static CRT so users don't need a separate VC++ Redistributable
# installed in their Skyrim Wine prefix. Pairs with vcpkg's
# x64-windows-static triplet (set in the Actions workflow), which builds
# spdlog/fmt/directxtk against /MT as well — no symbol mismatch at link.
if(MSVC)
    set_property(TARGET ${PROJECT_NAME} PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE
    CommonLibSSE::CommonLibSSE
    ws2_32
    crypt32
    user32
    advapi32
)
