## ----------------------------------------------------------------------
## Copyright 2025 Jody Hagins
## Distributed under the MIT Software License
## See accompanying file LICENSE or copy at
## https://opensource.org/licenses/MIT
## ----------------------------------------------------------------------
add_library(testing INTERFACE)
target_include_directories(testing
        INTERFACE
        "${PROJECT_SOURCE_DIR}"
)
target_link_libraries(testing
    INTERFACE
        rapidcheck_doctest
        doctest
)
