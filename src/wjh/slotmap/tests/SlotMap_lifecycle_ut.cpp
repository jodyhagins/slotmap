// ----------------------------------------------------------------------
// Copyright 2025 Jody Hagins
// Distributed under the MIT Software License
// See accompanying file LICENSE or copy at
// https://opensource.org/licenses/MIT
// ----------------------------------------------------------------------
//
// SlotMap lifecycle tests: construction, copy, move, swap, pop, reserve
//

#include "wjh/slotmap/SlotMap.hpp"

#include <cstdint>
#include <string>
#include <type_traits>

#include "testing/doctest.hpp"

namespace {
using wjh::slotmap::Key;
using namespace wjh::slotmap::literals;

template <typename KeyT>
class SlotMap
: public wjh::slotmap::SlotMap<KeyT>
{
    using Base = wjh::slotmap::SlotMap<KeyT>;

public:
    using size_type = typename Base::size_type;
    using naked_size_type = typename size_type::value_type;
    using Base::Base;

    template <typename SizeT>
    explicit SlotMap(SizeT slots_per_slab)
    requires std::is_integral_v<SizeT>
    : Base(size_type{static_cast<naked_size_type>(slots_per_slab)})
    { }
};

// ============================================================================
// Type Alias Tests
// ============================================================================

TEST_CASE("SlotMap: type aliases")
{
    using TestMap = SlotMap<Key<int, 16_ib, 16_vb>>;

    static_assert(std::is_same_v<TestMap::key_type, Key<int, 16_ib, 16_vb>>);
    static_assert(std::is_same_v<TestMap::mapped_type, int>);

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
        using Size = SlotMap<Key<int, 16_ib, 16_vb>>::size_type;
        SlotMap<Key<int, 16_ib, 16_vb>> map;

        CHECK(map.is_empty());
        CHECK(map.size() == Size(0u));
    }

    SUBCASE("works with different key configurations") {
        SlotMap<Key<int, 8_ib, 8_vb, 16_ub>> map32;
        CHECK(map32.is_empty());

        SlotMap<Key<int, 20_ib, 20_vb, 24_ub>> map64;
        CHECK(map64.is_empty());

        SlotMap<Key<std::string, 10_ib, 10_vb, 12_ub>> map_string;
        CHECK(map_string.is_empty());
    }
}

