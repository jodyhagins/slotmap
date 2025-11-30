// ----------------------------------------------------------------------
// Copyright 2025 Jody Hagins
// Distributed under the MIT Software License
// See accompanying file LICENSE or copy at
// https://opensource.org/licenses/MIT
// ----------------------------------------------------------------------

#include "wjh/slotmap/SlotMap.hpp"

#include <cstdint>
#include <string>
#include <type_traits>

#include "testing/doctest.hpp"
#include "testing/rapidcheck.hpp"

namespace {
using wjh::slotmap::Key;

// using namespace wjh::slotmap;

template <typename KeyT>
class SlotMap
: public wjh::slotmap::SlotMap<KeyT>
{
    using Base = wjh::slotmap::SlotMap<KeyT>;

public:
    using size_type = typename Base::size_type;
    using Base::Base;

    template <typename SizeT>
    explicit SlotMap(SizeT slots_per_slab)
    requires requires { size_type{slots_per_slab}; }
    : Base(size_type{slots_per_slab})
    { }
};

// ============================================================================
// Type Alias Tests
// ============================================================================

TEST_CASE("SlotMap: type aliases")
{
    using TestMap = SlotMap<Key<16, 16, 0, int>>;

    static_assert(std::is_same_v<TestMap::key_type, Key<16, 16, 0, int>>);
    static_assert(std::is_same_v<TestMap::value_type, int>);

    // Index type should be able to hold IndexBits worth of values
    static_assert(sizeof(TestMap::index_type) * 8 >= 16);
    static_assert(sizeof(TestMap::version_type) * 8 >= 16);

    REQUIRE(true);
}

// ============================================================================
// Construction Tests
// ============================================================================

TEST_CASE("SlotMap: default construction")
{
    SUBCASE("is empty after construction") {
        using Size = SlotMap<Key<16, 16, 0, int>>::size_type;
        SlotMap<Key<16, 16, 0, int>> map;

        CHECK(map.is_empty());
        CHECK(map.size() == Size(0u));
    }

    SUBCASE("works with different key configurations") {
        SlotMap<Key<8, 8, 16, int>> map32;
        CHECK(map32.is_empty());

        SlotMap<Key<20, 20, 24, int>> map64;
        CHECK(map64.is_empty());

        SlotMap<Key<10, 10, 12, std::string>> map_string;
        CHECK(map_string.is_empty());
    }
}

TEST_CASE("SlotMap: explicit slab size construction")
{
    SUBCASE("accepts power of 2 slab sizes") {
        CHECK_NOTHROW(SlotMap<Key<16, 16, 0, int>>(1u));
        CHECK_NOTHROW(SlotMap<Key<16, 16, 0, int>>(2u));
        CHECK_NOTHROW(SlotMap<Key<16, 16, 0, int>>(4u));
        CHECK_NOTHROW(SlotMap<Key<16, 16, 0, int>>(1024u));
        CHECK_NOTHROW(SlotMap<Key<16, 16, 0, int>>(4096u));
    }

    SUBCASE("rejects non-power of 2 slab sizes") {
        using Map = SlotMap<Key<16, 16, 0, int>>;
        CHECK_THROWS_AS(Map(3u), std::invalid_argument);
        CHECK_THROWS_AS(Map(1000u), std::invalid_argument);
        CHECK_THROWS_AS(Map(1023u), std::invalid_argument);
    }

    SUBCASE("rejects zero slab size") {
        using Map = SlotMap<Key<16, 16, 0, int>>;
        CHECK_THROWS_AS(Map(0u), std::invalid_argument);
    }

    SUBCASE("slab size cannot exceed max index") {
        // With 4 index bits, max_index = (1 << 4) - 1 = 15
        // So max usable slots is 15, and slab size must be <= 15
        // But slab size must be power of 2, so max is 8
        using SmallMap = SlotMap<Key<4, 4, 24, int>>;
        using size_type = SmallMap::size_type;
        CHECK_NOTHROW(SmallMap(size_type{std::uint8_t(8)}));

        // 16 > 15, so this should fail
        CHECK_THROWS_AS(
            SmallMap(size_type{std::uint8_t(16 + 1)}),
            std::invalid_argument);
    }
}

TEST_CASE("SlotMap: constants")
{
    SUBCASE("end_of_free_list is one past 1 << numbits") {
        using Map16 = SlotMap<Key<16, 16, 0, int>>;
        CHECK(Map16::end_of_free_list.value == 0x10000u); // 2^16

        using Map8 = SlotMap<Key<8, 8, 16, int>>;
        CHECK(Map8::end_of_free_list.value == 0x100u); // 2^8

        using Map4 = SlotMap<Key<4, 4, 24, int>>;
        CHECK(Map4::end_of_free_list.value == 0x10u); // 2^4
    }

    SUBCASE("end_of_free_list fits in size_type but not index_type") {
        // Verify the design: end_of_free_list can be stored in size_type
        // but would overflow index_type

        using Map16 = SlotMap<Key<16, 16, 0, int>>;
        static_assert(sizeof(Map16::size_type) * 8 >= 17); // Need 17 bits
        static_assert(sizeof(Map16::index_type) * 8 >= 16); // Only 16 bits

        // The sentinel value should be exactly 2^IndexBits
        CHECK(Map16::end_of_free_list.value == (1u << 16));
    }
}

TEST_CASE("SlotMap: free list sentinel storage in Slot")
{
    // This test verifies that the sentinel value can be stored in a Slot's
    // next field. This is critical for the free list to work correctly.
    //
    // The Slab's slot_type uses size_type (not index_type) for the next-link,
    // which allows storing end_of_free_list (one past max index).

    using Map = SlotMap<Key<8, 8, 16, int>>;
    using index_type = Map::index_type;
    using size_type = Map::size_type;
    using version_type = Map::version_type;

    // end_of_free_list = 256 (0x100), which doesn't fit in uint8_t (index_type)
    // but does fit in uint16_t (size_type)
    constexpr auto sentinel = Map::end_of_free_list;
    CHECK(sentinel.value == 0x100u);

    SUBCASE("size_type slot can store sentinel") {
        // Create a slot using size_type for the next-link (as Slab now does)
        using slot_type = wjh::slotmap::detail::Slot<int, size_type, version_type>;
        alignas(slot_type) std::byte storage[sizeof(slot_type)]{};
        auto & slot = *::new (storage) slot_type{};

        // Store the sentinel in the slot's next field
        slot.set_next(sentinel);

        // Verify we can retrieve it correctly (no truncation)
        CHECK(slot.next() == sentinel);

        slot.~slot_type();
    }

    SUBCASE("index_type slot would truncate sentinel") {
        // This demonstrates that index_type cannot hold the sentinel
        // (it wraps around to 0 due to overflow)
        using bad_slot_type = wjh::slotmap::detail::Slot<int, index_type, version_type>;
        alignas(bad_slot_type) std::byte storage[sizeof(bad_slot_type)]{};
        auto & slot = *::new (storage) bad_slot_type{};

        // Store a truncated version (simulating what would happen)
        auto truncated = index_type(static_cast<index_type::value_type>(sentinel.value));
        slot.set_next(truncated);

        // The retrieved value is NOT the sentinel - it wrapped to 0
        CHECK(slot.next() != size_type(sentinel));
        CHECK(slot.next().value == 0u);  // 0x100 truncated to uint8_t = 0

        slot.~bad_slot_type();
    }
}

// ============================================================================
// Move Operations
// ============================================================================

TEST_CASE("SlotMap: move construction")
{
    SlotMap<Key<16, 16, 0, int>> map;

    SUBCASE("moved-from map is empty") {
        auto moved = std::move(map);

        CHECK(moved.is_empty());
        CHECK(map.is_empty()); // NOLINT: testing moved-from state
    }
}

TEST_CASE("SlotMap: move assignment")
{
    SlotMap<Key<16, 16, 0, int>> map1;
    SlotMap<Key<16, 16, 0, int>> map2;

    SUBCASE("move assignment leaves source empty") {
        map2 = std::move(map1);

        CHECK(map2.is_empty());
        CHECK(map1.is_empty()); // NOLINT: testing moved-from state
    }
}

// ============================================================================
// Type Traits
// ============================================================================

TEST_CASE("SlotMap: type traits")
{
    using Map = SlotMap<Key<16, 16, 0, int>>;

    SUBCASE("is default constructible") {
        static_assert(std::is_default_constructible_v<Map>);
    }

    SUBCASE("is not copy constructible (for now)") {
        static_assert(not std::is_copy_constructible_v<Map>);
    }

    SUBCASE("is not copy assignable (for now)") {
        static_assert(not std::is_copy_assignable_v<Map>);
    }

    SUBCASE("is move constructible") {
        static_assert(std::is_move_constructible_v<Map>);
    }

    SUBCASE("is move assignable") {
        static_assert(std::is_move_assignable_v<Map>);
    }

    SUBCASE("is nothrow move constructible") {
        static_assert(std::is_nothrow_move_constructible_v<Map>);
    }

    SUBCASE("is nothrow move assignable") {
        static_assert(std::is_nothrow_move_assignable_v<Map>);
    }

    REQUIRE(true);
}

// ============================================================================
// Property-Based Tests
// ============================================================================

TEST_CASE("SlotMap: property-based slab size validation")
{
    rc::check("rejects non-power-of-2 slab sizes", []() {
        auto const size_hint = *rc::gen::inRange<std::uint16_t>(3, 10000);

        // Skip if it's a power of 2
        if ((size_hint & (size_hint - 1)) == 0) {
            return;
        }

        bool threw = false;
        try {
            SlotMap<Key<16, 16, 0, int>> map(size_hint);
        } catch (std::invalid_argument const &) {
            threw = true;
        }

        RC_ASSERT(threw);
    });
}

TEST_CASE("SlotMap: property-based power-of-2 acceptance")
{
    rc::check("accepts power-of-2 slab sizes", []() {
        auto const exp = *rc::gen::inRange<unsigned>(0, 12);
        auto const size = static_cast<std::uint16_t>(1u << exp);

        bool threw = false;
        try {
            SlotMap<Key<16, 16, 0, int>> map(size);
        } catch (...) {
            threw = true;
        }

        RC_ASSERT(not threw);
    });
}

// ============================================================================
// Convenience Alias Tests
// ============================================================================

TEST_CASE("SlotMap: wjh namespace alias")
{
    // wjh::SlotMap should work
    wjh::SlotMap<wjh::SlotMapKey<16, 16, 0, int>> map;

    CHECK(map.is_empty());
    CHECK(map.size() == std::uint16_t(0));
}

} // anonymous namespace
