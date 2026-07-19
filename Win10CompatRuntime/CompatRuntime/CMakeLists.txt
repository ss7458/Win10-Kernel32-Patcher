# CompatRuntime.dll - Windows 10 Compatibility Runtime
cmake_minimum_required(VERSION 3.20)

file(GLOB THREAD_SRC Thread/*.cpp)
file(GLOB MEMORY_SRC Memory/*.cpp)
file(GLOB FILE_SRC File/*.cpp)
file(GLOB PROCESS_SRC Process/*.cpp)
file(GLOB TIME_SRC Time/*.cpp)
file(GLOB SYNC_SRC Sync/*.cpp)
file(GLOB DPI_SRC DPI/*.cpp)
file(GLOB DLL_SRC DLL/*.cpp)
file(GLOB ADVAPI_SRC AdvApi/*.cpp)
file(GLOB UTIL_SRC Utils/*.cpp)

add_library(CompatRuntime SHARED
    CompatRuntime.cpp
    CompatRuntime.def
    ${THREAD_SRC} ${MEMORY_SRC} ${FILE_SRC} ${PROCESS_SRC}
    ${TIME_SRC} ${SYNC_SRC} ${DPI_SRC} ${DLL_SRC} ${UTIL_SRC}
)

target_compile_definitions(CompatRuntime PRIVATE COMPATRUNTIME_EXPORTS)
set_target_properties(CompatRuntime PROPERTIES
    OUTPUT_NAME "CompatRuntime"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

install(TARGETS CompatRuntime RUNTIME DESTINATION bin LIBRARY DESTINATION bin)
