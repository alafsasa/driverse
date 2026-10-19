cmake_minimum_required(VERSION 3.20)
project(driverse LANGUAGES CXX VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    # Optimized by default: scanning/classification is string- and syscall-
    # heavy and is an order of magnitude slower at -O0. RelWithDebInfo keeps
    # symbols for profiling.
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
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
    # The test suites use plain assert(): keep assertions enabled even in
    # optimized configurations (otherwise -DNDEBUG from RelWithDebInfo/Release
    # would silently turn them into no-ops).
    add_compile_options(-UNDEBUG)
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
    src/core/cleanup.cpp
    src/platform/mounts.cpp
    src/platform/blockdev.cpp
    src/platform/packages.cpp
    src/platform/trash.cpp
    src/utils/config.cpp
    src/utils/audit.cpp
    src/utils/cleanupaudit.cpp
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
    src/gui/scanmodel.cpp
    src/gui/scanmodel.h
    src/gui/scandata.h
    src/gui/cleanupdialog.cpp
    src/gui/cleanupdialog.h
    src/gui/skippedpathsdialog.cpp
    src/gui/skippedpathsdialog.h
    src/gui/appsdialog.cpp
    src/gui/appsdialog.h
    src/gui/formatting.h
)
target_link_libraries(driverse PRIVATE
    driverse_core
    Qt6::Widgets
    Qt6::Concurrent
)

install(TARGETS driverse DESTINATION bin)
install(FILES data/driverse.desktop DESTINATION share/applications)
install(FILES data/driverse.svg DESTINATION share/icons/hicolor/scalable/apps)
install(FILES LICENSE DESTINATION share/doc/driverse)

# ---- Packaging (optional; `cpack` in the build dir produces a tarball) ----
set(CPACK_PACKAGE_NAME "driverse")
set(CPACK_PACKAGE_VENDOR "Driverse")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Safe, conservative Linux storage cleaner")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_GENERATOR "TGZ")
set(CPACK_RESOURCE_FILE_LICENSE ${CMAKE_CURRENT_SOURCE_DIR}/LICENSE)
include(CPack)

# ---- Tests ----
option(DRIVERSE_BUILD_TESTS "Build tests" ON)
if(DRIVERSE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
