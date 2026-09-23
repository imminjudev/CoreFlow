cmake_minimum_required(VERSION 3.20)

project(CoreFlow LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(CoreFlow
    src/main.cpp
    src/runtime/Task.hpp
    src/core/CoreSpinLock.hpp
)

target_include_directories(CoreFlow PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

if(MSVC)
    target_compile_options(CoreFlow PRIVATE /utf-8)
endif()