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
        FILE_SET HEADERS
        BASE_DIRS "${PROJECT_SOURCE_DIR}/src"
        FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/Key.hpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/SlotMap.hpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/detail.hpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/detail/Slot.hpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/detail/Slab.hpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/detail/SlotMap.hpp"
)

target_include_directories(wjh_slotmap
        INTERFACE
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>
        $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(wjh_slotmap
        INTERFACE
        cxx_std_20
)

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

# Installation support for find_package() integration
include(GNUInstallDirs)

install(TARGETS wjh_slotmap
        EXPORT wjh_slotmapTargets
        FILE_SET HEADERS
)

# Install umbrella header separately (it's in parent directory)
install(FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/../slotmap.hpp"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/wjh"
)

install(EXPORT wjh_slotmapTargets
        FILE wjh_slotmapTargets.cmake
        NAMESPACE wjh::
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/wjh_slotmap"
)