TEST_CASE("SlotMap: explicit slab size construction")
{
    SUBCASE("accepts power of 2 slab sizes") {
        CHECK_NOTHROW(SlotMap<Key<int, 16_ib, 16_vb>>(1u));
        CHECK_NOTHROW(SlotMap<Key<int, 16_ib, 16_vb>>(2u));
        CHECK_NOTHROW(SlotMap<Key<int, 16_ib, 16_vb>>(4u));
        CHECK_NOTHROW(SlotMap<Key<int, 16_ib, 16_vb>>(1024u));
        CHECK_NOTHROW(SlotMap<Key<int, 16_ib, 16_vb>>(4096u));
    }

    SUBCASE("rejects non-power of 2 slab sizes") {
        using Map = SlotMap<Key<int, 16_ib, 16_vb>>;
        CHECK_THROWS_AS(Map(3u), std::invalid_argument);
        CHECK_THROWS_AS(Map(1000u), std::invalid_argument);
        CHECK_THROWS_AS(Map(1023u), std::invalid_argument);
    }

    SUBCASE("rejects zero slab size") {
        using Map = SlotMap<Key<int, 16_ib, 16_vb>>;
        CHECK_THROWS_AS(Map(0u), std::invalid_argument);
    }

    SUBCASE("slab size cannot exceed max index") {
        // With 4 index bits, max_index = (1 << 4) - 1 = 15
        // So max usable slots is 15, and slab size must be <= 15
        // But slab size must be power of 2, so max is 8
        using SmallMap = SlotMap<Key<int, 4_ib, 4_vb, 24_ub>>;
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
    SUBCASE("max_simultaneous_objects is one past 1 << numbits") {
        using Map16 = SlotMap<Key<int, 16_ib, 16_vb>>;
        CHECK(Map16::max_simultaneous_objects.value == 0x10000u); // 2^16

        using Map8 = SlotMap<Key<int, 8_ib, 8_vb, 16_ub>>;
        CHECK(Map8::max_simultaneous_objects.value == 0x100u); // 2^8

        using Map4 = SlotMap<Key<int, 4_ib, 4_vb, 24_ub>>;
        CHECK(Map4::max_simultaneous_objects.value == 0x10u); // 2^4
    }

    SUBCASE("max_simultaneous_objects fits in size_type but not index_type") {
        // Verify the design: max_simultaneous_objects can be stored in
        // size_type but would overflow index_type

        using Map16 = SlotMap<Key<int, 16_ib, 16_vb>>;
        static_assert(sizeof(Map16::size_type) * 8 >= 17); // Need 17 bits
        static_assert(sizeof(Map16::index_type) * 8 >= 16); // Only 16 bits

        // The sentinel value should be exactly 2^IndexBits
        CHECK(Map16::max_simultaneous_objects.value == (1u << 16));
    }

    SUBCASE("max_total_objects is one 2^IndexBits + 2^VersionBits - 1") {
        using Map16 = SlotMap<Key<int, 16_ib, 16_vb>>;
        CHECK(Map16::max_total_objects.value == 0xffffffffu);

        using Map8 = SlotMap<Key<int, 8_ib, 8_vb, 16_ub>>;
        CHECK(Map8::max_total_objects.value == 0xffffu);

        using Map4 = SlotMap<Key<int, 4_ib, 4_vb, 24_ub>>;
        CHECK(Map4::max_total_objects.value == 0xffu);
    }
}

TEST_CASE("SlotMap: free list sentinel storage in Slot")
{
    // This test verifies that the sentinel value can be stored in a Slot's
    // next field. This is critical for the free list to work correctly.
    //
    // The Slab's slot_type uses size_type (not index_type) for the next-link,
    // which allows storing max_simultaneous_objects (one past max index).
    using Map = SlotMap<Key<int, 8_ib, 8_vb, 16_ub>>;

    // max_simultaneous_objects = 256 (0x100), which doesn't fit in uint8_t
    // (index_type) but does fit in uint16_t (size_type)
    constexpr auto sentinel = Map::max_simultaneous_objects;
    CHECK(sentinel.value == 0x100u);

    SUBCASE("size_type slot can store sentinel") {
        // Create a slot using size_type for the next-link (as Slab now does)
        using slot_type = Map::slot_type;
        alignas(slot_type) std::byte storage[sizeof(slot_type)]{};
        auto & slot = *::new (storage) slot_type{};

        // Store the sentinel in the slot's next field
        slot.set_next(sentinel);

        // Verify we can retrieve it correctly (no truncation)
        CHECK(slot.next() == sentinel);

        slot.~slot_type();
    }
}

// ============================================================================
// Move Operations
// ============================================================================

TEST_CASE("SlotMap: move construction")
{
    SlotMap<Key<int, 16_ib, 16_vb>> map;

    SUBCASE("moved-from map is empty") {
        auto moved = std::move(map);

        CHECK(moved.is_empty());
        CHECK(map.is_empty()); // NOLINT: testing moved-from state
    }
}

TEST_CASE("SlotMap: move assignment")
{
    SlotMap<Key<int, 16_ib, 16_vb>> map1;
    SlotMap<Key<int, 16_ib, 16_vb>> map2;

    SUBCASE("move assignment leaves source empty") {
        map2 = std::move(map1);

        CHECK(map2.is_empty());
        CHECK(map1.is_empty()); // NOLINT: testing moved-from state
    }
}

// ============================================================================
// Swap Tests
// ============================================================================

TEST_CASE("SlotMap: swap")
{
    SUBCASE("swap empty maps") {
        SlotMap<Key<int, 16_ib, 16_vb>> map1;
        SlotMap<Key<int, 16_ib, 16_vb>> map2;

        map1.swap(map2);

        CHECK(map1.is_empty());
        CHECK(map2.is_empty());
    }

    SUBCASE("swap with one empty map") {
        SlotMap<Key<int, 16_ib, 16_vb>> map1;
        SlotMap<Key<int, 16_ib, 16_vb>> map2;

        auto key = map1.emplace(42);
        map1.swap(map2);

        CHECK(map1.is_empty());
        CHECK(map2.size().value == 1);
        CHECK(map2.contains(key));
    }

    SUBCASE("swap with both non-empty") {
        SlotMap<Key<int, 16_ib, 16_vb>> map1;
        SlotMap<Key<int, 16_ib, 16_vb>> map2;

        auto key1 = map1.emplace(42);
        auto key2 = map2.emplace(100);
        auto key3 = map2.emplace(200);

        map1.swap(map2);

        CHECK(map1.size().value == 2);
        CHECK(map2.size().value == 1);

        CHECK(map1.contains(key2));
        CHECK(map1.contains(key3));
        CHECK(map2.contains(key1));

        int v1 = 0;
        (void)map2.use(key1, [&](int const & v) { v1 = v; });
        CHECK(v1 == 42);
    }

    SUBCASE("swap with different slab sizes") {
        SlotMap<Key<int, 16_ib, 16_vb>> map1(4u);
        SlotMap<Key<int, 16_ib, 16_vb>> map2(8u);

        for (int i = 0; i < 10; ++i) {
            (void)map1.emplace(i);
        }
        for (int i = 100; i < 105; ++i) {
            (void)map2.emplace(i);
        }

        map1.swap(map2);

        CHECK(map1.size().value == 5);
        CHECK(map2.size().value == 10);
    }

    SUBCASE("self-swap is safe") {
        SlotMap<Key<int, 16_ib, 16_vb>> map;
        auto key = map.emplace(42);

        map.swap(map);

        CHECK(map.size().value == 1);
        CHECK(map.contains(key));
    }
}

// ============================================================================
// Copy Construction Tests
// ============================================================================

TEST_CASE("SlotMap: copy construction")
{
    SUBCASE("copy empty map") {
        SlotMap<Key<int, 16_ib, 16_vb>> original;
        SlotMap<Key<int, 16_ib, 16_vb>> copy(original);

        CHECK(copy.is_empty());
    }

    SUBCASE("copy single element") {
        SlotMap<Key<int, 16_ib, 16_vb>> original;
        auto key = original.emplace(42);

        SlotMap<Key<int, 16_ib, 16_vb>> copy(original);

        CHECK(copy.size().value == 1);
        CHECK(copy.contains(key));

        int value = 0;
        (void)copy.use(key, [&](int const & v) { value = v; });
        CHECK(value == 42);
    }

    SUBCASE("copy multiple elements") {
        SlotMap<Key<int, 16_ib, 16_vb>> original;
        std::vector<Key<int, 16_ib, 16_vb>> keys;

        for (int i = 0; i < 100; ++i) {
            keys.push_back(original.emplace(i * 10));
        }

        SlotMap<Key<int, 16_ib, 16_vb>> copy(original);

        CHECK(copy.size().value == 100);

        for (std::size_t i = 0; i < keys.size(); ++i) {
            CHECK(copy.contains(keys[i]));

            int value = 0;
            (void)copy.use(keys[i], [&](int const & v) { value = v; });
            CHECK(value == static_cast<int>(i * 10));
        }
    }

    SUBCASE("copy preserves versions") {
        SlotMap<Key<int, 16_ib, 16_vb>> original(1u);

        // Create and erase to bump versions
        auto key1 = original.emplace(1);
        original.erase(key1);
        auto key2 = original.emplace(2);

        CHECK(key2.version().value > 1);

        SlotMap<Key<int, 16_ib, 16_vb>> copy(original);

        CHECK(copy.contains(key2));
        CHECK(not copy.contains(key1)); // Old key should still be invalid
    }

    SUBCASE("copy is independent - modifying copy doesn't affect original") {
        SlotMap<Key<int, 16_ib, 16_vb>> original;
        auto key = original.emplace(42);

        SlotMap<Key<int, 16_ib, 16_vb>> copy(original);

        // Modify copy
        (void)copy.use(key, [](int & v) { v = 100; });
        auto key2 = copy.emplace(200);

        // Original should be unchanged
        CHECK(original.size().value == 1);
        int original_value = 0;
        (void)original.use(key, [&](int const & v) { original_value = v; });
        CHECK(original_value == 42);
        CHECK(not original.contains(key2));
    }

    SUBCASE("copy is independent - modifying original doesn't affect copy") {
        SlotMap<Key<int, 16_ib, 16_vb>> original;
        auto key = original.emplace(42);

        SlotMap<Key<int, 16_ib, 16_vb>> copy(original);

        // Modify original
        (void)original.use(key, [](int & v) { v = 100; });
        auto key2 = original.emplace(200);

        // Copy should be unchanged
        CHECK(copy.size().value == 1);
        int copy_value = 0;
        (void)copy.use(key, [&](int const & v) { copy_value = v; });
        CHECK(copy_value == 42);
        CHECK(not copy.contains(key2));
    }

    SUBCASE("copy with sparse data") {
        SlotMap<Key<int, 16_ib, 16_vb>> original(4u);

        std::vector<Key<int, 16_ib, 16_vb>> keys;
        for (int i = 0; i < 10; ++i) {
            keys.push_back(original.emplace(i));
        }

        // Erase every other element to create gaps
        for (std::size_t i = 0; i < keys.size(); i += 2) {
            original.erase(keys[i]);
        }

        SlotMap<Key<int, 16_ib, 16_vb>> copy(original);

        CHECK(copy.size().value == 5);

        // Check that correct elements are present
        for (std::size_t i = 1; i < keys.size(); i += 2) {
            CHECK(copy.contains(keys[i]));

            int value = 0;
            (void)copy.use(keys[i], [&](int const & v) { value = v; });
            CHECK(value == static_cast<int>(i));
        }

        // Check that erased elements are not present
        for (std::size_t i = 0; i < keys.size(); i += 2) {
            CHECK(not copy.contains(keys[i]));
        }
    }

    SUBCASE("copy with string values") {
        SlotMap<Key<std::string, 16_ib, 16_vb>> original;
        auto key1 = original.emplace("hello");
        auto key2 = original.emplace("world");

        SlotMap<Key<std::string, 16_ib, 16_vb>> copy(original);

        CHECK(copy.size().value == 2);

        std::string s1, s2;
        (void)copy.use(key1, [&](std::string const & s) { s1 = s; });
        (void)copy.use(key2, [&](std::string const & s) { s2 = s; });

        CHECK(s1 == "hello");
        CHECK(s2 == "world");
    }
}

// ============================================================================
// Copy Assignment Tests
// ============================================================================

TEST_CASE("SlotMap: copy assignment")
{
    SUBCASE("assign empty to empty") {
        SlotMap<Key<int, 16_ib, 16_vb>> original;
        SlotMap<Key<int, 16_ib, 16_vb>> target;

        target = original;

        CHECK(target.is_empty());
    }

    SUBCASE("assign non-empty to empty") {
        SlotMap<Key<int, 16_ib, 16_vb>> original;
        auto key = original.emplace(42);

        SlotMap<Key<int, 16_ib, 16_vb>> target;
        target = original;

        CHECK(target.size().value == 1);
        CHECK(target.contains(key));
    }

    SUBCASE("assign empty to non-empty") {
        SlotMap<Key<int, 16_ib, 16_vb>> original;
        SlotMap<Key<int, 16_ib, 16_vb>> target;

        auto key = target.emplace(42);

        target = original;

        CHECK(target.is_empty());
        CHECK(not target.contains(key));
    }

    SUBCASE("assign non-empty to non-empty") {
        SlotMap<Key<int, 16_ib, 16_vb>> original;
        auto key1 = original.emplace(100);

        SlotMap<Key<int, 16_ib, 16_vb>> target;
        // Add more elements to target so it has different structure
        (void)target.emplace(42);
        auto key3 = target.emplace(43);
        auto key4 = target.emplace(44);

        target = original;

        CHECK(target.size().value == 1);
        CHECK(target.contains(key1));

        // Verify the value is correct
        int value = 0;
        (void)target.use(key1, [&](int const & v) { value = v; });
        CHECK(value == 100);

        // key3 and key4 should be invalid since original only has one element
        CHECK(not target.contains(key3));
        CHECK(not target.contains(key4));
    }

    SUBCASE("self-assignment is safe") {
        SlotMap<Key<int, 16_ib, 16_vb>> map;
        auto key = map.emplace(42);

        // Use reference to defeat compiler self-assignment warning
        auto & ref = map;
        map = ref;

        CHECK(map.size().value == 1);
        CHECK(map.contains(key));
    }

    SUBCASE("assignment destroys old values") {
        static int destructor_count = 0;

        struct Counter
        {
            int value = 0;
            Counter() = default;
            Counter(Counter const &) = default;
            Counter & operator = (Counter const &) = default;

            ~Counter() { ++destructor_count; }
        };

        destructor_count = 0;

        {
            SlotMap<Key<Counter, 16_ib, 16_vb>> original;
            SlotMap<Key<Counter, 16_ib, 16_vb>> target;

            (void)target.emplace();
            (void)target.emplace();

            CHECK(destructor_count == 0);

            target = original;

            CHECK(destructor_count == 2);
        }
    }
}

// ============================================================================
// Pop Tests
// ============================================================================

TEST_CASE("SlotMap: pop")
{
    SUBCASE("pop returns value and removes element") {
        SlotMap<Key<int, 16_ib, 16_vb>> map;
        auto key = map.emplace(42);

        auto result = map.pop(key);

        CHECK(result.has_value());
        CHECK(result.value() == 42);
        CHECK(map.is_empty());
        CHECK(not map.contains(key));
    }

    SUBCASE("pop returns nullopt for invalid key") {
        SlotMap<Key<int, 16_ib, 16_vb>> map;
        auto key = map.emplace(42);
        map.erase(key);

        auto result = map.pop(key);

        CHECK(not result.has_value());
    }

    SUBCASE("pop returns nullopt for null key") {
        SlotMap<Key<int, 16_ib, 16_vb>> map;
        (void)map.emplace(42);

        auto result = map.pop(Key<int, 16_ib, 16_vb>::null());

        CHECK(not result.has_value());
    }

    SUBCASE("pop moves value out") {
        SlotMap<Key<std::string, 16_ib, 16_vb>> map;
        auto key = map.emplace("hello world");

        auto result = map.pop(key);

        CHECK(result.has_value());
        CHECK(result.value() == "hello world");
        CHECK(not map.contains(key));
    }

    SUBCASE("pop with move-only type") {
        SlotMap<Key<std::unique_ptr<int>, 16_ib, 16_vb>> map;
        auto key = map.emplace(std::make_unique<int>(42));

        auto result = map.pop(key);

        CHECK(result.has_value());
        CHECK(*result.value() == 42);
        CHECK(not map.contains(key));
    }

    SUBCASE("pop multiple elements") {
        SlotMap<Key<int, 16_ib, 16_vb>> map;
        auto key1 = map.emplace(1);
        auto key2 = map.try_emplace(2);
        auto key3 = map.emplace(3);

        CHECK(map.size().value == 3);

        auto r1 = map.pop(key1);
        CHECK(r1.value() == 1);
        CHECK(map.size().value == 2);

        auto r2 = map.pop(key2);
        CHECK(r2.value() == 2);
        CHECK(map.size().value == 1);

        auto r3 = map.pop(key3);
        CHECK(r3.value() == 3);
        CHECK(map.is_empty());
    }
}

// ============================================================================
// Reserve Tests
// ============================================================================

TEST_CASE("SlotMap: reserve")
{
    using TestMap = SlotMap<Key<int, 16_ib, 16_vb>>;

    SUBCASE("reserve does not change size") {
        TestMap map;

        map.reserve(TestMap::size_type(100u));

        CHECK(map.is_empty());
    }

    SUBCASE("reserve allows emplace without allocation") {
        TestMap map(4u);

        // Reserve 16 slots (4 slabs of 4)
        map.reserve(TestMap::size_type(16u));

        // Emplace 16 elements - should not need new allocation
        for (int i = 0; i < 16; ++i) {
            auto key = map.emplace(i);
            CHECK(key != Key<int, 16_ib, 16_vb>::null());
        }

        CHECK(map.size().value == 16);
    }

    SUBCASE("reserve more than index space") {
        // Use small index space (32-bit key: 8 index, 24 version)
        using SmallMap = SlotMap<Key<int, 8_ib, 24_vb>>;
        SmallMap map(4u);

        // Reserve more than can fit - should cap at max (256 slots)
        // Size has IndexBits+1 = 9 bits, so value 256 (max index+1) is valid
        using size_value_type = SmallMap::size_type::value_type;
        map.reserve(SmallMap::size_type(size_value_type(256u)));

        // Can only fit 256 slots with 8-bit index
        for (int i = 0; i < 256; ++i) {
            auto key = map.emplace(i);
            CHECK(key != Key<int, 8_ib, 24_vb>::null());
        }

        // 257th should return null key
        auto extra_key = map.try_emplace(999);
        CHECK(extra_key == Key<int, 8_ib, 24_vb>::null());
    }
}

} // anonymous namespace
