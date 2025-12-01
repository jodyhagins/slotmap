## ----------------------------------------------------------------------
## Copyright 2025 Jody Hagins
## Distributed under the MIT Software License
## See accompanying file LICENSE or copy at
## https://opensource.org/licenses/MIT
## ----------------------------------------------------------------------

add_library(wjh_slotmap INTERFACE)
add_library(wjh::slotmap ALIAS wjh_slotmap)

target_sources(wjh_slotmap
        INTERFACE
        Key.hpp
)

target_include_directories(wjh_slotmap
        INTERFACE
        "${PROJECT_SOURCE_DIR}/src")

# Define WJH_SLOTMAP_DEBUG_MODE for Debug builds using generator expression
# This works correctly with both single-config (Make, Ninja) and multi-config
# (Xcode, Visual Studio) generators
target_compile_definitions(wjh_slotmap
        INTERFACE
        $<$<CONFIG:Debug>:WJH_SLOTMAP_DEBUG_MODE>)

if (WJH_SLOTMAP_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif ()

if (WJH_SLOTMAP_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif ()
