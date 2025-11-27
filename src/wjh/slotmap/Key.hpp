// ----------------------------------------------------------------------
// Copyright 2025 Jody Hagins
// Distributed under the MIT Software License
// See accompanying file LICENSE or copy at
// https://opensource.org/licenses/MIT
// ----------------------------------------------------------------------
#ifndef WJH_SLOTMAP_073D1EC2FEF04177914D3CC646306810
#define WJH_SLOTMAP_073D1EC2FEF04177914D3CC646306810

#include "detail.hpp"

#include <compare>
#include <cstddef>
#include <functional>

namespace wjh::slotmap {

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
: private detail::
      KeyBase<detail::storage_type_t<IndexBits + VersionBits + UserBits>, T>
{
    static constexpr unsigned TotalBits = IndexBits + VersionBits + UserBits;
    using Base = detail::KeyBase<detail::storage_type_t<TotalBits>, T>;

public:
    using value_type = typename Base::value_type;
    using tag_type = typename Base::tag_type;

    struct Index
    {
        using value_type = detail::type_with_at_least_t<IndexBits>;
        value_type value;

        constexpr operator value_type () const { return value; }
    };

    struct Version
    {
        using value_type = detail::type_with_at_least_t<VersionBits>;
        value_type value;

        constexpr operator value_type () const { return value; }
    };

    struct User
    {
        using value_type = detail::type_with_at_least_t<UserBits>;
        value_type value;

        constexpr operator value_type () const { return value; }
    };

    using Base::Base;

    constexpr Key(Index index, Version version, User user = {0}) noexcept
    : Base(static_cast<value_type>(
        (index.value & index_mask) |
        safe_shift_left(version.value & version_mask, version_shift) |
        safe_shift_left(user.value & user_mask, user_shift)))
    { }

    [[nodiscard]]
    constexpr Index index() const noexcept
    {
        return Index{static_cast<Index::value_type>(Base::bits_ & index_mask)};
    }

    [[nodiscard]]
    constexpr Version version() const noexcept
    {
        return Version{static_cast<Version::value_type>(
            safe_shift_right(Base::bits_, version_shift) & version_mask)};
    }

    [[nodiscard]]
    constexpr User user() const noexcept
    {
        return User{static_cast<User::value_type>(
            safe_shift_right(Base::bits_, user_shift) & user_mask)};
    }

    // Create a new key with modified user bits
    [[nodiscard]]
    constexpr Key with_user(User new_user) const noexcept
    {
        // Clear existing user bits and set new ones
        auto const cleared = Base::bits_ &
            ~safe_shift_left(user_mask, user_shift);
        auto const updated = cleared |
            safe_shift_left(new_user.value & user_mask, user_shift);

        Key result;
        result.Base::bits_ = updated;
        return result;
    }

    [[nodiscard]]
    constexpr value_type to_underlying() const noexcept
    {
        return Base::bits_;
    }

    // Create a null key (all bits zero)
    [[nodiscard]]
    static constexpr Key null() noexcept
    {
        return Key{};
    }

    [[nodiscard]]
    constexpr bool is_null() const noexcept
    {
        return Base::bits_ == value_type{0};
    }

    [[nodiscard]]
    constexpr std::size_t hash() const noexcept
    {
        return detail::hash_bits(Base::bits_);
    }

private:
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

    // Equality comparison (defaulted)
    [[nodiscard]]
    friend constexpr bool
    operator == (Key const & x, Key const & y) noexcept
    {
        return x.Base::bits_ == y.Base::bits_;
    }

    // Three-way comparison (defaulted)
    // This compares the entire bit pattern, giving lexicographic ordering
    [[nodiscard]]
    friend constexpr auto
    operator <=> (Key const & x, Key const & y) noexcept
    {
        return x.Base::bits_ <=> y.Base::bits_;
    }
};

template <
    unsigned IndexBits,
    unsigned VersionBits,
    unsigned UserBits,
    typename T = void>
using TrivialKey =
    slotmap::Key<IndexBits, VersionBits, UserBits, detail::Trivial<T>>;

} // namespace wjh::slotmap

namespace wjh {

template <
    unsigned IndexBits,
    unsigned VersionBits,
    unsigned UserBits,
    typename T = void>
using SlotMapKey = slotmap::Key<IndexBits, VersionBits, UserBits, T>;

template <
    unsigned IndexBits,
    unsigned VersionBits,
    unsigned UserBits,
    typename T = void>
using TrivialSlotMapKey =
    slotmap::TrivialKey<IndexBits, VersionBits, UserBits, T>;

} // namespace wjh

/**
 * Specialization of std::hash for wjh::slotmap::Key.
 */
template <
    unsigned IndexBits,
    unsigned VersionBits,
    unsigned UserBits,
    typename T>
struct std::hash<wjh::slotmap::Key<IndexBits, VersionBits, UserBits, T>>
{
    [[nodiscard]]
    std::size_t
    operator () (wjh::slotmap::Key<IndexBits, VersionBits, UserBits, T> const &
                     key) const noexcept
    {
        return key.hash();
    }
};

#endif // WJH_SLOTMAP_073D1EC2FEF04177914D3CC646306810
