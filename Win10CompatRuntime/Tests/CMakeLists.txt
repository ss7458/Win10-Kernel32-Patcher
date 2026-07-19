# Tests - CompatRuntime API validation
cmake_minimum_required(VERSION 3.20)

add_executable(CompatTests test_runtime.cpp)
target_link_libraries(CompatTests PRIVATE CompatRuntime)
set_target_properties(CompatTests PROPERTIES
    OUTPUT_NAME "CompatTests"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
