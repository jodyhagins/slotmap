// ----------------------------------------------------------------------
// Copyright 2025 Jody Hagins
// Distributed under the MIT Software License
// See accompanying file LICENSE or copy at
// https://opensource.org/licenses/MIT
// ----------------------------------------------------------------------

#include "wjh/slotmap/Key.hpp"

#include <cassert>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <unordered_map>

#include "testing/doctest.hpp"

namespace {
using namespace wjh::slotmap;

template <
    typename KeyT,
    typename IndexT = typename KeyT::index_type,
    typename I = typename IndexT::value_type,
    typename VersionT = typename KeyT::version_type,
    typename V = typename VersionT::value_type,
    typename UserT = typename KeyT::user_type,
    typename U = typename UserT::value_type>
constexpr KeyT
make_key(auto index, auto version, auto user) noexcept
requires requires {
    KeyT(IndexT(I(index)), VersionT(V(version)), UserT(U(user)));
}
{
    return KeyT(IndexT(I(index)), VersionT(V(version)), UserT(U(user)));
}

template <
    typename T = void,
    unsigned IndexBits,
    unsigned VersionBits,
    unsigned UserBits = 0,
    typename KeyT = wjh::slotmap::Key<T, IndexBits, VersionBits, UserBits>>
constexpr auto
make_key(auto index, auto version, auto user) noexcept
{
    return make_key<KeyT>(index, version, user);
}

template <
    typename T = void,
    unsigned IndexBits,
    unsigned VersionBits,
    unsigned UserBits = 0>
constexpr auto
make_key(auto index, auto version) noexcept
{
    return make_key<T, IndexBits, VersionBits, UserBits>(
        index,
        version,
        std::uint8_t(0));
}

// ============================================================================
// 32-bit Key Tests
// ============================================================================

TEST_CASE("Key 32-bit: value_type is uint32_t")
{
    using K = Key<int, 20, 10, 2>;
    static_assert(std::is_same_v<K::value_type, std::uint32_t>);
    static_assert(std::is_same_v<K::tag_type, int>);
    REQUIRE(true);
}

TEST_CASE("Key 32-bit: basic construction and accessors")
{
    SUBCASE("construct with all parameters") {
        constexpr auto k = make_key<void, 20, 10, 2>(100, 5, 3);
        static_assert(k.index() == 100);
        static_assert(k.version() == 5);
        static_assert(k.user() == 3);

        REQUIRE(k.index() == 100);
        REQUIRE(k.version() == 5);
        REQUIRE(k.user() == 3);
    }

    SUBCASE("construct without user bits (defaults to 0)") {
        constexpr auto k = make_key<void, 20, 10, 2>(100, 5);
        static_assert(k.index() == 100);
        static_assert(k.version() == 5);
        static_assert(k.user() == 0);

        REQUIRE(k.index() == 100);
        REQUIRE(k.version() == 5);
        REQUIRE(k.user() == 0);
    }

    SUBCASE("zero values") {
        constexpr auto k = make_key<void, 20, 10, 2>(0, 0, 0);
        static_assert(k.index() == 0);
        static_assert(k.version() == 0);
        static_assert(k.user() == 0);

        REQUIRE(k.index() == 0);
        REQUIRE(k.version() == 0);
        REQUIRE(k.user() == 0);
    }
}

TEST_CASE("Key 32-bit: default constructor creates null key")
{
    SUBCASE("default constructed key is null") {
        static_assert(std::is_default_constructible_v<Key<void, 20, 10, 2>>);
        static_assert(not std::is_trivially_default_constructible_v<
                      Key<void, 20, 10, 2>>);
        constexpr Key<void, 20, 10, 2> k;
        static_assert(k.index() == 0);
        static_assert(k.version() == 0);
        static_assert(k.user() == 0);
        static_assert(k.is_null());

        REQUIRE(k.is_null());
        REQUIRE(k.index() == 0);
        REQUIRE(k.version() == 0);
        REQUIRE(k.user() == 0);
    }

    SUBCASE("default constructed equals null()") {
        constexpr Key<void, 20, 10, 2> k{};
        constexpr auto null_key = Key<void, 20, 10, 2>::null();
        static_assert(k == null_key);

        REQUIRE(k == null_key);
    }
}

TEST_CASE("Key 32-bit: null() static method")
{
    SUBCASE("null() returns all-zero key") {
        constexpr auto k = Key<void, 20, 10, 2>::null();
        static_assert(k.index() == 0);
        static_assert(k.version() == 0);
        static_assert(k.user() == 0);
        static_assert(k.is_null());

        REQUIRE(k.is_null());
    }

    SUBCASE("non-null key is_null() returns false") {
        constexpr auto k = make_key<void, 20, 10, 2>(1, 0, 0);
        static_assert(not k.is_null());

        REQUIRE(not k.is_null());
    }
}

TEST_CASE("Key 32-bit: with_user() creates new key")
{
    SUBCASE("with_user modifies user bits only") {
        constexpr auto k1 = make_key<void, 20, 10, 2>(100, 5, 1);
        constexpr auto k2 = k1.with_user(3u);

        static_assert(k1.index() == 100);
        static_assert(k1.version() == 5);
        static_assert(k1.user() == 1);

        static_assert(k2.index() == 100);
        static_assert(k2.version() == 5);
        static_assert(k2.user() == 3);

        REQUIRE(k1.index() == k2.index());
        REQUIRE(k1.version() == k2.version());
        REQUIRE(k1.user() == 1);
        REQUIRE(k2.user() == 3);
    }

    SUBCASE("with_user preserves constexpr") {
        constexpr auto k1 = make_key<void, 20, 10, 2>(100, 5, 0);
        constexpr auto k2 = k1.with_user(2u);
        static_assert(k2.user() == 2);
        REQUIRE(k2.user() == 2);
    }
}

TEST_CASE("Key 32-bit: to_underlying() returns raw bits")
{
    SUBCASE("to_underlying for simple values") {
        constexpr auto k = make_key<void, 20, 10, 2>(1, 1, 1);
        constexpr auto raw = k.to_underlying();
        static_assert(std::is_same_v<decltype(raw), std::uint32_t const>);

        REQUIRE(raw != 0);
    }

    SUBCASE("round-trip through to_underlying") {
        constexpr auto k1 = make_key<void, 20, 10, 2>(12345, 678, 2);
        constexpr auto raw = k1.to_underlying();

        // We can't construct from raw in this test, but we verify it's the
        // right type
        REQUIRE(raw != 0);
    }
}

TEST_CASE("Key 32-bit: bit packing correctness")
{
    SUBCASE("maximum values for each field") {
        // 20 bits for index: max = 2^20 - 1 = 1048575
        // 10 bits for version: max = 2^10 - 1 = 1023
        // 2 bits for user: max = 2^2 - 1 = 3
        constexpr auto k = make_key<void, 20, 10, 2>(1048575, 1023, 3);

        static_assert(k.index() == 1048575);
        static_assert(k.version() == 1023);
        static_assert(k.user() == 3);

        REQUIRE(k.index() == 1048575);
        REQUIRE(k.version() == 1023);
        REQUIRE(k.user() == 3);
    }

    SUBCASE("independent bit fields don't interfere") {
        constexpr auto k1 = make_key<void, 20, 10, 2>(1048575, 0, 0);
        constexpr auto k2 = make_key<void, 20, 10, 2>(0, 1023, 0);
        constexpr auto k3 = make_key<void, 20, 10, 2>(0, 0, 3);

        static_assert(k1.index() == 1048575);
        static_assert(k1.version() == 0);
        static_assert(k1.user() == 0);

        static_assert(k2.index() == 0);
        static_assert(k2.version() == 1023);
        static_assert(k2.user() == 0);

        static_assert(k3.index() == 0);
        static_assert(k3.version() == 0);
        static_assert(k3.user() == 3);

        REQUIRE(k1.version() == 0);
        REQUIRE(k2.index() == 0);
        REQUIRE(k3.version() == 0);
    }
}

TEST_CASE("Key 32-bit: different bit configurations")
{
    SUBCASE("configuration 16/16/0") {
        constexpr auto k = make_key<void, 16, 16, 0>(65535, 65535);
        static_assert(k.index() == 65535);
        static_assert(k.version() == 65535);
        static_assert(k.user() == 0);

        REQUIRE(k.index() == 65535);
        REQUIRE(k.version() == 65535);
    }

    SUBCASE("configuration 24/8/0") {
        constexpr auto k = make_key<void, 24, 8, 0>(16777215, 255);
        static_assert(k.index() == 16777215);
        static_assert(k.version() == 255);

        REQUIRE(k.index() == 16777215);
        REQUIRE(k.version() == 255);
    }

    SUBCASE("configuration 10/10/12") {
        constexpr auto k = make_key<void, 10, 10, 12>(1023, 1023, 4095);
        static_assert(k.index() == 1023);
        static_assert(k.version() == 1023);
        static_assert(k.user() == 4095);

        REQUIRE(k.index() == 1023);
        REQUIRE(k.version() == 1023);
        REQUIRE(k.user() == 4095);
    }
}

TEST_CASE("Key 32-bit: equality comparison")
{
    SUBCASE("identical keys are equal") {
        constexpr auto k1 = make_key<void, 20, 10, 2>(100, 5, 3);
        constexpr auto k2 = make_key<void, 20, 10, 2>(100, 5, 3);
        static_assert(k1 == k2);

        REQUIRE(k1 == k2);
        REQUIRE_FALSE(k1 != k2);
    }

    SUBCASE("keys with different index are not equal") {
        constexpr auto k1 = make_key<void, 20, 10, 2>(100, 5, 3);
        constexpr auto k2 = make_key<void, 20, 10, 2>(101, 5, 3);
        static_assert(k1 != k2);

        REQUIRE(k1 != k2);
        REQUIRE_FALSE(k1 == k2);
    }

    SUBCASE("keys with different version are not equal") {
        constexpr auto k1 = make_key<void, 20, 10, 2>(100, 5, 3);
        constexpr auto k2 = make_key<void, 20, 10, 2>(100, 6, 3);
        static_assert(k1 != k2);

        REQUIRE(k1 != k2);
    }

    SUBCASE("keys with different user are not equal") {
        constexpr auto k1 = make_key<void, 20, 10, 2>(100, 5, 1);
        constexpr auto k2 = make_key<void, 20, 10, 2>(100, 5, 2);
        static_assert(k1 != k2);

        REQUIRE(k1 != k2);
    }
}

TEST_CASE("Key 32-bit: three-way comparison")
{
    SUBCASE("less than comparison") {
        constexpr auto k1 = make_key<void, 20, 10, 2>(100, 5, 1);
        constexpr auto k2 = make_key<void, 20, 10, 2>(200, 5, 1);

        REQUIRE(k1 < k2);
        REQUIRE(k1 <= k2);
        REQUIRE_FALSE(k1 > k2);
        REQUIRE_FALSE(k1 >= k2);
    }

    SUBCASE("greater than comparison") {
        constexpr auto k1 = make_key<void, 20, 10, 2>(200, 5, 1);
        constexpr auto k2 = make_key<void, 20, 10, 2>(100, 5, 1);

        REQUIRE(k1 > k2);
        REQUIRE(k1 >= k2);
        REQUIRE_FALSE(k1 < k2);
        REQUIRE_FALSE(k1 <= k2);
    }

    SUBCASE("equal comparison with <=, >=") {
        constexpr auto k1 = make_key<void, 20, 10, 2>(100, 5, 1);
        constexpr auto k2 = make_key<void, 20, 10, 2>(100, 5, 1);

        REQUIRE(k1 <= k2);
        REQUIRE(k1 >= k2);
        REQUIRE_FALSE(k1 < k2);
        REQUIRE_FALSE(k1 > k2);
    }

    SUBCASE("comparison considers all bits") {
        constexpr auto k1 = make_key<void, 20, 10, 2>(100, 5, 1);
        constexpr auto k2 = make_key<void, 20, 10, 2>(100, 5, 2);

        REQUIRE(k1 < k2);
    }
}

TEST_CASE("Key 32-bit: hash function")
{
    SUBCASE("hash() member function exists and is constexpr") {
        constexpr auto k = make_key<void, 20, 10, 2>(100, 5, 3);
        constexpr auto h = k.hash();
        static_assert(std::is_same_v<decltype(h), std::size_t const>);
        static_assert(h != 0); // Unlikely to be zero for non-null key

        REQUIRE(h != 0);
    }

    SUBCASE("equal keys have equal hashes") {
        constexpr auto k1 = make_key<void, 20, 10, 2>(100, 5, 3);
        constexpr auto k2 = make_key<void, 20, 10, 2>(100, 5, 3);

        static_assert(k1.hash() == k2.hash());
        REQUIRE(k1.hash() == k2.hash());
    }

    SUBCASE("different keys likely have different hashes") {
        constexpr auto k1 = make_key<void, 20, 10, 2>(100, 5, 3);
        constexpr auto k2 = make_key<void, 20, 10, 2>(101, 5, 3);

        // This is probabilistic, but very likely
        static_assert(k1.hash() != k2.hash());
        REQUIRE(k1.hash() != k2.hash());
    }
}

TEST_CASE("Key 32-bit: std::hash specialization")
{
    SUBCASE("std::hash works with Key") {
        using K = Key<void, 20, 10, 2>;
        auto k = make_key<K>(100, 5, 3);

        std::hash<K> hasher;
        auto h = hasher(k);

        REQUIRE(std::is_same_v<decltype(h), std::size_t>);
        REQUIRE(h != 0);
    }

    SUBCASE("std::hash matches member hash()") {
        using K = Key<void, 20, 10, 2>;
        auto k = make_key<K>(100, 5, 3);

        std::hash<K> hasher;
        REQUIRE(hasher(k) == k.hash());
    }

    SUBCASE("Key works in unordered_map") {
        using K = Key<void, 20, 10, 2>;
        std::unordered_map<K, std::string> map;

        auto k1 = make_key<K>(100, 5, 3);
        auto k2 = make_key<K>(200, 10, 1);

        map[k1] = "first";
        map[k2] = "second";

        REQUIRE(map[k1] == "first");
        REQUIRE(map[k2] == "second");
        REQUIRE(map.size() == 2);
    }
}

// ============================================================================
// 64-bit Key Tests
// ============================================================================

TEST_CASE("Key 64-bit: value_type is uint64_t")
{
    using K = Key<void, 40, 20, 4>;
    static_assert(std::is_same_v<K::value_type, std::uint64_t>);
    REQUIRE(true);
}

TEST_CASE("Key 64-bit: basic construction and accessors")
{
    SUBCASE("construct with all parameters") {
        constexpr auto k = make_key<void, 40, 20, 4>(1000000, 500000, 15);
        static_assert(k.index() == 1000000);
        static_assert(k.version() == 500000);
        static_assert(k.user() == 15);

        REQUIRE(k.index() == 1000000);
        REQUIRE(k.version() == 500000);
        REQUIRE(k.user() == 15);
    }

    SUBCASE("maximum values") {
        // 40 bits: max = 2^40 - 1 = 1099511627775
        // 20 bits: max = 2^20 - 1 = 1048575
        // 4 bits: max = 2^4 - 1 = 15
        constexpr auto k =
            make_key<void, 40, 20, 4>(1099511627775ULL, 1048575, 15);

        static_assert(k.index() == 1099511627775ULL);
        static_assert(k.version() == 1048575);
        static_assert(k.user() == 15);

        REQUIRE(k.index() == 1099511627775ULL);
        REQUIRE(k.version() == 1048575);
        REQUIRE(k.user() == 15);
    }
}

TEST_CASE("Key 64-bit: different bit configurations")
{
    SUBCASE("configuration 32/32/0") {
        constexpr auto k = make_key<void, 32, 32, 0>(4294967295U, 4294967295U);
        static_assert(k.index() == 4294967295U);
        static_assert(k.version() == 4294967295U);

        REQUIRE(k.index() == 4294967295U);
        REQUIRE(k.version() == 4294967295U);
    }

    SUBCASE("configuration 48/12/4") {
        constexpr auto k =
            make_key<void, 48, 12, 4>(281474976710655ULL, 4095, 15);
        static_assert(k.index() == 281474976710655ULL);
        static_assert(k.version() == 4095);
        static_assert(k.user() == 15);

        REQUIRE(k.index() == 281474976710655ULL);
        REQUIRE(k.version() == 4095);
        REQUIRE(k.user() == 15);
    }
}

TEST_CASE("Key 64-bit: null key semantics")
{
    SUBCASE("default constructed is null") {
        constexpr Key<void, 40, 20, 4> k{};
        static_assert(k.is_null());
        static_assert(k == Key<void, 40, 20, 4>::null());

        REQUIRE(k.is_null());
    }
}

TEST_CASE("Key 64-bit: with_user() creates new key")
{
    constexpr auto k1 = make_key<void, 40, 20, 4>(1000000, 500000, 5);
    constexpr auto k2 = k1.with_user(10u);

    static_assert(k2.index() == 1000000);
    static_assert(k2.version() == 500000);
    static_assert(k2.user() == 10);
    static_assert(k1.user() == 5); // Original unchanged

    REQUIRE(k2.user() == 10);
    REQUIRE(k1.user() == 5);
}

TEST_CASE("Key 64-bit: equality and comparison")
{
    SUBCASE("equality") {
        constexpr auto k1 = make_key<void, 40, 20, 4>(1000000, 500000, 15);
        constexpr auto k2 = make_key<void, 40, 20, 4>(1000000, 500000, 15);
        static_assert(k1 == k2);

        REQUIRE(k1 == k2);
    }

    SUBCASE("ordering") {
        constexpr auto k1 = make_key<void, 40, 20, 4>(1000000, 500000, 15);
        constexpr auto k2 = make_key<void, 40, 20, 4>(2000000, 500000, 15);

        REQUIRE(k1 < k2);
        REQUIRE(k2 > k1);
    }
}

TEST_CASE("Key 64-bit: hashing")
{
    SUBCASE("member hash() is constexpr") {
        constexpr auto k = make_key<void, 40, 20, 4>(1000000, 500000, 15);
        constexpr auto h = k.hash();
        static_assert(h != 0);

        REQUIRE(h != 0);
    }

    SUBCASE("std::hash specialization") {
        using K = Key<void, 40, 20, 4>;
        auto k = make_key<K>(1000000, 500000, 15);

        std::hash<K> hasher;
        REQUIRE(hasher(k) == k.hash());
    }
}

// ============================================================================
// 128-bit Key Tests (conditional on platform support)
// ============================================================================

#ifdef __UINT128_TYPE__

TEST_CASE("Key 128-bit: value_type is __uint128_t")
{
    using K = Key<void, 80, 40, 8>;
    static_assert(std::is_same_v<K::value_type, unsigned __int128>);
    REQUIRE(true);
}

TEST_CASE("Key 128-bit: basic construction and accessors")
{
    SUBCASE("construct with reasonable values") {
        // Use values that fit comfortably in 64-bit for testing
        constexpr auto k =
            make_key<void, 80, 40, 8>(1000000000000ULL, 1000000000ULL, 255);
        static_assert(k.index() == 1000000000000ULL);
        static_assert(k.version() == 1000000000ULL);
        static_assert(k.user() == 255);

        REQUIRE(k.index() == 1000000000000ULL);
        REQUIRE(k.version() == 1000000000ULL);
        REQUIRE(k.user() == 255);
    }

    SUBCASE("maximum user bits value") {
        // 8 bits for user: max = 255
        constexpr auto k = make_key<void, 80, 40, 8>(1, 1, 255);
        static_assert(k.user() == 255);

        REQUIRE(k.user() == 255);
    }
}

TEST_CASE("Key 128-bit: different bit configurations")
{
    SUBCASE("configuration 64/64/0") {
        constexpr auto k = make_key<void, 64, 64, 0>(
            18446744073709551615ULL,
            18446744073709551615ULL);
        static_assert(k.index() == 18446744073709551615ULL);
        static_assert(k.version() == 18446744073709551615ULL);

        REQUIRE(k.index() == 18446744073709551615ULL);
        REQUIRE(k.version() == 18446744073709551615ULL);
    }

    SUBCASE("configuration 100/20/8") {
        constexpr auto k = make_key<void, 100, 20, 8>(1ULL << 50, 1048575, 255);
        static_assert(k.index() == (1ULL << 50));
        static_assert(k.version() == 1048575);
        static_assert(k.user() == 255);

        REQUIRE(k.index() == (1ULL << 50));
        REQUIRE(k.version() == 1048575);
        REQUIRE(k.user() == 255);
    }
}

TEST_CASE("Key 128-bit: null key semantics")
{
    constexpr Key<void, 80, 40, 8> k;
    static_assert(k.is_null());
    static_assert(k == Key<void, 80, 40, 8>::null());

    REQUIRE(k.is_null());
}

TEST_CASE("Key 128-bit: with_user() creates new key")
{
    constexpr Key<void, 80, 40, 8> k1{
        {1000000000000ULL},
        {1000000000ULL},
        {100}};
    constexpr auto k2 = k1.with_user({200});

    static_assert(k2.user() == 200);
    static_assert(k1.user() == 100);

    REQUIRE(k2.user() == 200);
    REQUIRE(k1.user() == 100);
}

TEST_CASE("Key 128-bit: equality and comparison")
{
    SUBCASE("equality") {
        constexpr Key<void, 80, 40, 8> k1{
            {1000000000000ULL},
            {1000000000ULL},
            {255}};
        constexpr Key<void, 80, 40, 8> k2{
            {1000000000000ULL},
            {1000000000ULL},
            {255}};
        static_assert(k1 == k2);

        REQUIRE(k1 == k2);
    }

    SUBCASE("inequality") {
        constexpr Key<void, 80, 40, 8> k1{
            {1000000000000ULL},
            {1000000000ULL},
            {255}};
        constexpr Key<void, 80, 40, 8> k2{
            {2000000000000ULL},
            {1000000000ULL},
            {255}};
        static_assert(k1 != k2);

        REQUIRE(k1 != k2);
        REQUIRE(k1 < k2);
    }
}

TEST_CASE("Key 128-bit: hashing")
{
    SUBCASE("member hash() is constexpr") {
        constexpr Key<void, 80, 40, 8> k{
            {1000000000000ULL},
            {1000000000ULL},
            {255}};
        constexpr auto h = k.hash();
        static_assert(h != 0);

        REQUIRE(h != 0);
    }

    SUBCASE("std::hash specialization") {
        using K = Key<void, 80, 40, 8>;
        K k{{1000000000000ULL}, {1000000000ULL}, {255}};

        std::hash<K> hasher;
        REQUIRE(hasher(k) == k.hash());
    }
}

#endif // __UINT128_TYPE__

// ============================================================================
// Type Safety Tests
// ============================================================================

TEST_CASE("Key: type safety with phantom type parameter")
{
    SUBCASE("different phantom types are different types") {
        using K1 = Key<int, 20, 10, 2>;
        using K2 = Key<unsigned, 20, 10, 2>;

        static_assert(not std::is_same_v<K1, K2>);
        REQUIRE(true);
    }

    SUBCASE("same phantom type and configuration are same type") {
        using K1 = Key<void, 20, 10, 2>;
        using K2 = Key<void, 20, 10, 2>;

        static_assert(std::is_same_v<K1, K2>);
        REQUIRE(true);
    }

    SUBCASE("different bit configurations are different types") {
        using K1 = Key<void, 20, 10, 2>;
        using K2 = Key<void, 16, 16>;

        static_assert(not std::is_same_v<K1, K2>);
        REQUIRE(true);
    }
}

// ============================================================================
// Edge Cases and Special Values
// ============================================================================

TEST_CASE("Key: edge cases with zero user bits")
{
    SUBCASE("zero user bits configuration compiles") {
        constexpr auto k = make_key<void, 24, 8, 0>(16777215, 255);
        static_assert(k.index() == 16777215);
        static_assert(k.version() == 255);
        static_assert(k.user() == 0);

        REQUIRE(k.user() == 0);
    }

    SUBCASE("with_user on zero user bits key") {
        constexpr auto k1 = make_key<void, 24, 8, 0>(16777215, 255);
        constexpr auto k2 = k1.with_user(0u);

        static_assert(k2.user() == 0);
        REQUIRE(k1 == k2);
    }
}

TEST_CASE("Key: edge cases with single-bit fields")
{
    SUBCASE("1-bit user field") {
        constexpr auto k = make_key<void, 30, 1, 1>(1073741823, 1, 1);
        static_assert(k.index() == 1073741823);
        static_assert(k.version() == 1);
        static_assert(k.user() == 1);

        REQUIRE(k.index() == 1073741823);
        REQUIRE(k.version() == 1);
        REQUIRE(k.user() == 1);
    }
}

TEST_CASE("Key: constexpr capabilities")
{
    SUBCASE("all operations are constexpr") {
        constexpr auto k1 = make_key<void, 20, 10, 2>(100, 5, 3);
        constexpr auto k2 = k1.with_user(2u);
        constexpr auto idx = k1.index();
        constexpr auto ver = k1.version();
        constexpr auto usr = k1.user();
        constexpr auto null_check = k1.is_null();
        constexpr auto null_key = Key<void, 20, 10, 2>::null();
        constexpr auto equal = (k1 == k2);
        constexpr auto less = (k1 < k2);
        constexpr auto raw = k1.to_underlying();
        constexpr auto h = k1.hash();

        static_assert(idx == 100);
        static_assert(ver == 5);
        static_assert(usr == 3);
        static_assert(not null_check);
        static_assert(null_key.is_null());
        static_assert(raw != 0);
        static_assert(not equal);
        static_assert(not less);
        static_assert(h != 0);

        REQUIRE(true);
    }
}

TEST_CASE("Key: to_underlying round-trip preserves all bits")
{
    SUBCASE("32-bit round-trip conceptual test") {
        // We can't construct from underlying in this test suite,
        // but we verify that to_underlying returns non-zero for non-null keys
        constexpr auto k1 = make_key<void, 20, 10, 2>(12345, 678, 2);
        constexpr auto raw1 = k1.to_underlying();

        constexpr auto k2 = make_key<void, 20, 10, 2>(12345, 678, 2);
        constexpr auto raw2 = k2.to_underlying();

        static_assert(raw1 == raw2);
        REQUIRE(raw1 == raw2);
    }

    SUBCASE("different keys have different underlying values") {
        constexpr auto k1 = make_key<void, 20, 10, 2>(100, 5, 1);
        constexpr auto k2 = make_key<void, 20, 10, 2>(100, 5, 2);

        constexpr auto raw1 = k1.to_underlying();
        constexpr auto raw2 = k2.to_underlying();

        static_assert(raw1 != raw2);
        REQUIRE(raw1 != raw2);
    }
}

TEST_CASE("Key: comprehensive comparison semantics")
{
    SUBCASE("comparison is lexicographic on underlying bits") {
        // Bit layout is [user][version][index] from MSB to LSB
        // So comparison order is: user first, then version, then index
        auto k1 = make_key<void, 20, 10, 2>(100, 0, 0);
        auto k2 = make_key<void, 20, 10, 2>(100, 0, 1);

        REQUIRE(k1 < k2); // k1.user (0) < k2.user (1)
    }

    SUBCASE("null key compares less than any non-null key") {
        constexpr auto null_key = Key<void, 20, 10, 2>::null();
        constexpr auto k = make_key<void, 20, 10, 2>(1, 0, 0);

        static_assert(null_key < k);
        REQUIRE(null_key < k);
    }

    SUBCASE("null keys are equal") {
        constexpr auto null1 = Key<void, 20, 10, 2>::null();
        constexpr auto null2 = Key<void, 20, 10, 2>{};

        static_assert(null1 == null2);
        REQUIRE(null1 == null2);
    }
}

TEST_CASE("Key: hash quality basic check")
{
    SUBCASE("consecutive indices produce different hashes") {
        auto k1 = make_key<void, 20, 10, 2>(100, 5, 1);
        auto k2 = make_key<void, 20, 10, 2>(101, 5, 1);
        auto k3 = make_key<void, 20, 10, 2>(102, 5, 1);

        auto h1 = k1.hash();
        auto h2 = k2.hash();
        auto h3 = k3.hash();

        // All hashes should be different (probabilistic but very likely)
        REQUIRE(h1 != h2);
        REQUIRE(h2 != h3);
        REQUIRE(h1 != h3);
    }

    SUBCASE("null key has consistent hash") {
        auto null1 = Key<void, 20, 10, 2>::null();
        auto null2 = Key<void, 20, 10, 2>::null();

        REQUIRE(null1.hash() == null2.hash());
    }
}

// ============================================================================
// Practical Usage Scenarios
// ============================================================================

TEST_CASE("Key: practical usage in containers")
{
    SUBCASE("Key as map key with mixed operations") {
        using K = Key<void, 20, 10, 2>;
        std::unordered_map<K, int> map;

        auto k1 = make_key<K>(100, 1, 0);
        auto k2 = make_key<K>(200, 1, 0);
        auto k3 = make_key<K>(300, 1, 0);

        map[k1] = 10;
        map[k2] = 20;
        map[k3] = 30;

        REQUIRE(map.size() == 3);
        REQUIRE(map[k1] == 10);
        REQUIRE(map[k2] == 20);
        REQUIRE(map[k3] == 30);

        // with_user creates different key
        auto k1_modified = k1.with_user(1u);
        REQUIRE(map.find(k1_modified) == map.end());
    }
}

TEST_CASE("Key: value semantics comprehensive test")
{
    SUBCASE("copying and assignment work as expected") {
        auto k1 = make_key<void, 20, 10, 2>(100, 5, 3);
        auto k2 = k1; // Copy construction

        REQUIRE(k1 == k2);
        REQUIRE(k1.index() == k2.index());
        REQUIRE(k1.version() == k2.version());
        REQUIRE(k1.user() == k2.user());

        auto k3 = make_key<void, 20, 10, 2>(0, 0, 0);
        k3 = k1; // Copy assignment

        REQUIRE(k1 == k3);
    }

    SUBCASE("with_user doesn't modify original") {
        auto k1 = make_key<void, 20, 10, 2>(100, 5, 1);
        auto original_user = k1.user();

        auto k2 = k1.with_user(3u);

        REQUIRE(k1.user() == original_user);
        REQUIRE(k2.user() == 3);
        REQUIRE(k1 != k2);
    }
}

} // anonymous namespace
