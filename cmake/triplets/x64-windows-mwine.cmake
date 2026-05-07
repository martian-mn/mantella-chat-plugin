# Custom vcpkg triplet for cross-compiling Windows-target packages from Linux
# using msvc-wine. Points vcpkg at our wine-MSVC compiler so it builds
# packages with the correct Windows ABI even though we're not on Windows.

set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE static)
set(VCPKG_LIBRARY_LINKAGE static)

set(VCPKG_CMAKE_SYSTEM_NAME Windows)
set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/../msvc-wine-toolchain.cmake")

# Override the supports check — directxtk/spdlog list "windows" as supported,
# and we are targeting Windows, even though our host platform is Linux.
set(VCPKG_PLATFORM_TOOLSET v143)
