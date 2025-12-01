## ----------------------------------------------------------------------
## Copyright 2025 Jody Hagins
## Distributed under the MIT Software License
## See accompanying file LICENSE or copy at
## https://opensource.org/licenses/MIT
## ----------------------------------------------------------------------
add_library(benchmarking INTERFACE)
target_include_directories(benchmarking
        INTERFACE
        "${PROJECT_SOURCE_DIR}"
)
target_link_libraries(benchmarking
    INTERFACE
        benchmark::benchmark
        benchmark::benchmark_main
)
