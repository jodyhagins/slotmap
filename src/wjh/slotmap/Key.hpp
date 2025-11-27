// ----------------------------------------------------------------------
// Copyright 2025 Jody Hagins
// Distributed under the MIT Software License
// See accompanying file LICENSE or copy at
// https://opensource.org/licenses/MIT
// ----------------------------------------------------------------------
#ifndef WJH_SLOTMAP_073D1EC2FEF04177914D3CC646306810
#define WJH_SLOTMAP_073D1EC2FEF04177914D3CC646306810

#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>

namespace wjh::slotmap {

namespace detail {

// Helper to select the appropriate storage type based on total bit count
template <unsigned TotalBits>
struct storage_type;

template <>
struct storage_type<32>
{
    using type = std::uint32_t;
};

template <>
struct storage_type<64>
{
    using type = std::uint64_t;
};

#ifdef __UINT128_TYPE__
template <>
struct storage_type<128>
{
    using type = unsigned __int128;
};
#endif

// Helper alias for cleaner code
template <unsigned TotalBits>
using storage_type_t = typename storage_type<TotalBits>::type;

} // namespace detail

/**
 * A type-safe, bit-packed key with compile-time validation
 *
 * @tparam IndexBits  Number of bits allocated for the index field
 *
 * @tparam VersionBits  Number of bits allocated for the version field
 *
 * @tparam UserBits  Number of bits allocated for user-defined data
 *
 * @tparam T  Phantom type for type safety (not stored, only for type
 * distinction)
 *
 * The sum of IndexBits + VersionBits + UserBits must equal 32, 64, or 128.
 * 128-bit keys are only supported on platforms with __uint128_t.
 *
 * Bit layout: [user][version][index] from MSB to LSB This ordering ensures that
 * index comparisons dominate in sorting.
 */
template <
    unsigned IndexBits,
    unsigned VersionBits,
    unsigned UserBits,
    typename T = void>
requires requires {
    typename detail::storage_type_t<IndexBits + VersionBits + UserBits>;
}
class Key
{
    static constexpr unsigned TotalBits = IndexBits + VersionBits + UserBits;

public:
    using value_type = detail::storage_type_t<TotalBits>;
    using tag_type = T;

private:
    value_type bits_;

    // Helper to compute mask safely (handles 0-bit case)
    template <unsigned Bits>
    static constexpr value_type make_mask() noexcept
    {
        if constexpr (Bits == 0) {
            return value_type{0};
        } else {
            return (value_type{1} << Bits) - value_type{1};
        }
    }

    // Bit masks for each field
    static constexpr value_type index_mask = make_mask<IndexBits>();
    static constexpr value_type version_mask = make_mask<VersionBits>();
    static constexpr value_type user_mask = make_mask<UserBits>();

    // Bit positions for each field (index starts at bit 0)
    static constexpr unsigned version_shift = IndexBits;
    static constexpr unsigned user_shift = IndexBits + VersionBits;

public:
    // Default constructor: creates a null key (all bits zero)
    constexpr Key() noexcept
    : bits_{0}
    { }

    // Helper to shift left safely (handles 0-bit or full-width shifts)
    static constexpr value_type safe_shift_left(
        value_type val,
        unsigned shift) noexcept
    {
        if (shift >= TotalBits) {
            return value_type{0};
        }
        return val << shift;
    }

    // Helper to shift right safely (handles 0-bit or full-width shifts)
    static constexpr value_type safe_shift_right(
        value_type val,
        unsigned shift) noexcept
    {
        if (shift >= TotalBits) {
            return value_type{0};
        }
        return val >> shift;
    }

public:
    // Construct a key from its components
    constexpr Key(
        value_type index,
        value_type version,
        value_type user = 0) noexcept
    : bits_{static_cast<value_type>(
        (index & index_mask) |
        safe_shift_left(version & version_mask, version_shift) |
        safe_shift_left(user & user_mask, user_shift))}
    { }

    // Extract the index field
    [[nodiscard]]
    constexpr value_type index() const noexcept
    {
        return bits_ & index_mask;
    }

    // Extract the version field
    [[nodiscard]]
    constexpr value_type version() const noexcept
    {
        return safe_shift_right(bits_, version_shift) & version_mask;
    }

    // Extract the user field
    [[nodiscard]]
    constexpr value_type user() const noexcept
    {
        return safe_shift_right(bits_, user_shift) & user_mask;
    }

    // Create a new key with modified user bits (immutable operation)
    [[nodiscard]]
    constexpr Key with_user(value_type new_user) const noexcept
    {
        // Clear existing user bits and set new ones
        value_type cleared = bits_ & ~safe_shift_left(user_mask, user_shift);
        value_type updated = cleared |
            safe_shift_left(new_user & user_mask, user_shift);

        Key result;
        result.bits_ = updated;
        return result;
    }

    // Get the raw underlying bits
    [[nodiscard]]
    constexpr value_type to_underlying() const noexcept
    {
        return bits_;
    }

    // Create a null key (all bits zero)
    [[nodiscard]]
    static constexpr Key null() noexcept
    {
        return Key{};
    }

    // Check if this key is null (all bits zero)
    [[nodiscard]]
    constexpr bool is_null() const noexcept
    {
        return bits_ == value_type{0};
    }

    // Compute hash of this key
    [[nodiscard]]
    constexpr std::size_t hash() const noexcept
    {
        // For types that fit in size_t, just use the bits directly
        if constexpr (sizeof(value_type) <= sizeof(std::size_t)) {
            return static_cast<std::size_t>(bits_);
        } else {
            // For 128-bit types on 64-bit platforms, fold the upper and lower
            // halves This is a simple hash that preserves the constexpr
            // requirement
            auto lower = static_cast<std::size_t>(bits_);
            auto upper = static_cast<std::size_t>(bits_ >> 64);
            return lower ^ upper;
        }
    }

    // Equality comparison (defaulted)
    [[nodiscard]]
    constexpr bool
    operator == (Key const &) const noexcept = default;

    // Three-way comparison (defaulted)
    // This compares the entire bit pattern, giving lexicographic ordering
    [[nodiscard]]
    constexpr auto
    operator <=> (Key const &) const noexcept = default;
};

} // namespace wjh::slotmap

namespace wjh {
template <
    unsigned IndexBits,
    unsigned VersionBits,
    unsigned UserBits,
    typename T = void>
using SlotMapKey = slotmap::Key<IndexBits, VersionBits, UserBits, T>;
} // namespace wjh

// std::hash specialization for Key
template <
    unsigned IndexBits,
    unsigned VersionBits,
    unsigned UserBits,
    typename T>
struct std::hash<wjh::slotmap::Key<IndexBits, VersionBits, UserBits, T>>
{
    [[nodiscard]]
    constexpr std::size_t
    operator () (wjh::slotmap::Key<IndexBits, VersionBits, UserBits, T> const &
                     key) const noexcept
    {
        return key.hash();
    }
};

#endif // WJH_SLOTMAP_073D1EC2FEF04177914D3CC646306810
