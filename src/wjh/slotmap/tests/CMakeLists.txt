## ----------------------------------------------------------------------
## Copyright 2025 Jody Hagins
## Distributed under the MIT Software License
## See accompanying file LICENSE or copy at
## https://opensource.org/licenses/MIT
## ----------------------------------------------------------------------
add_executable(slotmap_ut main.cpp
        Key_ut.cpp
        Slot_ut.cpp
        Slab_ut.cpp
        SlotMap_ut.cpp
)
target_link_libraries(slotmap_ut
        PRIVATE
        wjh::slotmap
        testing
)
target_include_directories(slotmap_ut
        PRIVATE
        "${PROJECT_SOURCE_DIR}")
set_target_properties(slotmap_ut
        PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
add_test(
        NAME "SlotMap Tests"
        COMMAND slotmap_ut)
