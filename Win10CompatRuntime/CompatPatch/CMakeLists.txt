# CompatPatch.exe - PE Import Table Patching Tool
cmake_minimum_required(VERSION 3.20)

add_executable(CompatPatch
    main.cpp
    PEUtils.cpp
    ImportScanner.cpp
    ImportRebuilder.cpp
)

target_compile_definitions(CompatPatch PRIVATE _CRT_SECURE_NO_WARNINGS)
set_target_properties(CompatPatch PROPERTIES
    OUTPUT_NAME "CompatPatch"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

install(TARGETS CompatPatch RUNTIME DESTINATION bin)
