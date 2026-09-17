cmake_minimum_required(VERSION 3.20)
project(CANopen_IO_Node VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

include(CTest)
add_subdirectory(canopen)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
