// ----------------------------------------------------------------------
// Copyright 2025 Jody Hagins
// Distributed under the MIT Software License
// See accompanying file LICENSE or copy at
// https://opensource.org/licenses/MIT
// ----------------------------------------------------------------------
#ifndef WJH_SLOTMAP_073D1EC2FEF04177914D3CC646306810
#define WJH_SLOTMAP_073D1EC2FEF04177914D3CC646306810

#include "detail.hpp"

#include <cassert>
#include <compare>
#include <cstddef>
#include <functional>

namespace wjh::slotmap {

/**
 * A type-safe, bit-packed key with compile-time validation
 *
 * @tparam T  The type of the mapped item that this key will be used for. For
 * the purpose of @p Key, this is a phantom type for type, because it is not
 * used.
 *
 * @tparam IndexBits  Number of bits allocated for the index field. Must be
 * greater than 0.
 *
 * @tparam VersionBits  Number of bits allocated for the version field. Must be
 * greater than 0.
 *
 * @tparam UserBits  Number of bits allocated for user-defined data. If 0, there
 * are no user controlled bits. Defaults to 0.
 *
 * The sum of IndexBits + VersionBits + UserBits must equal 16, 32, 64, or 128.
 * 128-bit keys are only supported on platforms with __uint128_t.
 *
 * Bit layout: [user][version][index] from MSB to LSB.
 */
template <
    typename T,
    unsigned IndexBits,
    unsigned VersionBits,
    unsigned UserBits = 0>
class Key
: private detail::
      KeyBase<detail::storage_type_t<IndexBits + VersionBits + UserBits>, T>
{
    static_assert(IndexBits > 0);
    static_assert(IndexBits < 64);
    static_assert(VersionBits > 0);
    static constexpr unsigned num_bits = IndexBits + VersionBits + UserBits;
    using Base = detail::KeyBase<detail::storage_type_t<num_bits>, T>;
    template <unsigned N, typename DerivedT>
    using TypeBase = detail::TypeBase<N, DerivedT>;

    struct Index
    : TypeBase<IndexBits, Index>
    {
        using TypeBase<IndexBits, Index>::TypeBase;
    };

    using naked_index_type = typename Index::value_type;

    struct Size
    : TypeBase<IndexBits + 1, Size>
    {
        using TypeBase<IndexBits + 1, Size>::TypeBase;

        constexpr Size(Index index)
        : Size(index.value)
        { }

        friend constexpr auto operator <=> (Size x, Size y) = default;
        friend constexpr bool operator == (Size x, Size y) = default;
    };

    using naked_size_type = typename Size::value_type;

    struct Version
    : TypeBase<VersionBits, Version>
    {
        using TypeBase<VersionBits, Version>::TypeBase;
    };

    using naked_version_type = typename Version::value_type;

    class User
    : public TypeBase<UserBits, User>
    {
        using Base = TypeBase<UserBits, User>;

    public:
        using value_type = typename TypeBase<UserBits, User>::value_type;
        using Base::Base;

        constexpr User(std::unsigned_integral auto v)
        : Base(value_type(v))
        { }
    };

    using naked_user_type = typename User::value_type;

public:
    // ========================================================================
    // Member types
    // ========================================================================
    using value_type = typename Base::value_type;
    using tag_type = typename Base::tag_type;
    using index_type = Index;
    using size_type = Size;
    using version_type = Version;
    using user_type = User;

    // ========================================================================
    // Compile-time constants
    // ========================================================================
    static constexpr unsigned index_bits = IndexBits;
    static constexpr unsigned version_bits = VersionBits;
    static constexpr unsigned user_bits = UserBits;

    // ========================================================================
    // Special member functions
    // ========================================================================

    /**
     * The base class defines default constructors.
     *
     * If @p T, the type passed in, is wrapped with detail::Trivial, then this
     * type will be trivially default constructible, and the internal bits will
     * be uninitialized. Otherwise, the internal bits will be initialized to 0.
     */
    using Base::Base;

    /**
     * Construct a Key with its components.
     */
    explicit constexpr Key(
        index_type index,
        version_type version,
        user_type user) noexcept;

    /**
     * Construct a Key with its components, sans user_type, which will be 0.
     */
    explicit constexpr Key(index_type index, version_type version) noexcept;

    // ========================================================================
    // Static member functions
    // ========================================================================

    /**
     * Create a null key (all bits zero)
     */
    [[nodiscard]]
    static constexpr Key null() noexcept;

    // ========================================================================
    // Data access
    // ========================================================================

    [[nodiscard]]
    constexpr index_type index() const noexcept;

    [[nodiscard]]
    constexpr version_type version() const noexcept;

    [[nodiscard]]
    constexpr user_type user() const noexcept;

    /**
     * Get the underlying raw bits of the key.
     *
     * @note  The returned type is a naked unsigned integral type.
     */
    [[nodiscard]]
    constexpr value_type to_underlying() const noexcept;

    // ========================================================================
    // Operations
    // ========================================================================

    /**
     * Create a new key with modified user bits
     */
    [[nodiscard]]
    constexpr Key with_user(user_type new_user) const noexcept;

    /**
     * Check if the key is equal to the null_key.
     */
    [[nodiscard]]
    constexpr bool is_null() const noexcept;

    /**
     * Return a hash of the key.
     *
     * @note  This hash function is constexpr, and since std::hash is not, the
     * returned has is NOT guaranteed to yield the same as
     * std::hash<value_type>{}().
     */
    [[nodiscard]]
    constexpr std::size_t hash() const noexcept;

    // ========================================================================
    // Hidden friends
    // ========================================================================

    /**
     * Get the underlying raw bits of the key.
     *
     * @note  The returned type is a naked unsigned integral type.
     */
    [[nodiscard]]
    friend constexpr value_type to_underlying(Key const & key) noexcept
    {
        return key.to_underlying();
    }

    [[nodiscard]]
    friend constexpr bool
    operator == (Key const & x, Key const & y) noexcept
    {
        return x.Base::bits_ == y.Base::bits_;
    }

    [[nodiscard]]
    friend constexpr auto
    operator <=> (Key const & x, Key const & y) noexcept
    {
        return x.Base::bits_ <=> y.Base::bits_;
    }

private:
    template <unsigned Bits>
    static constexpr value_type make_mask() noexcept;

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
        unsigned shift) noexcept;

