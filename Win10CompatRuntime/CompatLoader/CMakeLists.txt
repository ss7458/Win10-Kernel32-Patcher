# CompatLoader - Runtime loader / injector
#
# Builds CompatLoader.exe, which launches a target EXE in a suspended state,
# injects CompatRuntime.dll via a LoadLibraryA remote thread, and resumes the
# target. CompatRuntime.dll's DllMain detects the COMPAT_LOADER environment
# flag and rewrites the target's IAT to redirect kernel32 / api-ms imports to
# the compatibility shims.

cmake_minimum_required(VERSION 3.20)

add_executable(CompatLoader main.cpp)

target_link_libraries(CompatLoader PRIVATE kernel32.lib)

set_target_properties(CompatLoader PROPERTIES
    OUTPUT_NAME "CompatLoader"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
