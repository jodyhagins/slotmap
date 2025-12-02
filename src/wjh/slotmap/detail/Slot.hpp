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

template <
    typename ValueT,
    typename IndexT,
    typename VersionT,
    bool AllowAliveBit>
struct SlotTraits
{
    using value_type = ValueT;
    using index_type = IndexT;
    using version_type = VersionT;
    static constexpr bool allow_alive_bit = AllowAliveBit;
};

template <typename T>
constexpr bool
has_alive_bit()
{
    using VersionT = typename T::version_type;
    constexpr auto version_digits =
        std::numeric_limits<typename VersionT::value_type>::digits;
    bool const has_room = VersionT::num_bits < version_digits;
#if defined(WJH_SLOTMAP_DEBUG_MODE)
    return has_room;
#else
    return has_room && T::allow_alive_bit;
#endif
}

/**
 * A slot in the slot map, containing either a value (when alive) or
 * a free-list link (when free).
 *
 * @tparam TraitsT  The set of traits used to instantiate this Slot.
 */
template <typename TraitsT>
class Slot
{
public:
    using value_type = typename TraitsT::value_type;
    using index_type = typename TraitsT::index_type;
    using version_type = typename TraitsT::version_type;
    using naked_version_type = typename version_type::value_type;
    using traits = TraitsT;

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
    constexpr value_type & emplace(Args &&... args);

    /**
     * Destroy the stored value
     *
     * @pre Slot must be in ALIVE state
     * @post Slot is in FREE state (next field may contain garbage)
     */
    constexpr void destroy() noexcept(
        std::is_nothrow_destructible_v<value_type>);

    /**
     * Access the stored value
     *
     * @pre Slot must be in ALIVE state
     */
    [[nodiscard]]
    constexpr value_type & value() noexcept;

    [[nodiscard]]
    constexpr value_type const & value() const noexcept;

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
        detail::has_alive_bit<TraitsT>();

    /**
     * Check if the slot is alive (has a constructed value).
     *
     * @note When has_embedded_alive_bit is true, this reads from the
     *       version_bytes_ field (same cache line as the slot). When false,
     *       it always returns true and the slab's bitmap must be consulted.
     */
    [[nodiscard]]
    constexpr bool is_alive() const noexcept;

    /**
     * Get the raw version storage including alive bit (if embedded).
     *
     * This returns the raw version_bytes_ as an integer, which includes
     * the alive bit in the MSB when has_embedded_alive_bit is true.
     *
     * Used for combined version+alive check optimization in lookup paths.
     *
     * @note Only meaningful when has_embedded_alive_bit is true
     */
    [[nodiscard]]
    constexpr naked_version_type version_with_alive_bit() const noexcept;

    /**
     * Compute expected version value with alive bit set.
     *
     * Takes a version and ORs in the alive bit, producing a value that
     * can be compared directly against version_with_alive_bit() to check
     * both version match and alive status in a single comparison.
     *
     * @param v The version to combine with alive bit
     * @return Version with alive bit set (as raw integer type)
     *
     * @note Only valid when has_embedded_alive_bit is true (static_assert)
     */
    [[nodiscard]]
    static constexpr naked_version_type make_alive_version(
        version_type v) noexcept
    {
        static_assert(
            has_embedded_alive_bit,
            "make_alive_version only valid when alive bit is embedded");
        return v.value | alive_bit;
    }

private:
    // Storage for either index_type or value_type
    alignas(std::max(alignof(index_type), alignof(value_type))) std::array<
        std::byte,
        std::max(sizeof(index_type), sizeof(value_type))> storage_;

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

    static constexpr auto version_digits =
        std::numeric_limits<naked_version_type>::digits;
    static constexpr naked_version_type alive_bit = naked_version_type(1)
        << (version_digits - 1);
};

} // namespace wjh::slotmap::detail

#include "Slot.ipp"

#endif // WJH_SLOTMAP_210A58D772C142B58C2FDB99480EA93F
