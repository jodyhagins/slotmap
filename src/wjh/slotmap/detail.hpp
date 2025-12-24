// ----------------------------------------------------------------------
// Copyright 2025 Jody Hagins
// Distributed under the MIT Software License
// See accompanying file LICENSE or copy at
// https://opensource.org/licenses/MIT
// ----------------------------------------------------------------------
// INTERNAL IMPLEMENTATION HEADER - Do not include directly.
// Use <wjh/slotmap/SlotMap.hpp> or <wjh/slotmap.hpp> instead.
// ----------------------------------------------------------------------
#ifndef WJH_SLOTMAP_CA3F4DEEB84042E584DF898CE8D8E93A
#define WJH_SLOTMAP_CA3F4DEEB84042E584DF898CE8D8E93A

#include "types.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace wjh::slotmap::detail {

template <unsigned NumBits, typename = std::true_type>
struct type_with_at_least;

template <unsigned N>
struct type_with_at_least<N, std::bool_constant<(N <= 8 && N >= 0)>>
{
    using type = std::uint8_t;
};

template <unsigned N>
struct type_with_at_least<N, std::bool_constant<(N <= 16 && N > 8)>>

{
    using type = std::uint16_t;
};

template <unsigned N>
struct type_with_at_least<N, std::bool_constant<(N <= 32 && N > 16)>>
{
    using type = std::uint32_t;
};

template <unsigned N>
struct type_with_at_least<N, std::bool_constant<(N <= 64 && N > 32)>>
{
    using type = std::uint64_t;
};
#ifdef __SIZEOF_INT128__
template <unsigned N>
struct type_with_at_least<N, std::bool_constant<(N <= 128 && N > 64)>>
{
    using type = unsigned __int128;
};
#endif

template <unsigned N>
using type_with_at_least_t = typename type_with_at_least<N>::type;

// Helper to select the appropriate storage type based on total bit count
template <unsigned TotalBits>
struct storage_type;

template <>
struct storage_type<16>
{
    using type = std::uint16_t;
};

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

#ifdef __SIZEOF_INT128__
template <>
struct storage_type<128>
{
    using type = unsigned __int128;
};
#endif

// Helper alias for cleaner code
template <unsigned TotalBits>
using storage_type_t = typename storage_type<TotalBits>::type;

template <typename T>
struct Trivial
{
    using type = T;
};

template <typename ValueTypeT, typename TagTypeT>
struct KeyBase
{
    using value_type = ValueTypeT;
    using tag_type = TagTypeT;

    value_type bits_;

    constexpr KeyBase()
    : bits_(0)
    { }

protected:
    explicit constexpr KeyBase(value_type b)
    : bits_(b)
    { }
};

template <typename ValueTypeT, typename TagTypeT>
struct KeyBase<ValueTypeT, Trivial<TagTypeT>>
{
    using value_type = ValueTypeT;
    using tag_type = TagTypeT;

    value_type bits_;

private:
    // Trivial default constructor makes this an implicit lifetime type per the
    // C++ standard (visibility doesn't affect implicit lifetime status).
    // Private access prevents users from accidentally creating uninitialized
    // keys.
    constexpr KeyBase() = default;

protected:
    explicit constexpr KeyBase(value_type b)
    : bits_(b)
    { }
};

template <unsigned N, typename DerivedT>
struct TypeBase
{
    static constexpr unsigned num_bits = N;
    using value_type = detail::type_with_at_least_t<num_bits>;
    static constexpr value_type mask = [] {
        if constexpr (num_bits >= std::numeric_limits<value_type>::digits) {
            return value_type(~value_type{0});
        } else {
            return value_type((value_type{1} << num_bits) - 1);
        }
    }();
    value_type value;

    constexpr TypeBase() = default;

    template <typename ValT>
    constexpr explicit TypeBase(ValT val)
    requires requires { value_type{val}; }
    : value{val}
    {
        assert((val | mask) == mask);
    }

    // TODO: Consider making this explicit to enforce stronger type safety.
    // Implicit conversion is convenient but can mask type errors. Users who
    // need the raw value can use `.value` directly or an explicit cast.
    constexpr operator value_type () const { return value; }

    friend constexpr auto operator <=> (TypeBase x, TypeBase y) = default;

    template <typename ValT>
    requires(std::is_unsigned_v<ValT> && sizeof(ValT) <= sizeof(value_type))
    friend constexpr auto operator <=> (TypeBase x, ValT y)
    {
        return x.value <=> y;
    }

    friend constexpr bool operator == (TypeBase x, TypeBase y) = default;

    template <typename ValT>
    requires(std::is_unsigned_v<ValT> && sizeof(ValT) <= sizeof(value_type))
    friend constexpr bool operator == (TypeBase x, ValT y)
    {
        return x.value == y;
    }

    constexpr DerivedT & operator ++ ()
    {
        ++value;
        return static_cast<DerivedT &>(*this);
    }

    constexpr DerivedT operator ++ (int)
    {
        auto result = value;
        ++value;
        return DerivedT(result);
    }
};

// Constexpr hash function based on splitmix64
// This is a fast, high-quality hash with good avalanche properties
constexpr std::uint_fast64_t
splitmix64(std::uint_fast64_t x) noexcept
{
    x += 0x9e37'79b9'7f4a'7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58'476d'1ce4'e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d0'49bb'1331'11ebULL;
    return x ^ (x >> 31);
}

// Hash for 16-bit values
constexpr std::size_t
hash_bits(std::uint16_t x) noexcept
{
    return static_cast<std::size_t>(splitmix64(x));
}

// Hash for 32-bit values
constexpr std::size_t
hash_bits(std::uint32_t x) noexcept
{
    return static_cast<std::size_t>(splitmix64(x));
}

// Hash for 64-bit values
constexpr std::size_t
hash_bits(std::uint64_t x) noexcept
{
    return static_cast<std::size_t>(splitmix64(x));
}

#ifdef __SIZEOF_INT128__
// Hash for 128-bit values
constexpr std::size_t
hash_bits(unsigned __int128 x) noexcept
{
    auto lower = static_cast<std::uint64_t>(x);
    auto upper = static_cast<std::uint64_t>(x >> 64);
    // Combine the two halves with another round of mixing
    return static_cast<std::size_t>(splitmix64(lower ^ splitmix64(upper)));
}
#endif

} // namespace wjh::slotmap::detail

#endif // WJH_SLOTMAP_CA3F4DEEB84042E584DF898CE8D8E93A
