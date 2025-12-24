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

# ABI versioning options (for binary distribution)
option(WJH_SLOTMAP_ENABLE_INLINE_NAMESPACE
    "Enable inline namespace for ABI versioning (useful for binary distribution)" OFF)
set(WJH_SLOTMAP_INLINE_NAMESPACE_NAME "v${PROJECT_VERSION_MAJOR}_${PROJECT_VERSION_MINOR}"
    CACHE STRING "Inline namespace name (only used when ENABLE_INLINE_NAMESPACE is ON)")

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
    include(CMakePackageConfigHelpers)
    include(GNUInstallDirs)

    # Generate version file with SameMajorVersion compatibility
    write_basic_package_version_file(
            "${CMAKE_CURRENT_BINARY_DIR}/cmake/wjh_slotmapConfigVersion.cmake"
            VERSION ${PROJECT_VERSION}
            COMPATIBILITY SameMajorVersion
    )

    # Generate config file with proper path handling
    configure_package_config_file(
            "${CMAKE_CURRENT_SOURCE_DIR}/cmake/wjh_slotmapConfig.cmake.in"
            "${CMAKE_CURRENT_BINARY_DIR}/cmake/wjh_slotmapConfig.cmake"
            INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/wjh_slotmap"
    )

    install(FILES
            "${CMAKE_CURRENT_BINARY_DIR}/cmake/wjh_slotmapConfigVersion.cmake"
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
