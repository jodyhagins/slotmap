## ----------------------------------------------------------------------
## Copyright 2025 Jody Hagins
## Distributed under the MIT Software License
## See accompanying file LICENSE or copy at
## https://opensource.org/licenses/MIT
## ----------------------------------------------------------------------
cmake_minimum_required(VERSION 3.27)

project(wjh_slotmap
        VERSION 1.0
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
