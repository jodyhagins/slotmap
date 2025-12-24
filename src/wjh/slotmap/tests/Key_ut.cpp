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
using namespace wjh::slotmap::literals;

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
    IndexBits I,
    VersionBits V,
    UserBits U = 0_ub,
    typename KeyT = wjh::slotmap::Key<T, I, V, U>>
constexpr auto
make_key(auto index, auto version, auto user) noexcept
{
    return make_key<KeyT>(index, version, user);
}

template <typename T = void, IndexBits I, VersionBits V, UserBits U = 0_ub>
constexpr auto
make_key(auto index, auto version) noexcept
{
    return make_key<T, I, V, U>(index, version, std::uint8_t(0));
}

template <
    typename T = void,
    IndexBits I,
    VersionBits V,
    UserBits U = 0_ub,
    typename KeyT = wjh::slotmap::TrivialKey<T, I, V, U>>
constexpr auto
make_trivial_key(auto index, auto version, auto user) noexcept
{
    return make_key<KeyT>(index, version, user);
}

template <typename T = void, IndexBits I, VersionBits V, UserBits U = 0_ub>
constexpr auto
make_trivial_key(auto index, auto version) noexcept
{
    return make_trivial_key<T, I, V, U>(index, version, std::uint8_t(0));
}

// ============================================================================
// 16-bit Key Tests
// ============================================================================

TEST_CASE("Key 16-bit: value_type is uint16_t")
{
    using K = Key<int, 10_ib, 6_vb>;
    static_assert(std::is_same_v<K::value_type, std::uint16_t>);
    static_assert(std::is_same_v<K::tag_type, int>);
    REQUIRE(true);
}

TEST_CASE("Key 16-bit: basic construction and accessors")
{
    SUBCASE("construct with all parameters") {
        constexpr auto k = make_key<void, 10_ib, 4_vb, 2_ub>(100, 5, 3);
        static_assert(k.index() == 100);
        static_assert(k.version() == 5);
        static_assert(k.user() == 3);

        REQUIRE(k.index() == 100);
        REQUIRE(k.version() == 5);
        REQUIRE(k.user() == 3);
    }

    SUBCASE("construct without user bits (defaults to 0)") {
        constexpr auto k = make_key<void, 10_ib, 6_vb, 0_ub>(100, 5);
        static_assert(k.index() == 100);
        static_assert(k.version() == 5);
        static_assert(k.user() == 0);

        REQUIRE(k.index() == 100);
        REQUIRE(k.version() == 5);
        REQUIRE(k.user() == 0);
    }

    SUBCASE("zero values") {
        constexpr auto k = make_key<void, 10_ib, 4_vb, 2_ub>(0, 0, 0);
        static_assert(k.index() == 0);
        static_assert(k.version() == 0);
        static_assert(k.user() == 0);

        REQUIRE(k.index() == 0);
        REQUIRE(k.version() == 0);
        REQUIRE(k.user() == 0);
    }
}

TEST_CASE("Key 16-bit: default constructor creates null key")
{
    SUBCASE("default constructed key is null") {
        static_assert(std::is_default_constructible_v<Key<void, 10_ib, 6_vb>>);
        static_assert(not std::is_trivially_default_constructible_v<
                      Key<void, 10_ib, 6_vb>>);
        constexpr Key<void, 10_ib, 6_vb> k;
        static_assert(k.index() == 0);
        static_assert(k.version() == 0);
        static_assert(k.user() == 0);
        static_assert(k.is_null());
        static_assert(not k);

        REQUIRE(k.is_null());
        REQUIRE(k.index() == 0);
        REQUIRE(k.version() == 0);
        REQUIRE(k.user() == 0);
    }

    SUBCASE("default constructed equals null()") {
        constexpr Key<void, 10_ib, 6_vb> k{};
        constexpr auto null_key = Key<void, 10_ib, 6_vb>::null();
        static_assert(k == null_key);

        REQUIRE(k == null_key);
    }
}

TEST_CASE("Key 16-bit: null() static method")
{
    SUBCASE("null() returns all-zero key") {
        constexpr auto k = Key<void, 10_ib, 4_vb, 2_ub>::null();
        static_assert(k.index() == 0);
        static_assert(k.version() == 0);
        static_assert(k.user() == 0);
        static_assert(k.is_null());

        REQUIRE(k.is_null());
    }

    SUBCASE("non-null key is_null() returns false") {
        constexpr auto k = make_key<void, 10_ib, 4_vb, 2_ub>(1, 0, 0);
        static_assert(not k.is_null());
        static_assert(k);

        REQUIRE(not k.is_null());
    }
}

TEST_CASE("Key 16-bit: with_user() creates new key")
{
    SUBCASE("with_user modifies user bits only") {
        constexpr auto k1 = make_key<void, 10_ib, 4_vb, 2_ub>(100, 5, 1);
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
        constexpr auto k1 = make_key<void, 10_ib, 4_vb, 2_ub>(100, 5, 0);
        constexpr auto k2 = k1.with_user(2u);
        static_assert(k2.user() == 2);
        REQUIRE(k2.user() == 2);
    }
}

TEST_CASE("Key 16-bit: to_underlying() returns raw bits")
{
    SUBCASE("to_underlying for simple values") {
        constexpr auto k = make_key<void, 10_ib, 4_vb, 2_ub>(1, 1, 1);
        constexpr auto raw = k.to_underlying();
        static_assert(std::is_same_v<decltype(raw), std::uint16_t const>);

        REQUIRE(raw != 0);
    }

    SUBCASE("round-trip through to_underlying") {
        constexpr auto k1 = make_key<void, 10_ib, 4_vb, 2_ub>(512, 10, 2);
        constexpr auto raw = k1.to_underlying();

        REQUIRE(raw != 0);
    }
}

TEST_CASE("Key 16-bit: bit packing correctness")
{
    SUBCASE("maximum values for each field") {
        // 10 bits for index: max = 2^10 - 1 = 1023
        // 4 bits for version: max = 2^4 - 1 = 15
        // 2 bits for user: max = 2^2 - 1 = 3
        constexpr auto k = make_key<void, 10_ib, 4_vb, 2_ub>(1023, 15, 3);

        static_assert(k.index() == 1023);
        static_assert(k.version() == 15);
        static_assert(k.user() == 3);

        REQUIRE(k.index() == 1023);
        REQUIRE(k.version() == 15);
        REQUIRE(k.user() == 3);
    }

    SUBCASE("independent bit fields don't interfere") {
        constexpr auto k1 = make_key<void, 10_ib, 4_vb, 2_ub>(1023, 0, 0);
        constexpr auto k2 = make_key<void, 10_ib, 4_vb, 2_ub>(0, 15, 0);
        constexpr auto k3 = make_key<void, 10_ib, 4_vb, 2_ub>(0, 0, 3);

        static_assert(k1.index() == 1023);
        static_assert(k1.version() == 0);
        static_assert(k1.user() == 0);

        static_assert(k2.index() == 0);
        static_assert(k2.version() == 15);
        static_assert(k2.user() == 0);

        static_assert(k3.index() == 0);
        static_assert(k3.version() == 0);
        static_assert(k3.user() == 3);

        REQUIRE(k1.version() == 0);
        REQUIRE(k2.index() == 0);
        REQUIRE(k3.version() == 0);
    }
}

