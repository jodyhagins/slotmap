## ----------------------------------------------------------------------
## Copyright 2025 Jody Hagins
## Distributed under the MIT Software License
## See accompanying file LICENSE or copy at
## https://opensource.org/licenses/MIT
## ----------------------------------------------------------------------

if (WJH_SLOTMAP_BUILD_TESTS)
    message(STATUS "Processing third-party DocTest...")
    FetchContent_Declare(
            DocTest
            GIT_REPOSITORY https://github.com/jodyhagins/doctest.git
            GIT_TAG dev
            SYSTEM
    )
    FetchContent_MakeAvailable(DocTest)

    message(STATUS "Processing third-party RapidCheck...")
    set(RC_ENABLE_DOCTEST ON)
    FetchContent_Declare(
            rapidcheck
            GIT_REPOSITORY https://github.com/jodyhagins/rapidcheck.git
            GIT_TAG wjh-master
            SYSTEM
    )
    FetchContent_MakeAvailable(rapidcheck)
endif ()

if (WJH_SLOTMAP_BUILD_BENCHMARKS)
    message(STATUS "Processing third-party Google Benchmark...")
    set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
            benchmark
            GIT_REPOSITORY https://github.com/google/benchmark.git
            GIT_TAG v1.9.1
            SYSTEM
    )
    FetchContent_MakeAvailable(benchmark)
endif ()
