## ----------------------------------------------------------------------
## Copyright 2025 Jody Hagins
## Distributed under the MIT Software License
## See accompanying file LICENSE or copy at
## https://opensource.org/licenses/MIT
## ----------------------------------------------------------------------
include(${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake)

function(add_slotmap_test)
    set(options USE_RAPIDCHECK USE_TEST_UTILS)
    set(oneValueArgs TARGET NAME PREFIX)
    set(multiValueArgs SOURCES)
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    add_executable(${ARG_TARGET} main.cpp ${ARG_SOURCES})
    target_link_libraries(${ARG_TARGET} PRIVATE wjh::slotmap testing)
    target_include_directories(${ARG_TARGET} PRIVATE "${PROJECT_SOURCE_DIR}")
    set_target_properties(${ARG_TARGET}
        PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")

    if(ARG_NAME)
        add_test(NAME ${ARG_NAME} COMMAND ${ARG_TARGET})
    elseif(ARG_PREFIX)
        doctest_discover_tests(${ARG_TARGET}
            TEST_PREFIX "${ARG_PREFIX} : "
            PROPERTIES COST 99)
    endif()
endfunction()

add_slotmap_test(
    TARGET key_ut
    NAME "SlotMap Key Tests"
    SOURCES
        Key_ut.cpp
)

add_slotmap_test(
    TARGET slot_ut
    NAME "SlotMap Slot Tests"
    SOURCES
        Slot_ut.cpp
        Slot_debug_ut.cpp
)

add_slotmap_test(
    TARGET slab_ut
    NAME "SlotMap Slab Tests"
    SOURCES
        Slab_ut.cpp
)

add_slotmap_test(
    TARGET slotmap_core_ut
    NAME "SlotMap Core"
    SOURCES
        SlotMap_ut.cpp
)

add_slotmap_test(
    TARGET slotmap_lifecycle_ut
    NAME "SlotMap Lifecycle"
    SOURCES
        SlotMap_lifecycle_ut.cpp
)

add_slotmap_test(
    TARGET slotmap_iteration_ut
    PREFIX "SlotMap Iteration"
    SOURCES
        SlotMap_iteration_ut.cpp
)

add_slotmap_test(
    TARGET slotmap_edge_cases_ut
    NAME "SlotMap Edge Cases"
    SOURCES
        SlotMap_edge_cases_ut.cpp
)
