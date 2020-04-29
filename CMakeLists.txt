cmake_minimum_required(VERSION 3.7.2)
project(x4dyl)

set(CMAKE_CXX_STANDARD 14)

include_directories(include)
include_directories(src/hal)
include_directories(src/Radar)
include_directories(src/wiringPi)
include_directories(src/XDriver)

set(SOURCE_FILES
        include/xep_hal.h
        include/xtcompiler.h
        src/hal/radar_hal.c
        src/Radar/taskRadar.cpp
        src/wiringPi/src/wiringPiSPI.c
        src/XDriver/x4driver.c
        src/main.cpp)

LINK_LIBRARIES(${src/wiringPi/lib/}libwiringPi.so)
LINK_LIBRARIES(${src/wiringPi/lib/}libwiringPiDev.so)
LINK_LIBRARIES(-lpthread)

add_executable(x4dyl ${SOURCE_FILES})
