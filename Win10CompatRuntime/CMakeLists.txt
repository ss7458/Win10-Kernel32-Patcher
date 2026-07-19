# Win10CompatRuntime - Windows 10 Compatibility Runtime Framework
# Root CMake build configuration
cmake_minimum_required(VERSION 3.20)
project(Win10CompatRuntime VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/W3 /utf-8)
    add_definitions(-D_UNICODE -DUNICODE)
endif()

# Subdirectories
add_subdirectory(CompatRuntime)
add_subdirectory(CompatPatch)
add_subdirectory(CompatLoader)
add_subdirectory(Tests)

# Install database files alongside binaries
install(DIRECTORY Database DESTINATION .)
