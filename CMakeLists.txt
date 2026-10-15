cmake_minimum_required(VERSION 3.20)
project(driverse LANGUAGES CXX VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# Warnings: default on, opt out with -DDRIVERSE_WERROR=OFF
option(DRIVERSE_WERROR "Treat warnings as errors" OFF)
if(MSVC)
    add_compile_options(/W4)
    if(DRIVERSE_WERROR)
        add_compile_options(/WX)
    endif()
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
    if(DRIVERSE_WERROR)
        add_compile_options(-Werror)
    endif()
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Concurrent)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

include_directories(${CMAKE_SOURCE_DIR}/src)

# ---- Core library (no GUI dependency) ----
add_library(driverse_core STATIC
    src/core/types.cpp
    src/core/utils.cpp
    src/core/protections.cpp
    src/core/scanner.cpp
    src/core/recommendations.cpp
    src/platform/mounts.cpp
    src/utils/config.cpp
    src/utils/audit.cpp
    src/utils/integration.cpp
    src/utils/logger.cpp
)
target_include_directories(driverse_core PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(driverse_core PUBLIC Qt6::Core)

# ---- Application ----
add_executable(driverse
    src/main.cpp
    src/gui/mainwindow.cpp
    src/gui/mainwindow.h
    src/gui/deviceTableModel.cpp
    src/gui/deviceTableModel.h
    src/gui/storageChart.cpp
    src/gui/storageChart.h
    src/gui/summaryCard.cpp
    src/gui/summaryCard.h
    src/gui/scanworker.cpp
    src/gui/scanworker.h
)
target_link_libraries(driverse PRIVATE
    driverse_core
    Qt6::Widgets
    Qt6::Concurrent
)

install(TARGETS driverse DESTINATION bin)

# ---- Tests ----
option(DRIVERSE_BUILD_TESTS "Build tests" ON)
if(DRIVERSE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
