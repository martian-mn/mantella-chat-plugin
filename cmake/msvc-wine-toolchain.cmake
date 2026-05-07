# CMake toolchain file targeting Windows x64 via msvc-wine.
# Used both as the top-level toolchain (when chained with vcpkg.cmake) and
# as VCPKG_CHAINLOAD_TOOLCHAIN_FILE so vcpkg's package builds use the same
# wine-MSVC compiler.

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR AMD64)

# Paths inside the msvc-wine docker image (/opt/msvc/...).
# CMAKE_PROGRAM_PATH ensures cmake's compiler-finding logic picks up our
# wine wrapper scripts (which are bash, not Windows .exe — cmake handles this).
list(PREPEND CMAKE_PROGRAM_PATH "/opt/msvc/bin/x64")

set(CMAKE_C_COMPILER cl)
set(CMAKE_CXX_COMPILER cl)
set(CMAKE_RC_COMPILER rc)
set(CMAKE_LINKER link)
set(CMAKE_AR lib)

# Skip cmake's compiler test program — without winbind running in vcpkg's
# child build, the test would fail with the PDB-manager-mismatch error
# even though release-mode compilation works fine.
set(CMAKE_C_COMPILER_WORKS TRUE)
set(CMAKE_CXX_COMPILER_WORKS TRUE)

# Search for libraries / headers only in target paths.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
