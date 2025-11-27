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

if (WJH_SLOTMAP_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif ()
