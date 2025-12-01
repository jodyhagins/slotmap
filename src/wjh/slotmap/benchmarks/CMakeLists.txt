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

# Micro-benchmarks: Core operations (insert, lookup, erase, iterate)
add_slotmap_benchmark(
    TARGET slotmap_micro_bench
    SOURCES
        micro_bench.cpp
)

# Macro-benchmarks: Realistic workloads (game lifecycle, static lookup, etc.)
add_slotmap_benchmark(
    TARGET slotmap_macro_bench
    SOURCES
        macro_bench.cpp
)

# Combined benchmark executable for convenience
add_slotmap_benchmark(
    TARGET slotmap_all_bench
    SOURCES
        micro_bench.cpp
        macro_bench.cpp
)

# Sized iteration benchmark - uses appropriately-sized keys
add_slotmap_benchmark(
    TARGET slotmap_sized_bench
    SOURCES
        sized_iteration_bench.cpp
)
