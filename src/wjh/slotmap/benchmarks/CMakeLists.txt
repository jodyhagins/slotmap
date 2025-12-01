## ----------------------------------------------------------------------
## Copyright 2025 Jody Hagins
## Distributed under the MIT Software License
## See accompanying file LICENSE or copy at
## https://opensource.org/licenses/MIT
## ----------------------------------------------------------------------

function(add_slotmap_benchmark)
    set(oneValueArgs TARGET)
    set(multiValueArgs SOURCES)
    cmake_parse_arguments(ARG "" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    add_executable(${ARG_TARGET} ${ARG_SOURCES})
    target_link_libraries(${ARG_TARGET} PRIVATE wjh::slotmap benchmarking)
    target_include_directories(${ARG_TARGET} PRIVATE "${PROJECT_SOURCE_DIR}")
    set_target_properties(${ARG_TARGET}
        PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
endfunction()

add_slotmap_benchmark(
    TARGET slotmap_bench
    SOURCES
        simple_bench.cpp
)
