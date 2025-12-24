// ----------------------------------------------------------------------
// Copyright 2025 Jody Hagins
// Distributed under the MIT Software License
// See accompanying file LICENSE or copy at
// https://opensource.org/licenses/MIT
// ----------------------------------------------------------------------
#ifndef WJH_SLOTMAP_073D1EC2FEF04177914D3CC646306810
#define WJH_SLOTMAP_073D1EC2FEF04177914D3CC646306810

#include "abi.hpp"
#include "detail.hpp"
#include "types.hpp"

#include <cassert>
#include <compare>
#include <cstddef>
#include <functional>

namespace wjh::slotmap {
WJH_SLOTMAP_NAMESPACE_BEGIN

/**
 * A type-safe, bit-packed key with compile-time validation
 *
 * @tparam T  The type of the mapped item that this key will be used for. For
 * the purpose of @p Key, this is a phantom type, because it is not used.
 *
 * @tparam I  Number of bits allocated for the index field. Must be
 * greater than 0.
 *
 * @tparam V  Number of bits allocated for the version field. Must be
 * greater than 0.
 *
 * @tparam U  Number of bits allocated for user-defined data. If 0, there
 * are no user controlled bits. Defaults to 0.
 *
 * The sum of I + V + U must equal 16, 32, 64, or 128.
 * 128-bit keys are only supported on platforms with __uint128_t.
 *
 * Bit layout: [user][version][index] from MSB to LSB.
 */
template <typename T, IndexBits I, VersionBits V, UserBits U = UserBits(0)>
class Key
: private detail::KeyBase<
      detail::storage_type_t<unsigned(I) + unsigned(V) + unsigned(U)>,
      T>
{
    static constexpr unsigned index_bits_value = static_cast<unsigned>(I);
    static constexpr unsigned version_bits_value = static_cast<unsigned>(V);
    static constexpr unsigned user_bits_value = static_cast<unsigned>(U);

    static_assert(index_bits_value > 0);
    static_assert(index_bits_value < 64);
    static_assert(version_bits_value > 0);
    static_assert(version_bits_value < 64);
    static constexpr unsigned num_bits = index_bits_value + version_bits_value +
        user_bits_value;
    using Base = detail::KeyBase<detail::storage_type_t<num_bits>, T>;
    template <unsigned N, typename DerivedT>
    using TypeBase = detail::TypeBase<N, DerivedT>;

    struct Index
    : TypeBase<index_bits_value, Index>
    {
        using TypeBase<index_bits_value, Index>::TypeBase;
    };

    using naked_index_type = typename Index::value_type;

    struct Size
    : TypeBase<index_bits_value + 1, Size>
    {
        using TypeBase<index_bits_value + 1, Size>::TypeBase;

        constexpr Size(Index index)
        : Size(index.value)
        { }

        friend constexpr auto operator <=> (Size x, Size y) = default;
        friend constexpr bool operator == (Size x, Size y) = default;
    };

    using naked_size_type = typename Size::value_type;

    struct Version
    : TypeBase<version_bits_value, Version>
    {
        using TypeBase<version_bits_value, Version>::TypeBase;
    };

    using naked_version_type = typename Version::value_type;

    class User
    : public TypeBase<user_bits_value, User>
    {
        using Base = TypeBase<user_bits_value, User>;

    public:
        using value_type = typename TypeBase<user_bits_value, User>::value_type;
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

    static constexpr unsigned index_bits = index_bits_value;
    static constexpr unsigned version_bits = version_bits_value;
    static constexpr unsigned user_bits = user_bits_value;

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
     * Cast to bool for logical expressions.
     *
     * @return  true if the key is not null.
     */
    [[nodiscard]] explicit constexpr operator bool () const noexcept;

    /**
     * Return a hash of the key.
     *
     * @note  This hash function is constexpr, and since std::hash is not, the
     * returned has is NOT guaranteed to yield the same as
     * std::hash<value_type>{}().
     */
    [[nodiscard]]
    constexpr std::size_t hash() const noexcept;

    /**
     * Check if this key identifies the same slot as another key.
     *
     * This compares only the index and version bits, ignoring user bits.
     * Two keys that return true will access the same underlying object in a
     * SlotMap, even if they have different user bits.
     *
     * @param key  The key to compare against
     *
     * @return true if both keys reference the same slot (index and version
     * match)
     *
     * @note This is different from operator==, which compares ALL bits
     *       including user bits.
     *
     * Example:
     * @code
     * auto k1 = map.emplace(value);
     * auto k2 = k1.with_user({7});
     * assert(k1 != k2); // Different keys (user bits differ)
     * assert(k1.identifies_same_object(k2)); // Same underlying slot
     * assert(map.contains(k1) && map.contains(k2)); // Both find same element
     * @endcode
     */
    [[nodiscard]]
    constexpr bool identifies_same_object(Key const & key) const noexcept;

private:
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

    /**
     * Equality is a comparison of the underlying bits for the key.
     *
     * @note  Keys that differ only in user-bits are not the same key, but they
     * identify the same object because user bits are ignored when looking for
     * the object.
     */
    [[nodiscard]]
    friend constexpr bool
    operator == (Key const & x, Key const & y) noexcept
    {
        return x.Base::bits_ == y.Base::bits_;
    }

    /**
     * Spaceship is a comparison of the underlying bits for the key.
     *
     * @note  Keys that differ only in user-bits are not the same key, but they
     * identify the same object because user bits are ignored when looking for
     * the object.
     */
    [[nodiscard]]
    friend constexpr auto
    operator <=> (Key const & x, Key const & y) noexcept
    {
        return x.Base::bits_ <=> y.Base::bits_;
    }

    // Bit masks for each field
    template <unsigned Bits>
    static constexpr value_type make_mask() noexcept;
    static constexpr value_type index_mask = make_mask<index_bits_value>();
    static constexpr value_type version_mask = make_mask<version_bits_value>();
    static constexpr value_type user_mask = make_mask<user_bits_value>();

    // Bit positions for each field (index starts at bit 0)
    static constexpr unsigned version_shift = index_bits_value;
    static constexpr unsigned user_shift = index_bits_value +
        version_bits_value;

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

template <typename T, IndexBits I, VersionBits V, UserBits U>
struct is_key<Key<T, I, V, U>>
: std::true_type
{ };

template <typename T>
inline constexpr bool is_key_v = is_key<T>::value;

template <typename T>
concept KeyC = is_key_v<T>;

/**
 * The same as Key<T, I, V, U>, except the Key class
 * will be trivially default constructible.
 */
template <typename T, IndexBits I, VersionBits V, UserBits U = UserBits(0)>
using TrivialKey = slotmap::Key<detail::Trivial<T>, I, V, U>;

WJH_SLOTMAP_NAMESPACE_END
} // namespace wjh::slotmap

namespace wjh {

template <
    typename T,
    slotmap::IndexBits I,
    slotmap::VersionBits V,
    slotmap::UserBits U = slotmap::UserBits(0)>
using SlotMapKey = slotmap::Key<T, I, V, U>;

template <
    typename T,
    slotmap::IndexBits I,
    slotmap::VersionBits V,
    slotmap::UserBits U = slotmap::UserBits(0)>
using TrivialSlotMapKey = slotmap::TrivialKey<T, I, V, U>;

} // namespace wjh

/**
 * Specialization of std::hash for wjh::slotmap::Key.
 */
template <
    typename T,
    wjh::slotmap::IndexBits I,
    wjh::slotmap::VersionBits V,
    wjh::slotmap::UserBits U>
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
