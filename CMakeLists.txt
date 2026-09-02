# iphone-1 — booting the genuine iPhone OS 1.0 (1A543a) on an emulated iPhone1,1.
cmake_minimum_required(VERSION 3.16)
project(iphone1 C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

enable_testing()
add_subdirectory(core)
option(IPHONE1_WEB "Build the Emscripten browser frontend" OFF)
if(IPHONE1_WEB)
    add_executable(emu web/emu_api.c)
    target_link_libraries(emu PRIVATE emucore)
    target_compile_options(emu PRIVATE -O2)
    target_link_options(emu PRIVATE
        -O2
        -sALLOW_MEMORY_GROWTH=1
        -sINITIAL_MEMORY=536870912
        -sMODULARIZE=1
        -sEXPORT_ES6=0
        -sEXPORT_NAME=createEmu
        "-sEXPORTED_FUNCTIONS=['_emu_init','_emu_error','_emu_load_snapshot','_emu_run','_emu_status','_emu_frame','_emu_touch','_emu_button','_emu_cycles','_emu_serial_read','_malloc','_free']"
        "-sEXPORTED_RUNTIME_METHODS=['ccall','cwrap','HEAPU8','HEAPU32']"
    )
    set_target_properties(emu PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/web/dist"
    )
else()
    add_subdirectory(tools)
endif()
