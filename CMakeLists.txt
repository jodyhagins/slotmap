## ----------------------------------------------------------------------
## Copyright 2025 Jody Hagins
## Distributed under the MIT Software License
## See accompanying file LICENSE or copy at
## https://opensource.org/licenses/MIT
## ----------------------------------------------------------------------
cmake_minimum_required(VERSION 3.27)

project(wjh_slotmap
        VERSION 0.1.0
        DESCRIPTION "A slot map data structure library."
        LANGUAGES CXX)

list(APPEND CMAKE_PREFIX_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")
list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")

include(Setup)
if (CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    option(WJH_SLOTMAP_BUILD_TESTS "whether or not to build tests" ON)
    option(WJH_SLOTMAP_SANITIZE "whether or not to use address sanitizer" ON)
    option(WJH_SLOTMAP_BUILD_BENCHMARKS "whether or not to build benchmarks (Release only)" OFF)
endif ()

include(FetchContent)
include(CTest)
include(CompileOptions)
include(ThirdParty)

# Generate version header
configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/src/wjh/slotmap/version.hpp.in"
        "${CMAKE_CURRENT_BINARY_DIR}/include/wjh/slotmap/version.hpp"
        @ONLY
)

# Configuration files for find_package() support
# Only generate when building as top-level project
if (CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    configure_file(
            "${CMAKE_CURRENT_SOURCE_DIR}/cmake/wjh_slotmapVersion.cmake.in"
            "${CMAKE_CURRENT_BINARY_DIR}/cmake/wjh_slotmapVersion.cmake"
            @ONLY
    )
    configure_file(
            "${CMAKE_CURRENT_SOURCE_DIR}/cmake/wjh_slotmapConfig.cmake.in"
            "${CMAKE_CURRENT_BINARY_DIR}/cmake/wjh_slotmapConfig.cmake"
            COPYONLY
    )

    include(GNUInstallDirs)
    install(FILES
            "${CMAKE_CURRENT_BINARY_DIR}/cmake/wjh_slotmapVersion.cmake"
            "${CMAKE_CURRENT_BINARY_DIR}/cmake/wjh_slotmapConfig.cmake"
            DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/wjh_slotmap"
    )
endif()

add_subdirectory(src)
if (WJH_SLOTMAP_BUILD_TESTS)
    add_subdirectory(testing)
endif ()

if (WJH_SLOTMAP_BUILD_BENCHMARKS)
    if (NOT CMAKE_BUILD_TYPE STREQUAL "Release")
        message(WARNING "Benchmarks should be built in Release mode for meaningful results")
    endif ()
    add_subdirectory(benchmarking)
endif ()
