// ----------------------------------------------------------------------
// Copyright 2025 Jody Hagins
// Distributed under the MIT Software License
// See accompanying file LICENSE or copy at
// https://opensource.org/licenses/MIT
// ----------------------------------------------------------------------
#ifndef WJH_SLOTMAP_66B914388D2346199DB41E7B25FBD115
#define WJH_SLOTMAP_66B914388D2346199DB41E7B25FBD115

/**
 * @file abi.hpp
 *
 * @brief ABI versioning configuration for wjh_slotmap library.
 *
 * This header controls optional inline namespace versioning, which helps
 * prevent ODR violations and ABI mismatches when different versions of
 * the library are used in the same program.
 *
 * ## Configuration Options
 *
 * These macros can be defined before including any slotmap headers, or
 * configured via CMake. If editing this file directly for distribution
 * builds, users will be forced to compile with the same configuration.
 *
 * ### WJH_SLOTMAP_USE_INLINE_NAMESPACE
 *
 * Set to 1 to enable inline namespace versioning. When enabled, all
 * symbols in `wjh::slotmap` will be wrapped in an inline namespace,
 * causing mangled symbol names to include the version identifier.
 *
 * Default: 0 (disabled)
 *
 * ### WJH_SLOTMAP_INLINE_NAMESPACE_NAME
 *
 * The name of the inline namespace. Must be a valid C++ identifier.
 * Only used when WJH_SLOTMAP_USE_INLINE_NAMESPACE is 1.
 *
 * Recommended naming conventions:
 * - Version-based: v0, v1, v2
 * - Date-based: lts_20250101
 * - SemVer-based: v0_1, v1_0
 *
 * Default: v0
 *
 * ## Usage Examples
 *
 * ### Default (no inline namespace):
 * @code
 * #include <wjh/slotmap.hpp>
 * // Symbols: wjh::slotmap::Key, wjh::slotmap::SlotMap, etc.
 * @endcode
 *
 * ### With inline namespace enabled:
 * @code
 * #define WJH_SLOTMAP_USE_INLINE_NAMESPACE 1
 * #define WJH_SLOTMAP_INLINE_NAMESPACE_NAME v1
 * #include <wjh/slotmap.hpp>
 * // Symbols: wjh::slotmap::v1::Key (but wjh::slotmap::Key also works)
 * // Mangled names include v1, preventing silent ABI mismatches
 * @endcode
 *
 * ### Via CMake:
 * @code{.cmake}
 * cmake -DWJH_SLOTMAP_ENABLE_INLINE_NAMESPACE=ON \
 *       -DWJH_SLOTMAP_INLINE_NAMESPACE_NAME=v1 ..
 * @endcode
 *
 * ## Guidelines
 *
 * - Users should NOT name the inline namespace in their code. Write
 *   `wjh::slotmap::Key`, not `wjh::slotmap::v1::Key`. This allows
 *   transparent upgrades when recompiling.
 *
 * - Only spell the inline namespace name if you need to pin a specific
 *   version for binary compatibility reasons.
 *
 * - Forward declarations of library types are not supported when inline
 *   namespaces are enabled (this is a known limitation of the approach).
 *   Forward declarations of library types outside the library implementation
 *   are evil anyway.
 */

// ============================================================================
// User-configurable options
// ============================================================================

/**
 * Set to 1 to enable inline namespace versioning.
 *
 * When enabled, all public symbols will be placed inside an inline namespace.
 * This affects mangled symbol names, helping catch ABI mismatches at link time
 * instead of experiencing undefined behavior at runtime.
 */
#ifndef WJH_SLOTMAP_USE_INLINE_NAMESPACE
    #define WJH_SLOTMAP_USE_INLINE_NAMESPACE 0
#endif

/**
 * The inline namespace name to use when versioning is enabled.
 *
 * Must be a valid C++ identifier. This value is ignored when
 * WJH_SLOTMAP_USE_INLINE_NAMESPACE is 0.
 */
#ifndef WJH_SLOTMAP_INLINE_NAMESPACE_NAME
    #define WJH_SLOTMAP_INLINE_NAMESPACE_NAME v0
#endif

// ============================================================================
// Implementation macros (do not modify)
// ============================================================================

#if WJH_SLOTMAP_USE_INLINE_NAMESPACE

    /**
     * Opens the versioned namespace scope.
     *
     * Usage:
     * @code
     * namespace wjh::slotmap {
     * WJH_SLOTMAP_NAMESPACE_BEGIN
     *
     * class Key { ... };
     *
     * WJH_SLOTMAP_NAMESPACE_END
     * } // namespace wjh::slotmap
     * @endcode
     */
    #define WJH_SLOTMAP_NAMESPACE_BEGIN \
        inline namespace WJH_SLOTMAP_INLINE_NAMESPACE_NAME {

    /**
     * Closes the versioned namespace scope.
     */
    #define WJH_SLOTMAP_NAMESPACE_END }

#else // WJH_SLOTMAP_USE_INLINE_NAMESPACE == 0

    #define WJH_SLOTMAP_NAMESPACE_BEGIN
    #define WJH_SLOTMAP_NAMESPACE_END

#endif // WJH_SLOTMAP_USE_INLINE_NAMESPACE

#endif // WJH_SLOTMAP_66B914388D2346199DB41E7B25FBD115
