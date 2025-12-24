## ----------------------------------------------------------------------
## Copyright 2025 Jody Hagins
## Distributed under the MIT Software License
## See accompanying file LICENSE or copy at
## https://opensource.org/licenses/MIT
## ----------------------------------------------------------------------

include(CheckCXXCompilerFlag)

function(add_cxx_compile_options)
    set(COMMON_WARNINGS
            -Wall                # Enables most warnings.
            -Wextra              # Enables an extra set of warnings.
            -pedantic            # Strict compliance to the standard is not met.
            -Wcast-align         # Pointer casts which increase alignment.
            -Wcast-qual          # A pointer cast to unsafe qualifier
            -Wconversion         # Implicit conversions that may change a value.
            -Wformat=2           # printf/scanf/strftime/strfmon format string
            -Wnon-virtual-dtor   # Non-virtual destructors are found.
            -Wold-style-cast     # C-style cast is used in a program.
            -Woverloaded-virtual # Overloaded virtual function names.
            -Wsign-conversion    # Implicit conversions between signed and unsigned
            -Wshadow             # One variable shadows another.
            -Wswitch-enum        # SA switch statement has an index of enumerated type and lacks a case.
            -Wundef              # An undefined identifier is evaluated in an #if directive.
            -Wunused             # Enable all -Wunused- warnings.
    )
    set(COMMON_NO_WARNINGS)

    set(GCC_WARNINGS
            -Wdisabled-optimization     # GCC's optimizers are unable to handle the code effectively.
            -Wlogical-op                # Warn when a logical operator is always evaluating to true or false.
            -Wsign-promo                # Overload resolution chooses a promotion from unsigned to a signed type.
            -Wredundant-decls           # Something is declared more than once in the same scope.
            -Wdouble-promotion          # Warn about implicit conversions from "float" to "double".
            -Wdate-time                 # Warn when encountering macros that might prevent bit-wise-identical compilations.
            -Wsuggest-final-methods     # Virtual methods that could be declared final or in an anonymous namespace.
            -Wsuggest-final-types       # Types with virtual methods that can be declared final or in an anonymous namespace.
            -Wsuggest-override          # Overriding virtual functions that are not marked with the override keyword.
            -Wduplicated-cond           # Warn about duplicated conditions in an if-else-if chain.
            -Wmisleading-indentation    # Warn when indentation does not reflect the block structure.
            -Wnull-dereference          # Dereferencing a pointer may lead to undefined behavior.
            -Wduplicated-branches       # Warn about duplicated branches in if-else statements.
            -Wextra-semi                # Redundant semicolons after in-class function definitions.
            -Wunsafe-loop-optimizations # The loop cannot be optimized because the compiler cannot assume anything.
            -Warith-conversion          # Stricter implicit conversion warnings in arithmetic operations.
            -Wredundant-tags            # Redundant class-key and enum-key where it can be eliminated.
    )
    set(GCC_NO_WARNINGS
            -Wno-multiple-inheritance      # Do not allow multiple inheritance.
            -Wno-switch-default         # A switch statement does not have a default case.  This conflicts with the option that checks for all enums.
            -Wno-useless-cast              # Warn about useless casts.
    )

    set(CLANG_WARNINGS
            -Weverything               # Enables every Clang warning.
    )
    set(CLANG_NO_WARNINGS
            -Wno-c++20-compat          # Without this, we can't use code that assumes at least C++20, like implicit typename.
            -Wno-c++98-compat          # Duh.
            -Wno-c++98-compat-pedantic # Duh.
            -Wno-padded                # We only care about this in explicit cases.
            -Wno-dollar-in-identifier-extension
            -Wno-comma                 # I don't want to disable this one, but the compiler issues warning when the comma operator is used within a decltype, where we know we only want the last result.
            -Wno-switch-default        # Interferes with other warnings, and when all cases of an enum are handled, a warning is generated saying that there is no default. But, if you add a default, a warning is generated saying that there is a default when all the cases are handled.
            -Wno-unsafe-buffer-usage   # False positives on legitimate pointer arithmetic in Slab memory layout code.
    )

    set(WARNINGS ${COMMON_WARNINGS})
    if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        list(APPEND WARNINGS ${GCC_WARNINGS} ${GCC_NO_WARNINGS})
    elseif (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        list(APPEND WARNINGS ${CLANG_WARNINGS} ${CLANG_NO_WARNINGS})
        if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
            list(APPEND WARNINGS "-Wno-poison-system-directories")
        endif ()
    endif ()

    list(APPEND WARNINGS ${COMMON_NO_WARNINGS})
    LIST(REMOVE_DUPLICATES WARNINGS)
    add_compile_options(-Werror ${WARNINGS})

    if (WJH_SLOTMAP_SANITIZE)
        add_compile_options(-fsanitize=address -g)
        add_link_options(-fsanitize=address)
    endif ()

endfunction()