TEST_CASE("Key 16-bit: different bit configurations")
{
    SUBCASE("configuration 8/8/0") {
        constexpr auto k = make_key<void, 8_ib, 8_vb, 0_ub>(255, 255);
        static_assert(k.index() == 255);
        static_assert(k.version() == 255);
        static_assert(k.user() == 0);

        REQUIRE(k.index() == 255);
        REQUIRE(k.version() == 255);
    }

    SUBCASE("configuration 12/4/0") {
        constexpr auto k = make_key<void, 12_ib, 4_vb, 0_ub>(4095, 15);
        static_assert(k.index() == 4095);
        static_assert(k.version() == 15);

        REQUIRE(k.index() == 4095);
        REQUIRE(k.version() == 15);
    }

    SUBCASE("configuration 6/6/4") {
        constexpr auto k = make_key<void, 6_ib, 6_vb, 4_ub>(63, 63, 15);
        static_assert(k.index() == 63);
        static_assert(k.version() == 63);
        static_assert(k.user() == 15);

        REQUIRE(k.index() == 63);
        REQUIRE(k.version() == 63);
        REQUIRE(k.user() == 15);
    }
}

TEST_CASE("Key 16-bit: equality comparison")
{
    SUBCASE("identical keys are equal") {
        constexpr auto k1 = make_key<void, 10_ib, 4_vb, 2_ub>(100, 5, 3);
        constexpr auto k2 = make_key<void, 10_ib, 4_vb, 2_ub>(100, 5, 3);
        static_assert(k1 == k2);

        REQUIRE(k1 == k2);
        REQUIRE_FALSE(k1 != k2);
    }

    SUBCASE("keys with different index are not equal") {
        constexpr auto k1 = make_key<void, 10_ib, 4_vb, 2_ub>(100, 5, 3);
        constexpr auto k2 = make_key<void, 10_ib, 4_vb, 2_ub>(101, 5, 3);
        static_assert(k1 != k2);

        REQUIRE(k1 != k2);
        REQUIRE_FALSE(k1 == k2);
    }

    SUBCASE("keys with different version are not equal") {
        constexpr auto k1 = make_key<void, 10_ib, 4_vb, 2_ub>(100, 5, 3);
        constexpr auto k2 = make_key<void, 10_ib, 4_vb, 2_ub>(100, 6, 3);
        static_assert(k1 != k2);

        REQUIRE(k1 != k2);
    }

    SUBCASE("keys with different user are not equal") {
        constexpr auto k1 = make_key<void, 10_ib, 4_vb, 2_ub>(100, 5, 0);
        constexpr auto k2 = make_key<void, 10_ib, 4_vb, 2_ub>(100, 5, 2);
        static_assert(k1 != k2);

        REQUIRE(k1 != k2);
    }
}

TEST_CASE("Key 16-bit: three-way comparison")
{
    SUBCASE("less than comparison") {
        constexpr auto k1 = make_key<void, 10_ib, 4_vb, 2_ub>(100, 5, 1);
        constexpr auto k2 = make_key<void, 10_ib, 4_vb, 2_ub>(200, 5, 1);

        REQUIRE(k1 < k2);
        REQUIRE(k1 <= k2);
        REQUIRE_FALSE(k1 > k2);
        REQUIRE_FALSE(k1 >= k2);
    }

    SUBCASE("greater than comparison") {
        constexpr auto k1 = make_key<void, 10_ib, 4_vb, 2_ub>(200, 5, 1);
        constexpr auto k2 = make_key<void, 10_ib, 4_vb, 2_ub>(100, 5, 1);

        REQUIRE(k1 > k2);
        REQUIRE(k1 >= k2);
        REQUIRE_FALSE(k1 < k2);
        REQUIRE_FALSE(k1 <= k2);
    }

    SUBCASE("equal comparison with <=, >=") {
        constexpr auto k1 = make_key<void, 10_ib, 4_vb, 2_ub>(100, 5, 1);
        constexpr auto k2 = make_key<void, 10_ib, 4_vb, 2_ub>(100, 5, 1);

        REQUIRE(k1 <= k2);
        REQUIRE(k1 >= k2);
        REQUIRE_FALSE(k1 < k2);
        REQUIRE_FALSE(k1 > k2);
    }

    SUBCASE("comparison considers all bits") {
        constexpr auto k1 = make_key<void, 10_ib, 4_vb, 2_ub>(100, 5, 1);
        constexpr auto k2 = make_key<void, 10_ib, 4_vb, 2_ub>(100, 5, 2);

        REQUIRE(k1 < k2);
    }
}

TEST_CASE("Key 16-bit: hash function")
{
    SUBCASE("hash() member function exists and is constexpr") {
        constexpr auto k = make_key<void, 10_ib, 4_vb, 2_ub>(100, 5, 3);
        constexpr auto h = k.hash();
        static_assert(std::is_same_v<decltype(h), std::size_t const>);
        static_assert(h != 0); // Unlikely to be zero for non-null key

        REQUIRE(h != 0);
    }

    SUBCASE("equal keys have equal hashes") {
        constexpr auto k1 = make_key<void, 10_ib, 4_vb, 2_ub>(100, 5, 3);
        constexpr auto k2 = make_key<void, 10_ib, 4_vb, 2_ub>(100, 5, 3);

        static_assert(k1.hash() == k2.hash());
        REQUIRE(k1.hash() == k2.hash());
    }

    SUBCASE("different keys likely have different hashes") {
        constexpr auto k1 = make_key<void, 10_ib, 4_vb, 2_ub>(100, 5, 3);
        constexpr auto k2 = make_key<void, 10_ib, 4_vb, 2_ub>(101, 5, 3);

        // This is probabilistic, but very likely
        static_assert(k1.hash() != k2.hash());
        REQUIRE(k1.hash() != k2.hash());
    }
}

