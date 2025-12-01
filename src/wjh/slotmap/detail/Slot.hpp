// ----------------------------------------------------------------------
// Copyright 2025 Jody Hagins
// Distributed under the MIT Software License
// See accompanying file LICENSE or copy at
// https://opensource.org/licenses/MIT
// ----------------------------------------------------------------------
#ifndef WJH_SLOTMAP_210A58D772C142B58C2FDB99480EA93F
#define WJH_SLOTMAP_210A58D772C142B58C2FDB99480EA93F

#include <array>
#include <cassert>
#include <cstddef>
#include <cstring>
#include <limits>
#include <new>
#include <type_traits>
#include <utility>

namespace wjh::slotmap::detail {

/**
 * Check if a version type has spare bits that can be used for an alive bit.
 *
 * @tparam VersionT A type with static members num_bits and value_type.
 * @return true if num_bits < std::numeric_limits<value_type>::digits
 */
template <typename VersionT>
constexpr bool
has_alive_bit()
{
    constexpr auto version_digits =
        std::numeric_limits<typename VersionT::value_type>::digits;
    return VersionT::num_bits < version_digits;
}

/**
 * A slot in the slot map, containing either a value (when alive) or
 * a free-list link (when free).
 *
 * @tparam T The value type stored in the slot
 * @tparam IndexT The index type for free-list linking
 * @tparam VersionT The version type for ABA protection
 */
template <typename T, typename IndexT, typename VersionT>
class Slot
{
public:
    using value_type = T;
    using index_type = IndexT;
    using version_type = VersionT;

    /**
     * Default constructor is trivial, and DOES NOTHING.
     *
     * Value-initialization (i.e., Slot() or Slot{}) will zero-initialize.
     */
    constexpr Slot() = default;

    /**
     * Construct, setting the next index in the free list.
     *
     * @post  This instance will be FREE, with a version of 0 and the linking
     * index to the next free node in the list will be @p next.
     */
    constexpr explicit Slot(index_type next);

    // Non-copyable, non-movable (managed by Slab)
    Slot(Slot const &) = delete;
    Slot & operator = (Slot const &) = delete;
    Slot(Slot &&) = delete;
    Slot & operator = (Slot &&) = delete;

    ~Slot() = default;

    // ========================================================================
    // Version access (always valid)
    // ========================================================================

    [[nodiscard]]
    constexpr version_type version() const noexcept;

    constexpr void set_version(version_type v) noexcept;

    // ========================================================================
    // Free-list access (only valid when FREE)
    // ========================================================================

    [[nodiscard]]
    constexpr index_type next() const noexcept;

    constexpr void set_next(index_type i) noexcept;

    // ========================================================================
    // Value access (only valid when ALIVE)
    // ========================================================================

    /**
     * Construct a value in-place
     *
     * @pre Slot must be in FREE state
     * @post Slot is in ALIVE state
     */
    template <typename... Args>
    constexpr T & emplace(Args &&... args);

    /**
     * Destroy the stored value
     *
     * @pre Slot must be in ALIVE state
     * @post Slot is in FREE state (next field may contain garbage)
     */
    constexpr void destroy() noexcept(std::is_nothrow_destructible_v<T>);

    /**
     * Access the stored value
     *
     * @pre Slot must be in ALIVE state
     */
    [[nodiscard]]
    constexpr T & value() noexcept;

    [[nodiscard]]
    constexpr T const & value() const noexcept;

    // ========================================================================
    // Alive bit support
    // ========================================================================

    /**
     * Whether this slot type has an embedded alive bit.
     *
     * When true, is_alive() can be used to check slot status without
     * accessing the slab's bitmap, improving cache locality.
     */
    static constexpr bool has_embedded_alive_bit =
        detail::has_alive_bit<version_type>();

    /**
     * Check if the slot is alive (has a constructed value).
     *
     * @note When has_embedded_alive_bit is true, this reads from the
     *       version_bytes_ field (same cache line as the slot). When false,
     *       it always returns true and the slab's bitmap must be consulted.
     */
    [[nodiscard]]
    constexpr bool is_alive() const noexcept;

private:
    // Storage for either index_type or T
    alignas(std::max(alignof(index_type), alignof(T)))
        std::array<std::byte, std::max(sizeof(index_type), sizeof(T))> storage_;

    // Version stored as byte array to avoid padding issues between
    // the union and the version field
    std::array<std::byte, sizeof(version_type)> version_bytes_;

    template <typename U>
    constexpr U * get();
    template <typename U>
    constexpr U const * get() const;

    constexpr void set_free();
    constexpr bool is_free() const;
    constexpr void set_alive();

    using naked_version_type = typename version_type::value_type;
    static constexpr auto version_digits =
        std::numeric_limits<naked_version_type>::digits;
    static constexpr naked_version_type alive_bit = naked_version_type(1)
        << (version_digits - 1);
};

} // namespace wjh::slotmap::detail

#include "Slot.ipp"

#endif // WJH_SLOTMAP_210A58D772C142B58C2FDB99480EA93F