    // Helper to shift right safely (handles 0-bit or full-width shifts)
    static constexpr value_type safe_shift_right(
        value_type val,
        unsigned shift) noexcept;
};

template <typename T>
struct is_key
: std::false_type
{ };

template <typename T, unsigned I, unsigned V, unsigned U>
struct is_key<Key<T, I, V, U>>
: std::true_type
{ };

template <typename T>
inline constexpr bool is_key_v = is_key<T>::value;

template <typename T>
concept KeyC = is_key_v<T>;

/**
 * The same as Key<T, IndexBits, VersionBits, UserBits>, except the Key class
 * will be trivially default constructible.
 */
template <
    typename T,
    unsigned IndexBits,
    unsigned VersionBits,
    unsigned UserBits = 0>
using TrivialKey =
    slotmap::Key<detail::Trivial<T>, IndexBits, VersionBits, UserBits>;

} // namespace wjh::slotmap

namespace wjh {

template <
    typename T,
    unsigned IndexBits,
    unsigned VersionBits,
    unsigned UserBits = 0>
using SlotMapKey = slotmap::Key<T, IndexBits, VersionBits, UserBits>;

template <
    typename T,
    unsigned IndexBits,
    unsigned VersionBits,
    unsigned UserBits = 0>
using TrivialSlotMapKey =
    slotmap::TrivialKey<T, IndexBits, VersionBits, UserBits>;

} // namespace wjh

/**
 * Specialization of std::hash for wjh::slotmap::Key.
 */
template <typename T, unsigned I, unsigned V, unsigned U>
struct std::hash<wjh::slotmap::Key<T, I, V, U>>
{
    /**
     * Return a hash of the key.
     *
     * @note  This hash function returns the same as key.hash(), which is NOT
     * guaranteed to yield the same as std::hash<value_type>{}().
     */
    [[nodiscard]]
    std::size_t
    operator () (wjh::slotmap::Key<T, I, V, U> const & key) const noexcept;
};

#include "Key.ipp"

#endif // WJH_SLOTMAP_073D1EC2FEF04177914D3CC646306810
