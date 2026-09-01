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
add_subdirectory(tools)