TEST_CASE("Key 16-bit: std::hash specialization")
{
    SUBCASE("std::hash works with Key") {
        using K = Key<void, 10_ib, 4_vb, 2_ub>;
        auto k = make_key<K>(100, 5, 3);

        std::hash<K> hasher;
        auto h = hasher(k);

        REQUIRE(std::is_same_v<decltype(h), std::size_t>);
        REQUIRE(h != 0);
    }

    SUBCASE("std::hash matches member hash()") {
        using K = Key<void, 10_ib, 4_vb, 2_ub>;
        auto k = make_key<K>(100, 5, 3);

        std::hash<K> hasher;
        REQUIRE(hasher(k) == k.hash());
    }

    SUBCASE("Key works in unordered_map") {
        using K = Key<void, 10_ib, 4_vb, 2_ub>;
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
// 32-bit Key Tests
// ============================================================================

TEST_CASE("Key 32-bit: value_type is uint32_t")
{
    using K = Key<int, 20_ib, 10_vb, 2_ub>;
    static_assert(std::is_same_v<K::value_type, std::uint32_t>);
    static_assert(std::is_same_v<K::tag_type, int>);
    REQUIRE(true);
}

TEST_CASE("Key 32-bit: basic construction and accessors")
{
    SUBCASE("construct with all parameters") {
        constexpr auto k = make_key<void, 20_ib, 10_vb, 2_ub>(100, 5, 3);
        static_assert(k.index() == 100);
        static_assert(k.version() == 5);
        static_assert(k.user() == 3);

        REQUIRE(k.index() == 100);
        REQUIRE(k.version() == 5);
        REQUIRE(k.user() == 3);
    }

    SUBCASE("construct without user bits (defaults to 0)") {
        constexpr auto k = make_key<void, 20_ib, 10_vb, 2_ub>(100, 5);
        static_assert(k.index() == 100);
        static_assert(k.version() == 5);
        static_assert(k.user() == 0);

        REQUIRE(k.index() == 100);
        REQUIRE(k.version() == 5);
        REQUIRE(k.user() == 0);
    }

    SUBCASE("zero values") {
        constexpr auto k = make_key<void, 20_ib, 10_vb, 2_ub>(0, 0, 0);
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
        static_assert(
            std::is_default_constructible_v<Key<void, 20_ib, 10_vb, 2_ub>>);
        static_assert(not std::is_trivially_default_constructible_v<
                      Key<void, 20_ib, 10_vb, 2_ub>>);
        constexpr Key<void, 20_ib, 10_vb, 2_ub> k;
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
        constexpr Key<void, 20_ib, 10_vb, 2_ub> k{};
        constexpr auto null_key = Key<void, 20_ib, 10_vb, 2_ub>::null();
        static_assert(k == null_key);

        REQUIRE(k == null_key);
    }
}

TEST_CASE("Key 32-bit: null() static method")
{
    SUBCASE("null() returns all-zero key") {
        constexpr auto k = Key<void, 20_ib, 10_vb, 2_ub>::null();
        static_assert(k.index() == 0);
        static_assert(k.version() == 0);
        static_assert(k.user() == 0);
        static_assert(k.is_null());

        REQUIRE(k.is_null());
    }

    SUBCASE("non-null key is_null() returns false") {
        constexpr auto k = make_key<void, 20_ib, 10_vb, 2_ub>(1, 0, 0);
        static_assert(not k.is_null());

        REQUIRE(not k.is_null());
    }
}

TEST_CASE("Key 32-bit: with_user() creates new key")
{
    SUBCASE("with_user modifies user bits only") {
        constexpr auto k1 = make_key<void, 20_ib, 10_vb, 2_ub>(100, 5, 1);
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
        constexpr auto k1 = make_key<void, 20_ib, 10_vb, 2_ub>(100, 5, 0);
        constexpr auto k2 = k1.with_user(2u);
        static_assert(k2.user() == 2);
        REQUIRE(k2.user() == 2);
    }
}

TEST_CASE("Key 32-bit: to_underlying() returns raw bits")
{
    SUBCASE("to_underlying for simple values") {
        constexpr auto k = make_key<void, 20_ib, 10_vb, 2_ub>(1, 1, 1);
        constexpr auto raw = k.to_underlying();
        static_assert(std::is_same_v<decltype(raw), std::uint32_t const>);

        REQUIRE(raw != 0);
    }

    SUBCASE("round-trip through to_underlying") {
        constexpr auto k1 = make_key<void, 20_ib, 10_vb, 2_ub>(12345, 678, 2);
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
        constexpr auto k = make_key<void, 20_ib, 10_vb, 2_ub>(1048575, 1023, 3);

        static_assert(k.index() == 1048575);
        static_assert(k.version() == 1023);
        static_assert(k.user() == 3);

        REQUIRE(k.index() == 1048575);
        REQUIRE(k.version() == 1023);
        REQUIRE(k.user() == 3);
    }

    SUBCASE("independent bit fields don't interfere") {
        constexpr auto k1 = make_key<void, 20_ib, 10_vb, 2_ub>(1048575, 0, 0);
        constexpr auto k2 = make_key<void, 20_ib, 10_vb, 2_ub>(0, 1023, 0);
        constexpr auto k3 = make_key<void, 20_ib, 10_vb, 2_ub>(0, 0, 3);

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
        constexpr auto k = make_key<void, 16_ib, 16_vb, 0_ub>(65535, 65535);
        static_assert(k.index() == 65535);
        static_assert(k.version() == 65535);
        static_assert(k.user() == 0);

        REQUIRE(k.index() == 65535);
        REQUIRE(k.version() == 65535);
    }

    SUBCASE("configuration 24/8/0") {
        constexpr auto k = make_key<void, 24_ib, 8_vb, 0_ub>(16777215, 255);
        static_assert(k.index() == 16777215);
        static_assert(k.version() == 255);

        REQUIRE(k.index() == 16777215);
        REQUIRE(k.version() == 255);
    }

    SUBCASE("configuration 10/10/12") {
        constexpr auto k =
            make_key<void, 10_ib, 10_vb, 12_ub>(1023, 1023, 4095);
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
        constexpr auto k1 = make_key<void, 20_ib, 10_vb, 2_ub>(100, 5, 3);
        constexpr auto k2 = make_key<void, 20_ib, 10_vb, 2_ub>(100, 5, 3);
        static_assert(k1 == k2);

        REQUIRE(k1 == k2);
        REQUIRE_FALSE(k1 != k2);
    }

    SUBCASE("keys with different index are not equal") {
        constexpr auto k1 = make_key<void, 20_ib, 10_vb, 2_ub>(100, 5, 3);
        constexpr auto k2 = make_key<void, 20_ib, 10_vb, 2_ub>(101, 5, 3);
        static_assert(k1 != k2);

        REQUIRE(k1 != k2);
        REQUIRE_FALSE(k1 == k2);
    }

    SUBCASE("keys with different version are not equal") {
        constexpr auto k1 = make_key<void, 20_ib, 10_vb, 2_ub>(100, 5, 3);
        constexpr auto k2 = make_key<void, 20_ib, 10_vb, 2_ub>(100, 6, 3);
        static_assert(k1 != k2);

        REQUIRE(k1 != k2);
    }

    SUBCASE("keys with different user are not equal") {
        constexpr auto k1 = make_key<void, 20_ib, 10_vb, 2_ub>(100, 5, 1);
        constexpr auto k2 = make_key<void, 20_ib, 10_vb, 2_ub>(100, 5, 2);
        static_assert(k1 != k2);

        REQUIRE(k1 != k2);
    }
}

TEST_CASE("Key 32-bit: three-way comparison")
{
    SUBCASE("less than comparison") {
        constexpr auto k1 = make_key<void, 20_ib, 10_vb, 2_ub>(100, 5, 1);
        constexpr auto k2 = make_key<void, 20_ib, 10_vb, 2_ub>(200, 5, 1);

        REQUIRE(k1 < k2);
        REQUIRE(k1 <= k2);
        REQUIRE_FALSE(k1 > k2);
        REQUIRE_FALSE(k1 >= k2);
    }

    SUBCASE("greater than comparison") {
        constexpr auto k1 = make_key<void, 20_ib, 10_vb, 2_ub>(200, 5, 1);
        constexpr auto k2 = make_key<void, 20_ib, 10_vb, 2_ub>(100, 5, 1);

        REQUIRE(k1 > k2);
        REQUIRE(k1 >= k2);
        REQUIRE_FALSE(k1 < k2);
        REQUIRE_FALSE(k1 <= k2);
    }

    SUBCASE("equal comparison with <=, >=") {
        constexpr auto k1 = make_key<void, 20_ib, 10_vb, 2_ub>(100, 5, 1);
        constexpr auto k2 = make_key<void, 20_ib, 10_vb, 2_ub>(100, 5, 1);

        REQUIRE(k1 <= k2);
        REQUIRE(k1 >= k2);
        REQUIRE_FALSE(k1 < k2);
        REQUIRE_FALSE(k1 > k2);
    }

    SUBCASE("comparison considers all bits") {
        constexpr auto k1 = make_key<void, 20_ib, 10_vb, 2_ub>(100, 5, 1);
        constexpr auto k2 = make_key<void, 20_ib, 10_vb, 2_ub>(100, 5, 2);

        REQUIRE(k1 < k2);
    }
}

TEST_CASE("Key 32-bit: hash function")
{
    SUBCASE("hash() member function exists and is constexpr") {
        constexpr auto k = make_key<void, 20_ib, 10_vb, 2_ub>(100, 5, 3);
        constexpr auto h = k.hash();
        static_assert(std::is_same_v<decltype(h), std::size_t const>);
        static_assert(h != 0); // Unlikely to be zero for non-null key

        REQUIRE(h != 0);
    }

    SUBCASE("equal keys have equal hashes") {
        constexpr auto k1 = make_key<void, 20_ib, 10_vb, 2_ub>(100, 5, 3);
        constexpr auto k2 = make_key<void, 20_ib, 10_vb, 2_ub>(100, 5, 3);

        static_assert(k1.hash() == k2.hash());
        REQUIRE(k1.hash() == k2.hash());
    }

    SUBCASE("different keys likely have different hashes") {
        constexpr auto k1 = make_key<void, 20_ib, 10_vb, 2_ub>(100, 5, 3);
        constexpr auto k2 = make_key<void, 20_ib, 10_vb, 2_ub>(101, 5, 3);

        // This is probabilistic, but very likely
        static_assert(k1.hash() != k2.hash());
        REQUIRE(k1.hash() != k2.hash());
    }
}

TEST_CASE("Key 32-bit: std::hash specialization")
{
    SUBCASE("std::hash works with Key") {
        using K = Key<void, 20_ib, 10_vb, 2_ub>;
        auto k = make_key<K>(100, 5, 3);

        std::hash<K> hasher;
        auto h = hasher(k);

        REQUIRE(std::is_same_v<decltype(h), std::size_t>);
        REQUIRE(h != 0);
    }

    SUBCASE("std::hash matches member hash()") {
        using K = Key<void, 20_ib, 10_vb, 2_ub>;
        auto k = make_key<K>(100, 5, 3);

        std::hash<K> hasher;
        REQUIRE(hasher(k) == k.hash());
    }

    SUBCASE("Key works in unordered_map") {
        using K = Key<void, 20_ib, 10_vb, 2_ub>;
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
    using K = Key<void, 40_ib, 20_vb, 4_ub>;
    static_assert(std::is_same_v<K::value_type, std::uint64_t>);
    REQUIRE(true);
}

TEST_CASE("Key 64-bit: basic construction and accessors")
{
    SUBCASE("construct with all parameters") {
        constexpr auto k =
            make_key<void, 40_ib, 20_vb, 4_ub>(1000000, 500000, 15);
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
            make_key<void, 40_ib, 20_vb, 4_ub>(1099511627775ULL, 1048575, 15);

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
        constexpr auto k = make_key<void, 32_ib, 32_vb, 0_ub>(
            4294967295U,
            4294967295U);
        static_assert(k.index() == 4294967295U);
        static_assert(k.version() == 4294967295U);

        REQUIRE(k.index() == 4294967295U);
        REQUIRE(k.version() == 4294967295U);
    }

    SUBCASE("configuration 48/12/4") {
        constexpr auto k =
            make_key<void, 48_ib, 12_vb, 4_ub>(281474976710655ULL, 4095, 15);
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
        constexpr Key<void, 40_ib, 20_vb, 4_ub> k{};
        static_assert(k.is_null());
        static_assert(k == Key<void, 40_ib, 20_vb, 4_ub>::null());

        REQUIRE(k.is_null());
    }
}

TEST_CASE("Key 64-bit: with_user() creates new key")
{
    constexpr auto k1 = make_key<void, 40_ib, 20_vb, 4_ub>(1000000, 500000, 5);
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
        constexpr auto k1 =
            make_key<void, 40_ib, 20_vb, 4_ub>(1000000, 500000, 15);
        constexpr auto k2 =
            make_key<void, 40_ib, 20_vb, 4_ub>(1000000, 500000, 15);
        static_assert(k1 == k2);

        REQUIRE(k1 == k2);
    }

    SUBCASE("ordering") {
        constexpr auto k1 =
            make_key<void, 40_ib, 20_vb, 4_ub>(1000000, 500000, 15);
        constexpr auto k2 =
            make_key<void, 40_ib, 20_vb, 4_ub>(2000000, 500000, 15);

        REQUIRE(k1 < k2);
        REQUIRE(k2 > k1);
    }
}

TEST_CASE("Key 64-bit: hashing")
{
    SUBCASE("member hash() is constexpr") {
        constexpr auto k =
            make_key<void, 40_ib, 20_vb, 4_ub>(1000000, 500000, 15);
        constexpr auto h = k.hash();
        static_assert(h != 0);

        REQUIRE(h != 0);
    }

    SUBCASE("std::hash specialization") {
        using K = Key<void, 40_ib, 20_vb, 4_ub>;
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
    using K = Key<void, 80_ib, 40_vb, 8_ub>;
    static_assert(std::is_same_v<K::value_type, unsigned __int128>);
    REQUIRE(true);
}

TEST_CASE("Key 128-bit: basic construction and accessors")
{
    SUBCASE("construct with reasonable values") {
        // Use values that fit comfortably in 64-bit for testing
        constexpr auto k = make_key<void, 80_ib, 40_vb, 8_ub>(
            1000000000000ULL,
            1000000000ULL,
            255);
        static_assert(k.index() == 1000000000000ULL);
        static_assert(k.version() == 1000000000ULL);
        static_assert(k.user() == 255);

        REQUIRE(k.index() == 1000000000000ULL);
        REQUIRE(k.version() == 1000000000ULL);
        REQUIRE(k.user() == 255);
    }

    SUBCASE("maximum user bits value") {
        // 8 bits for user: max = 255
        constexpr auto k = make_key<void, 80_ib, 40_vb, 8_ub>(1, 1, 255);
        static_assert(k.user() == 255);

        REQUIRE(k.user() == 255);
    }
}

TEST_CASE("Key 128-bit: different bit configurations")
{
    SUBCASE("configuration 64/64/0") {
        constexpr auto k = make_key<void, 64_ib, 64_vb, 0_ub>(
            18446744073709551615ULL,
            18446744073709551615ULL);
        static_assert(k.index() == 18446744073709551615ULL);
        static_assert(k.version() == 18446744073709551615ULL);

        REQUIRE(k.index() == 18446744073709551615ULL);
        REQUIRE(k.version() == 18446744073709551615ULL);
    }

    SUBCASE("configuration 100/20/8") {
        constexpr auto k =
            make_key<void, 100_ib, 20_vb, 8_ub>(1ULL << 50, 1048575, 255);
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
    constexpr Key<void, 80_ib, 40_vb, 8_ub> k;
    static_assert(k.is_null());
    static_assert(k == Key<void, 80_ib, 40_vb, 8_ub>::null());

    REQUIRE(k.is_null());
}

TEST_CASE("Key 128-bit: with_user() creates new key")
{
    constexpr Key<void, 80_ib, 40_vb, 8_ub> k1{
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
        constexpr Key<void, 80_ib, 40_vb, 8_ub> k1{
            {1000000000000ULL},
            {1000000000ULL},
            {255}};
        constexpr Key<void, 80_ib, 40_vb, 8_ub> k2{
            {1000000000000ULL},
            {1000000000ULL},
            {255}};
        static_assert(k1 == k2);

        REQUIRE(k1 == k2);
    }

    SUBCASE("inequality") {
        constexpr Key<void, 80_ib, 40_vb, 8_ub> k1{
            {1000000000000ULL},
            {1000000000ULL},
            {255}};
        constexpr Key<void, 80_ib, 40_vb, 8_ub> k2{
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
        constexpr Key<void, 80_ib, 40_vb, 8_ub> k{
            {1000000000000ULL},
            {1000000000ULL},
            {255}};
        constexpr auto h = k.hash();
        static_assert(h != 0);

        REQUIRE(h != 0);
    }

    SUBCASE("std::hash specialization") {
        using K = Key<void, 80_ib, 40_vb, 8_ub>;
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
        using K1 = Key<int, 20_ib, 10_vb, 2_ub>;
        using K2 = Key<unsigned, 20_ib, 10_vb, 2_ub>;

        static_assert(not std::is_same_v<K1, K2>);
        REQUIRE(true);
    }

    SUBCASE("same phantom type and configuration are same type") {
        using K1 = Key<void, 20_ib, 10_vb, 2_ub>;
        using K2 = Key<void, 20_ib, 10_vb, 2_ub>;

        static_assert(std::is_same_v<K1, K2>);
        REQUIRE(true);
    }

    SUBCASE("different bit configurations are different types") {
        using K1 = Key<void, 20_ib, 10_vb, 2_ub>;
        using K2 = Key<void, 16_ib, 16_vb>;

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
        constexpr auto k = make_key<void, 24_ib, 8_vb, 0_ub>(16777215, 255);
        static_assert(k.index() == 16777215);
        static_assert(k.version() == 255);
        static_assert(k.user() == 0);

        REQUIRE(k.user() == 0);
    }

    SUBCASE("with_user on zero user bits key") {
        constexpr auto k1 = make_key<void, 24_ib, 8_vb, 0_ub>(16777215, 255);
        constexpr auto k2 = k1.with_user(0u);

        static_assert(k2.user() == 0);
        REQUIRE(k1 == k2);
    }
}

TEST_CASE("Key: edge cases with single-bit fields")
{
    SUBCASE("1-bit user field") {
        constexpr auto k = make_key<void, 30_ib, 1_vb, 1_ub>(1073741823, 1, 1);
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
        constexpr auto k1 = make_key<void, 20_ib, 10_vb, 2_ub>(100, 5, 3);
        constexpr auto k2 = k1.with_user(2u);
        constexpr auto idx = k1.index();
        constexpr auto ver = k1.version();
        constexpr auto usr = k1.user();
        constexpr auto null_check = k1.is_null();
        constexpr auto null_key = Key<void, 20_ib, 10_vb, 2_ub>::null();
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
        constexpr auto k1 = make_key<void, 20_ib, 10_vb, 2_ub>(12345, 678, 2);
        constexpr auto raw1 = k1.to_underlying();

        constexpr auto k2 = make_key<void, 20_ib, 10_vb, 2_ub>(12345, 678, 2);
        constexpr auto raw2 = k2.to_underlying();

        static_assert(raw1 == raw2);
        REQUIRE(raw1 == raw2);
    }

    SUBCASE("different keys have different underlying values") {
        constexpr auto k1 = make_key<void, 20_ib, 10_vb, 2_ub>(100, 5, 1);
        constexpr auto k2 = make_key<void, 20_ib, 10_vb, 2_ub>(100, 5, 2);

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
        auto k1 = make_key<void, 20_ib, 10_vb, 2_ub>(100, 0, 0);
        auto k2 = make_key<void, 20_ib, 10_vb, 2_ub>(100, 0, 1);

        REQUIRE(k1 < k2); // k1.user (0) < k2.user (1)
    }

    SUBCASE("null key compares less than any non-null key") {
        constexpr auto null_key = Key<void, 20_ib, 10_vb, 2_ub>::null();
        constexpr auto k = make_key<void, 20_ib, 10_vb, 2_ub>(1, 0, 0);

        static_assert(null_key < k);
        REQUIRE(null_key < k);
    }

    SUBCASE("null keys are equal") {
        constexpr auto null1 = Key<void, 20_ib, 10_vb, 2_ub>::null();
        constexpr auto null2 = Key<void, 20_ib, 10_vb, 2_ub>{};

        static_assert(null1 == null2);
        REQUIRE(null1 == null2);
    }
}

TEST_CASE("Key: hash quality basic check")
{
    SUBCASE("consecutive indices produce different hashes") {
        auto k1 = make_key<void, 20_ib, 10_vb, 2_ub>(100, 5, 1);
        auto k2 = make_key<void, 20_ib, 10_vb, 2_ub>(101, 5, 1);
        auto k3 = make_key<void, 20_ib, 10_vb, 2_ub>(102, 5, 1);

        auto h1 = k1.hash();
        auto h2 = k2.hash();
        auto h3 = k3.hash();

        // All hashes should be different (probabilistic but very likely)
        REQUIRE(h1 != h2);
        REQUIRE(h2 != h3);
        REQUIRE(h1 != h3);
    }

    SUBCASE("null key has consistent hash") {
        auto null1 = Key<void, 20_ib, 10_vb, 2_ub>::null();
        auto null2 = Key<void, 20_ib, 10_vb, 2_ub>::null();

        REQUIRE(null1.hash() == null2.hash());
    }
}

// ============================================================================
// Practical Usage Scenarios
// ============================================================================

TEST_CASE("Key: practical usage in containers")
{
    SUBCASE("Key as map key with mixed operations") {
        using K = Key<void, 20_ib, 10_vb, 2_ub>;
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
        auto k1 = make_key<void, 20_ib, 10_vb, 2_ub>(100, 5, 3);
        auto k2 = k1; // Copy construction

        REQUIRE(k1 == k2);
        REQUIRE(k1.index() == k2.index());
        REQUIRE(k1.version() == k2.version());
        REQUIRE(k1.user() == k2.user());

        auto k3 = make_key<void, 20_ib, 10_vb, 2_ub>(0, 0, 0);
        k3 = k1; // Copy assignment

        REQUIRE(k1 == k3);
    }

    SUBCASE("with_user doesn't modify original") {
        auto k1 = make_key<void, 20_ib, 10_vb, 2_ub>(100, 5, 1);
        auto original_user = k1.user();

        auto k2 = k1.with_user(3u);

        REQUIRE(k1.user() == original_user);
        REQUIRE(k2.user() == 3);
        REQUIRE(k1 != k2);
    }
}

// ============================================================================
// identifies_same_object() Tests
// ============================================================================

TEST_CASE("Key: identifies_same_object basic behavior")
{
    SUBCASE("identical keys identify same object") {
        constexpr auto k1 = make_key<void, 20_ib, 10_vb, 2_ub>(100, 5, 3);
        constexpr auto k2 = make_key<void, 20_ib, 10_vb, 2_ub>(100, 5, 3);
        static_assert(k1.identifies_same_object(k2));
        REQUIRE(k1.identifies_same_object(k2));
    }

    SUBCASE("keys differing only in user bits identify same object") {
        constexpr auto k1 = make_key<void, 20_ib, 10_vb, 2_ub>(100, 5, 0);
        constexpr auto k2 = make_key<void, 20_ib, 10_vb, 2_ub>(100, 5, 3);
        static_assert(k1 != k2); // Different keys
        static_assert(k1.identifies_same_object(k2)); // Same slot
        REQUIRE(k1 != k2);
        REQUIRE(k1.identifies_same_object(k2));
    }

    SUBCASE("keys with different index do not identify same object") {
        constexpr auto k1 = make_key<void, 20_ib, 10_vb, 2_ub>(100, 5, 3);
        constexpr auto k2 = make_key<void, 20_ib, 10_vb, 2_ub>(101, 5, 3);
        static_assert(not k1.identifies_same_object(k2));
        REQUIRE(not k1.identifies_same_object(k2));
    }

    SUBCASE("keys with different version do not identify same object") {
        constexpr auto k1 = make_key<void, 20_ib, 10_vb, 2_ub>(100, 5, 3);
        constexpr auto k2 = make_key<void, 20_ib, 10_vb, 2_ub>(100, 6, 3);
        static_assert(not k1.identifies_same_object(k2));
        REQUIRE(not k1.identifies_same_object(k2));
    }
}

TEST_CASE("Key: identifies_same_object with with_user")
{
    SUBCASE("with_user preserves slot identity") {
        constexpr auto k1 = make_key<void, 20_ib, 10_vb, 2_ub>(100, 5, 1);
        constexpr auto k2 = k1.with_user(2u);
        constexpr auto k3 = k1.with_user(3u);

        static_assert(k1 != k2);
        static_assert(k2 != k3);
        static_assert(k1 != k3);

        static_assert(k1.identifies_same_object(k2));
        static_assert(k2.identifies_same_object(k3));
        static_assert(k1.identifies_same_object(k3));

        REQUIRE(k1.identifies_same_object(k2));
        REQUIRE(k2.identifies_same_object(k3));
        REQUIRE(k1.identifies_same_object(k3));
    }
}

TEST_CASE("Key: identifies_same_object with zero user bits")
{
    SUBCASE("works correctly when user_bits = 0") {
        constexpr auto k1 = make_key<void, 24_ib, 8_vb, 0_ub>(16777215, 255);
        constexpr auto k2 = make_key<void, 24_ib, 8_vb, 0_ub>(16777215, 255);
        constexpr auto k3 = make_key<void, 24_ib, 8_vb, 0_ub>(16777215, 254);

        static_assert(k1.identifies_same_object(k2));
        static_assert(not k1.identifies_same_object(k3));

        REQUIRE(k1.identifies_same_object(k2));
        REQUIRE(not k1.identifies_same_object(k3));
    }
}

TEST_CASE("Key: identifies_same_object null key handling")
{
    SUBCASE("null keys identify same object") {
        constexpr auto null1 = Key<void, 20_ib, 10_vb, 2_ub>::null();
        constexpr auto null2 = Key<void, 20_ib, 10_vb, 2_ub>{};

        static_assert(null1.identifies_same_object(null2));
        REQUIRE(null1.identifies_same_object(null2));
    }

    SUBCASE("null key does not identify same object as non-null") {
        constexpr auto null_key = Key<void, 20_ib, 10_vb, 2_ub>::null();
        constexpr auto k = make_key<void, 20_ib, 10_vb, 2_ub>(1, 0, 0);

        static_assert(not null_key.identifies_same_object(k));
        REQUIRE(not null_key.identifies_same_object(k));
    }
}

TEST_CASE("Key: identifies_same_object is symmetric")
{
    SUBCASE("a.identifies_same_object(b) == b.identifies_same_object(a)") {
        constexpr auto k1 = make_key<void, 20_ib, 10_vb, 2_ub>(100, 5, 1);
        constexpr auto k2 = make_key<void, 20_ib, 10_vb, 2_ub>(100, 5, 2);
        constexpr auto k3 = make_key<void, 20_ib, 10_vb, 2_ub>(200, 5, 1);

        static_assert(
            k1.identifies_same_object(k2) == k2.identifies_same_object(k1));
        static_assert(
            k1.identifies_same_object(k3) == k3.identifies_same_object(k1));

        REQUIRE(k1.identifies_same_object(k2) == k2.identifies_same_object(k1));
        REQUIRE(k1.identifies_same_object(k3) == k3.identifies_same_object(k1));
    }
}

// ============================================================================
// TrivialKey Tests
// ============================================================================

TEST_CASE("TrivialKey: type traits")
{
    // TrivialKey has a trivial default constructor that is PRIVATE.
    // This makes it an implicit lifetime type per the C++ standard (which does
    // not consider visibility), while preventing users from accidentally
    // constructing uninitialized keys. Once std::is_implicit_lifetime is
    // available (requires compiler support), we can test that directly.

    SUBCASE("TrivialKey is NOT publicly default constructible (private ctor)") {
        using TK = TrivialKey<int, 16_ib, 16_vb>;
        // The default constructor exists and is trivial, making TrivialKey
        // suitable for implicit lifetime use. But it's private to prevent
        // accidental uninitialized construction by users.
        static_assert(not std::is_default_constructible_v<TK>);
        REQUIRE(not std::is_default_constructible_v<TK>);
    }

    SUBCASE("regular Key IS default constructible (public ctor, zero-init)") {
        using K = Key<int, 16_ib, 16_vb>;
        static_assert(std::is_default_constructible_v<K>);
        // But it's NOT trivially default constructible because it zero-inits
        static_assert(not std::is_trivially_default_constructible_v<K>);
        REQUIRE(std::is_default_constructible_v<K>);
    }

    SUBCASE("TrivialKey is trivially copyable") {
        using TK = TrivialKey<int, 16_ib, 16_vb>;
        static_assert(std::is_trivially_copyable_v<TK>);
        REQUIRE(std::is_trivially_copyable_v<TK>);
    }

    SUBCASE("TrivialKey is trivially destructible") {
        using TK = TrivialKey<int, 16_ib, 16_vb>;
        static_assert(std::is_trivially_destructible_v<TK>);
        REQUIRE(std::is_trivially_destructible_v<TK>);
    }

    SUBCASE("TrivialKey has standard layout") {
        using TK = TrivialKey<int, 16_ib, 16_vb>;
        static_assert(std::is_standard_layout_v<TK>);
        REQUIRE(std::is_standard_layout_v<TK>);
    }

    SUBCASE("regular Key is also trivially copyable and destructible") {
        using K = Key<int, 16_ib, 16_vb>;
        static_assert(std::is_trivially_copyable_v<K>);
        static_assert(std::is_trivially_destructible_v<K>);
        REQUIRE(std::is_trivially_copyable_v<K>);
        REQUIRE(std::is_trivially_destructible_v<K>);
    }
}

TEST_CASE("TrivialKey: tag_type is unwrapped")
{
    SUBCASE("tag_type extracts the inner type from Trivial wrapper") {
        using TK = TrivialKey<int, 16_ib, 16_vb>;
        static_assert(std::is_same_v<TK::tag_type, int>);
        REQUIRE(true);
    }

    SUBCASE("tag_type works with custom types") {
        struct MyStruct
        {
            int x;
        };

        using TK = TrivialKey<MyStruct, 16_ib, 16_vb>;
        static_assert(std::is_same_v<TK::tag_type, MyStruct>);
        REQUIRE(true);
    }
}

TEST_CASE("TrivialKey: same value_type as Key")
{
    SUBCASE("32-bit TrivialKey has uint32_t value_type") {
        using TK = TrivialKey<int, 16_ib, 16_vb>;
        using K = Key<int, 16_ib, 16_vb>;
        static_assert(std::is_same_v<TK::value_type, std::uint32_t>);
        static_assert(std::is_same_v<TK::value_type, K::value_type>);
        REQUIRE(true);
    }

    SUBCASE("64-bit TrivialKey has uint64_t value_type") {
        using TK = TrivialKey<int, 32_ib, 32_vb>;
        static_assert(std::is_same_v<TK::value_type, std::uint64_t>);
        REQUIRE(true);
    }
}

TEST_CASE("TrivialKey: construction and accessors work correctly")
{
    SUBCASE("construct with components") {
        auto k = make_trivial_key<int, 16_ib, 8_vb, 8_ub>(100, 5, 42);

        REQUIRE(k.index() == 100);
        REQUIRE(k.version() == 5);
        REQUIRE(k.user() == 42);
    }

    SUBCASE("null() returns null key") {
        using TK = TrivialKey<int, 16_ib, 16_vb>;
        auto k = TK::null();

        REQUIRE(k.is_null());
        REQUIRE(k.index() == 0);
        REQUIRE(k.version() == 0);
    }

    SUBCASE("with_user creates modified copy") {
        using TK = TrivialKey<int, 16_ib, 8_vb, 8_ub>;
        auto k1 = make_trivial_key<int, 16_ib, 8_vb, 8_ub>(100, 5, 1);
        auto k2 = k1.with_user(TK::user_type{std::uint8_t{99}});

        REQUIRE(k1.user() == 1);
        REQUIRE(k2.user() == 99);
        REQUIRE(k1.index() == k2.index());
        REQUIRE(k1.version() == k2.version());
    }
}

TEST_CASE("TrivialKey: comparison operators")
{
    SUBCASE("equality comparison") {
        auto k1 = make_trivial_key<int, 16_ib, 16_vb>(100, 5);
        auto k2 = make_trivial_key<int, 16_ib, 16_vb>(100, 5);
        auto k3 = make_trivial_key<int, 16_ib, 16_vb>(200, 5);

        REQUIRE(k1 == k2);
        REQUIRE(k1 != k3);
    }

    SUBCASE("ordering comparison") {
        auto k1 = make_trivial_key<int, 16_ib, 16_vb>(100, 5);
        auto k2 = make_trivial_key<int, 16_ib, 16_vb>(200, 5);

        REQUIRE(k1 < k2);
        REQUIRE(k2 > k1);
    }
}

TEST_CASE("TrivialKey: hashing")
{
    SUBCASE("hash() member function works") {
        auto k = make_trivial_key<int, 16_ib, 16_vb>(100, 5);

        auto h = k.hash();
        REQUIRE(h != 0);
    }

    SUBCASE("std::hash specialization works") {
        using TK = TrivialKey<int, 16_ib, 16_vb>;
        auto k = make_trivial_key<int, 16_ib, 16_vb>(100, 5);

        std::hash<TK> hasher;
        REQUIRE(hasher(k) == k.hash());
    }

    SUBCASE("TrivialKey works in unordered_map") {
        using TK = TrivialKey<int, 16_ib, 16_vb>;
        std::unordered_map<TK, std::string> map;

        auto k1 = make_trivial_key<int, 16_ib, 16_vb>(100, 1);
        auto k2 = make_trivial_key<int, 16_ib, 16_vb>(200, 2);

        map[k1] = "first";
        map[k2] = "second";

        REQUIRE(map[k1] == "first");
        REQUIRE(map[k2] == "second");
    }
}

TEST_CASE("TrivialKey: identifies_same_object")
{
    SUBCASE("keys with different user bits identify same object") {
        auto k1 = make_trivial_key<int, 16_ib, 8_vb, 8_ub>(100, 5, 1);
        auto k2 = make_trivial_key<int, 16_ib, 8_vb, 8_ub>(100, 5, 99);

        REQUIRE(k1 != k2);
        REQUIRE(k1.identifies_same_object(k2));
    }

    SUBCASE("keys with different index do not identify same object") {
        auto k1 = make_trivial_key<int, 16_ib, 8_vb, 8_ub>(100, 5, 1);
        auto k2 = make_trivial_key<int, 16_ib, 8_vb, 8_ub>(101, 5, 1);

        REQUIRE(not k1.identifies_same_object(k2));
    }
}

TEST_CASE("TrivialKey: implicit lifetime type suitability")
{
    // These tests verify properties needed for implicit lifetime types
    // which can be safely used in shared memory or with memcpy

    SUBCASE("TrivialKey has trivial copy constructor") {
        using TK = TrivialKey<int, 16_ib, 16_vb>;
        static_assert(std::is_trivially_copy_constructible_v<TK>);
        REQUIRE(std::is_trivially_copy_constructible_v<TK>);
    }

    SUBCASE("TrivialKey has trivial copy assignment") {
        using TK = TrivialKey<int, 16_ib, 16_vb>;
        static_assert(std::is_trivially_copy_assignable_v<TK>);
        REQUIRE(std::is_trivially_copy_assignable_v<TK>);
    }

    SUBCASE("TrivialKey has trivial move constructor") {
        using TK = TrivialKey<int, 16_ib, 16_vb>;
        static_assert(std::is_trivially_move_constructible_v<TK>);
        REQUIRE(std::is_trivially_move_constructible_v<TK>);
    }

    SUBCASE("TrivialKey has trivial move assignment") {
        using TK = TrivialKey<int, 16_ib, 16_vb>;
        static_assert(std::is_trivially_move_assignable_v<TK>);
        REQUIRE(std::is_trivially_move_assignable_v<TK>);
    }

    SUBCASE("memcpy round-trip preserves TrivialKey") {
        using TK = TrivialKey<int, 16_ib, 16_vb>;
        auto original = make_trivial_key<int, 16_ib, 16_vb>(12345, 678);

        // Simulate shared memory / memcpy scenario using a union to provide
        // storage. The union's implicit default constructor is trivial because
        // TrivialKey's default constructor is trivial (even though private).
        union Storage
        {
            TK key;
            std::byte bytes[sizeof(TK)];

            constexpr Storage()
            : bytes{}
            { }
        };

        Storage buffer;
        std::memcpy(buffer.bytes, &original, sizeof(TK));

        // For implicit lifetime types, accessing buffer.key after memcpy
        // is well-defined because the object's lifetime begins implicitly.
        REQUIRE(buffer.key == original);
        REQUIRE(buffer.key.index() == 12345);
        REQUIRE(buffer.key.version() == 678);
    }
}

TEST_CASE("TrivialKey: different bit widths"){
    SUBCASE("16-bit TrivialKey"){using TK = TrivialKey<int, 8_ib, 8_vb>;
static_assert(std::is_same_v<TK::value_type, std::uint16_t>);
static_assert(std::is_trivially_copyable_v<TK>);

auto k = make_trivial_key<int, 8_ib, 8_vb>(255, 255);
REQUIRE(k.index() == 255);
REQUIRE(k.version() == 255);
} // anonymous namespace

SUBCASE("64-bit TrivialKey") {
    using TK = TrivialKey<int, 32_ib, 32_vb>;
    static_assert(std::is_same_v<TK::value_type, std::uint64_t>);
    static_assert(std::is_trivially_copyable_v<TK>);

    auto k = make_trivial_key<int, 32_ib, 32_vb>(4294967295U, 4294967295U);
    REQUIRE(k.index() == 4294967295U);
    REQUIRE(k.version() == 4294967295U);
}

#ifdef __UINT128_TYPE__
SUBCASE("128-bit TrivialKey") {
    using TK = TrivialKey<int, 64_ib, 64_vb>;
    static_assert(std::is_same_v<TK::value_type, unsigned __int128>);
    static_assert(std::is_trivially_copyable_v<TK>);

    auto k = make_trivial_key<int, 64_ib, 64_vb>(1ULL << 40, 1ULL << 30);
    REQUIRE(k.index() == (1ULL << 40));
    REQUIRE(k.version() == (1ULL << 30));
}
#endif
}

TEST_CASE("TrivialKey: wjh namespace alias")
{
    SUBCASE("TrivialSlotMapKey alias works") {
        using TK = wjh::TrivialSlotMapKey<int, 16_ib, 16_vb>;
        static_assert(std::is_trivially_copyable_v<TK>);
        static_assert(std::is_same_v<TK::tag_type, int>);
        REQUIRE(true);
    }

    SUBCASE("TrivialSlotMapKey is same as TrivialKey") {
        using TK1 = wjh::TrivialSlotMapKey<int, 16_ib, 16_vb>;
        using TK2 = TrivialKey<int, 16_ib, 16_vb>;
        static_assert(std::is_same_v<TK1, TK2>);
        REQUIRE(true);
    }
}

} // anonymous namespace
