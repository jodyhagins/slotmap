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
using wjh::slotmap::Break;
using wjh::slotmap::Key;

// using namespace wjh::slotmap;

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
        using slot_type =
            wjh::slotmap::detail::Slot<int, size_type, version_type>;
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
        using bad_slot_type =
            wjh::slotmap::detail::Slot<int, index_type, version_type>;
        alignas(bad_slot_type) std::byte storage[sizeof(bad_slot_type)]{};
        auto & slot = *::new (storage) bad_slot_type{};

        // Store a truncated version (simulating what would happen)
        auto truncated = index_type(
            static_cast<index_type::value_type>(sentinel.value));
        slot.set_next(truncated);

        // The retrieved value is NOT the sentinel - it wrapped to 0
        CHECK(slot.next() != size_type(sentinel));
        CHECK(slot.next().value == 0u); // 0x100 truncated to uint8_t = 0

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
// Swap Tests
// ============================================================================

TEST_CASE("SlotMap: swap")
{
    SUBCASE("swap empty maps") {
        SlotMap<Key<16, 16, 0, int>> map1;
        SlotMap<Key<16, 16, 0, int>> map2;

        map1.swap(map2);

        CHECK(map1.is_empty());
        CHECK(map2.is_empty());
    }

    SUBCASE("swap with one empty map") {
        SlotMap<Key<16, 16, 0, int>> map1;
        SlotMap<Key<16, 16, 0, int>> map2;

        auto key = map1.emplace(42);
        map1.swap(map2);

        CHECK(map1.is_empty());
        CHECK(map2.size().value == 1);
        CHECK(map2.contains(key));
    }

    SUBCASE("swap with both non-empty") {
        SlotMap<Key<16, 16, 0, int>> map1;
        SlotMap<Key<16, 16, 0, int>> map2;

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
        map2.use(key1, [&](int const & v) { v1 = v; });
        CHECK(v1 == 42);
    }

    SUBCASE("swap with different slab sizes") {
        SlotMap<Key<16, 16, 0, int>> map1(4u);
        SlotMap<Key<16, 16, 0, int>> map2(8u);

        for (int i = 0; i < 10; ++i) {
            map1.emplace(i);
        }
        for (int i = 100; i < 105; ++i) {
            map2.emplace(i);
        }

        map1.swap(map2);

        CHECK(map1.size().value == 5);
        CHECK(map2.size().value == 10);
    }

    SUBCASE("self-swap is safe") {
        SlotMap<Key<16, 16, 0, int>> map;
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
        SlotMap<Key<16, 16, 0, int>> original;
        SlotMap<Key<16, 16, 0, int>> copy(original);

        CHECK(copy.is_empty());
    }

    SUBCASE("copy single element") {
        SlotMap<Key<16, 16, 0, int>> original;
        auto key = original.emplace(42);

        SlotMap<Key<16, 16, 0, int>> copy(original);

        CHECK(copy.size().value == 1);
        CHECK(copy.contains(key));

        int value = 0;
        copy.use(key, [&](int const & v) { value = v; });
        CHECK(value == 42);
    }

    SUBCASE("copy multiple elements") {
        SlotMap<Key<16, 16, 0, int>> original;
        std::vector<Key<16, 16, 0, int>> keys;

        for (int i = 0; i < 100; ++i) {
            keys.push_back(original.emplace(i * 10));
        }

        SlotMap<Key<16, 16, 0, int>> copy(original);

        CHECK(copy.size().value == 100);

        for (std::size_t i = 0; i < keys.size(); ++i) {
            CHECK(copy.contains(keys[i]));

            int value = 0;
            copy.use(keys[i], [&](int const & v) { value = v; });
            CHECK(value == static_cast<int>(i * 10));
        }
    }

    SUBCASE("copy preserves versions") {
        SlotMap<Key<16, 16, 0, int>> original(1u);

        // Create and erase to bump versions
        auto key1 = original.emplace(1);
        original.erase(key1);
        auto key2 = original.emplace(2);

        CHECK(key2.version().value > 1);

        SlotMap<Key<16, 16, 0, int>> copy(original);

        CHECK(copy.contains(key2));
        CHECK(not copy.contains(key1)); // Old key should still be invalid
    }

    SUBCASE("copy is independent - modifying copy doesn't affect original") {
        SlotMap<Key<16, 16, 0, int>> original;
        auto key = original.emplace(42);

        SlotMap<Key<16, 16, 0, int>> copy(original);

        // Modify copy
        copy.use(key, [](int & v) { v = 100; });
        auto key2 = copy.emplace(200);

        // Original should be unchanged
        CHECK(original.size().value == 1);
        int original_value = 0;
        original.use(key, [&](int const & v) { original_value = v; });
        CHECK(original_value == 42);
        CHECK(not original.contains(key2));
    }

    SUBCASE("copy is independent - modifying original doesn't affect copy") {
        SlotMap<Key<16, 16, 0, int>> original;
        auto key = original.emplace(42);

        SlotMap<Key<16, 16, 0, int>> copy(original);

        // Modify original
        original.use(key, [](int & v) { v = 100; });
        auto key2 = original.emplace(200);

        // Copy should be unchanged
        CHECK(copy.size().value == 1);
        int copy_value = 0;
        copy.use(key, [&](int const & v) { copy_value = v; });
        CHECK(copy_value == 42);
        CHECK(not copy.contains(key2));
    }

    SUBCASE("copy with sparse data") {
        SlotMap<Key<16, 16, 0, int>> original(4u);

        std::vector<Key<16, 16, 0, int>> keys;
        for (int i = 0; i < 10; ++i) {
            keys.push_back(original.emplace(i));
        }

        // Erase every other element to create gaps
        for (std::size_t i = 0; i < keys.size(); i += 2) {
            original.erase(keys[i]);
        }

        SlotMap<Key<16, 16, 0, int>> copy(original);

        CHECK(copy.size().value == 5);

        // Check that correct elements are present
        for (std::size_t i = 1; i < keys.size(); i += 2) {
            CHECK(copy.contains(keys[i]));

            int value = 0;
            copy.use(keys[i], [&](int const & v) { value = v; });
            CHECK(value == static_cast<int>(i));
        }

        // Check that erased elements are not present
        for (std::size_t i = 0; i < keys.size(); i += 2) {
            CHECK(not copy.contains(keys[i]));
        }
    }

    SUBCASE("copy with string values") {
        SlotMap<Key<16, 16, 0, std::string>> original;
        auto key1 = original.emplace("hello");
        auto key2 = original.emplace("world");

        SlotMap<Key<16, 16, 0, std::string>> copy(original);

        CHECK(copy.size().value == 2);

        std::string s1, s2;
        copy.use(key1, [&](std::string const & s) { s1 = s; });
        copy.use(key2, [&](std::string const & s) { s2 = s; });

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
        SlotMap<Key<16, 16, 0, int>> original;
        SlotMap<Key<16, 16, 0, int>> target;

        target = original;

        CHECK(target.is_empty());
    }

    SUBCASE("assign non-empty to empty") {
        SlotMap<Key<16, 16, 0, int>> original;
        auto key = original.emplace(42);

        SlotMap<Key<16, 16, 0, int>> target;
        target = original;

        CHECK(target.size().value == 1);
        CHECK(target.contains(key));
    }

    SUBCASE("assign empty to non-empty") {
        SlotMap<Key<16, 16, 0, int>> original;
        SlotMap<Key<16, 16, 0, int>> target;

        auto key = target.emplace(42);

        target = original;

        CHECK(target.is_empty());
        CHECK(not target.contains(key));
    }

    SUBCASE("assign non-empty to non-empty") {
        SlotMap<Key<16, 16, 0, int>> original;
        auto key1 = original.emplace(100);

        SlotMap<Key<16, 16, 0, int>> target;
        // Add more elements to target so it has different structure
        (void)target.emplace(42);
        auto key3 = target.emplace(43);
        auto key4 = target.emplace(44);

        target = original;

        CHECK(target.size().value == 1);
        CHECK(target.contains(key1));

        // Verify the value is correct
        int value = 0;
        target.use(key1, [&](int const & v) { value = v; });
        CHECK(value == 100);

        // key3 and key4 should be invalid since original only has one element
        CHECK(not target.contains(key3));
        CHECK(not target.contains(key4));
    }

    SUBCASE("self-assignment is safe") {
        SlotMap<Key<16, 16, 0, int>> map;
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
            SlotMap<Key<16, 16, 0, Counter>> original;
            SlotMap<Key<16, 16, 0, Counter>> target;

            target.emplace();
            target.emplace();

            CHECK(destructor_count == 0);

            target = original;

            CHECK(destructor_count == 2);
        }
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

    SUBCASE("is copy constructible for copyable types") {
        static_assert(std::is_copy_constructible_v<Map>);
    }

    SUBCASE("is copy assignable for copyable types") {
        static_assert(std::is_copy_assignable_v<Map>);
    }

    SUBCASE("is not copy constructible for non-copyable types") {
        using NonCopyMap = SlotMap<Key<16, 16, 0, std::unique_ptr<int>>>;
        static_assert(not std::is_copy_constructible_v<NonCopyMap>);
    }

    SUBCASE("is not copy assignable for non-copyable types") {
        using NonCopyMap = SlotMap<Key<16, 16, 0, std::unique_ptr<int>>>;
        static_assert(not std::is_copy_assignable_v<NonCopyMap>);
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

// ============================================================================
// Emplace Tests
// ============================================================================

TEST_CASE("SlotMap: emplace returns valid key")
{
    SlotMap<Key<16, 16, 0, int>> map;

    SUBCASE("single emplace") {
        auto key = map.emplace(42);

        CHECK(not key.is_null());
        CHECK(map.contains(key));
        CHECK(map.size().value == 1);
        CHECK(not map.is_empty());
    }

    SUBCASE("emplace with various types") {
        SlotMap<Key<16, 16, 0, std::string>> str_map;
        auto key = str_map.emplace("hello");

        CHECK(not key.is_null());
        CHECK(str_map.contains(key));
    }

    SUBCASE("emplace with move-only type") {
        SlotMap<Key<16, 16, 0, std::unique_ptr<int>>> ptr_map;
        auto key = ptr_map.emplace(std::make_unique<int>(42));

        CHECK(not key.is_null());
        CHECK(ptr_map.contains(key));
    }
}

TEST_CASE("SlotMap: emplace constructs value correctly")
{
    SlotMap<Key<16, 16, 0, int>> map;

    auto key = map.emplace(42);
    bool found = false;
    map.use(key, [&](int const & v) {
        found = true;
        CHECK(v == 42);
    });
    CHECK(found);
}

TEST_CASE("SlotMap: emplace never returns version 0 at index 0")
{
    SlotMap<Key<16, 16, 0, int>> map(4u);

    // First emplace should get index 0 but version >= 1
    auto key = map.emplace(1);
    CHECK(key.index().value == 0);
    CHECK(key.version().value >= 1);
}

TEST_CASE("SlotMap: multiple emplaces")
{
    SlotMap<Key<16, 16, 0, int>> map;
    std::vector<Key<16, 16, 0, int>> keys;

    for (int i = 0; i < 100; ++i) {
        auto key = map.emplace(i);
        CHECK(not key.is_null());
        keys.push_back(key);
    }

    CHECK(map.size().value == 100);

    // All keys should be valid and contain correct values
    for (std::size_t i = 0; i < 100; ++i) {
        int value = -1;
        bool found = map.use(keys[i], [&](int const & v) { value = v; });
        CHECK(found);
        CHECK(value == static_cast<int>(i));
    }
}

// ============================================================================
// Erase Tests
// ============================================================================

TEST_CASE("SlotMap: erase removes element")
{
    SlotMap<Key<16, 16, 0, int>> map;

    auto key = map.emplace(42);
    CHECK(map.erase(key));
    CHECK(not map.contains(key));
    CHECK(map.size().value == 0);
    CHECK(map.is_empty());
}

TEST_CASE("SlotMap: erase returns false for invalid key")
{
    SlotMap<Key<16, 16, 0, int>> map;

    auto key = map.emplace(42);
    map.erase(key);

    // Try erasing already-erased key
    CHECK(not map.erase(key));
}

TEST_CASE("SlotMap: erase returns false for null key")
{
    SlotMap<Key<16, 16, 0, int>> map;

    CHECK(not map.erase(Key<16, 16, 0, int>::null()));
}

TEST_CASE("SlotMap: key version increments after erase and re-emplace")
{
    SlotMap<Key<16, 16, 0, int>> map(1u); // Single slot slab

    auto key1 = map.emplace(1);
    auto v1 = key1.version();
    auto idx1 = key1.index();
    map.erase(key1);

    auto key2 = map.emplace(2);
    // Same index (only slot reused), but version incremented
    CHECK(key2.index() == idx1);
    CHECK(key2.version().value == v1.value + 1);

    // Old key no longer valid
    CHECK(not map.contains(key1));
    CHECK(map.contains(key2));
}

TEST_CASE("SlotMap: destructor is called on erase")
{
    static int destructor_count = 0;

    struct Counter
    {
        ~Counter() { ++destructor_count; }
    };

    destructor_count = 0;

    {
        SlotMap<Key<16, 16, 0, Counter>> map;
        auto key = map.emplace();
        CHECK(destructor_count == 0);
        map.erase(key);
        CHECK(destructor_count == 1);
    }
    // One more from slab destructor? No - already destroyed
}

// ============================================================================
// Null Key Tests
// ============================================================================

TEST_CASE("SlotMap: null key handling")
{
    SlotMap<Key<16, 16, 0, int>> map;
    auto null_key = Key<16, 16, 0, int>::null();

    SUBCASE("contains returns false for null key") {
        CHECK(not map.contains(null_key));
    }

    SUBCASE("use returns false for null key") {
        bool called = false;
        CHECK(not map.use(null_key, [&](int &) { called = true; }));
        CHECK(not called);
    }

    SUBCASE("erase returns false for null key") {
        CHECK(not map.erase(null_key));
    }
}

// ============================================================================
// Use Tests
// ============================================================================

TEST_CASE("SlotMap: use invokes callable with value")
{
    SlotMap<Key<16, 16, 0, int>> map;
    auto key = map.emplace(42);

    SUBCASE("non-const use") {
        int value = 0;
        bool found = map.use(key, [&](int & v) { value = v; });
        CHECK(found);
        CHECK(value == 42);
    }

    SUBCASE("const use") {
        int value = 0;
        auto const & cmap = map;
        bool found = cmap.use(key, [&](int const & v) { value = v; });
        CHECK(found);
        CHECK(value == 42);
    }

    SUBCASE("modify through use") {
        map.use(key, [](int & v) { v = 100; });

        int value = 0;
        map.use(key, [&](int const & v) { value = v; });
        CHECK(value == 100);
    }
}

TEST_CASE("SlotMap: use returns false for stale key")
{
    SlotMap<Key<16, 16, 0, int>> map(1u);

    auto key1 = map.emplace(1);
    map.erase(key1);
    map.emplace(2); // Reuses same slot

    bool called = false;
    CHECK(not map.use(key1, [&](int &) { called = true; }));
    CHECK(not called);
}

// ============================================================================
// Contains Tests
// ============================================================================

TEST_CASE("SlotMap: contains")
{
    SlotMap<Key<16, 16, 0, int>> map;

    SUBCASE("returns true for valid key") {
        auto key = map.emplace(42);
        CHECK(map.contains(key));
    }

    SUBCASE("returns false after erase") {
        auto key = map.emplace(42);
        map.erase(key);
        CHECK(not map.contains(key));
    }

    SUBCASE("returns false for stale version") {
        SlotMap<Key<16, 16, 0, int>> small_map(1u);
        auto key1 = small_map.emplace(1);
        small_map.erase(key1);
        auto key2 = small_map.emplace(2);

        CHECK(not small_map.contains(key1));
        CHECK(small_map.contains(key2));
    }
}

// ============================================================================
// Capacity Exhaustion Tests
// ============================================================================

TEST_CASE("SlotMap: capacity exhaustion returns null key")
{
    // Small index space: 8 bits = 256 usable indices (Key<8, 24, 0> = 32 bits)
    SlotMap<Key<8, 24, 0, int>> map(64u);

    using key_type = Key<8, 24, 0, int>;
    std::vector<key_type> keys;
    for (int i = 0; i < 256; ++i) {
        auto key = map.emplace(i);
        CHECK(not key.is_null());
        keys.push_back(key);
    }

    CHECK(map.size().value == 256);

    // 257th emplace should fail
    auto overflow_key = map.emplace(999);
    CHECK(overflow_key.is_null());
    CHECK(map.size().value == 256);
}

TEST_CASE("SlotMap: can reuse slots after erase")
{
    // 8-bit index space = 256 slots
    SlotMap<Key<8, 24, 0, int>> map(64u);

    using key_type = Key<8, 24, 0, int>;

    // Fill up
    std::vector<key_type> keys;
    for (int i = 0; i < 256; ++i) {
        keys.push_back(map.emplace(i));
    }

    // Erase half
    for (std::size_t i = 0; i < 128; ++i) {
        map.erase(keys[i]);
    }

    CHECK(map.size().value == 128);

    // Should be able to add 128 more
    for (int i = 0; i < 128; ++i) {
        auto key = map.emplace(1000 + i);
        CHECK(not key.is_null());
    }

    CHECK(map.size().value == 256);
}

// ============================================================================
// Version Exhaustion Tests
// ============================================================================

TEST_CASE("SlotMap: slot becomes dead after version exhaustion")
{
    // 2-bit version: versions 1, 2, 3; max_version = 3
    // After version 3 is used, slot is dead
    // Key<30, 2, 0> = 32 bits total
    SlotMap<Key<30, 2, 0, int>> map(4u);

    auto key1 = map.emplace(1); // version 1 (first slot of first slab)
    auto idx1 = key1.index();
    CHECK(key1.version().value == 1);
    map.erase(key1);

    auto key2 = map.emplace(2); // version 2, same index
    CHECK(key2.index() == idx1);
    CHECK(key2.version().value == 2);
    map.erase(key2);

    auto key3 = map.emplace(3); // version 3 (max), same index
    CHECK(key3.index() == idx1);
    CHECK(key3.version().value == 3);
    map.erase(key3); // Slot is now dead

    // Next emplace should get different index
    auto key4 = map.emplace(4);
    CHECK(key4.index() != idx1);
}

// ============================================================================
// Move Operations with Data
// ============================================================================

TEST_CASE("SlotMap: move preserves data")
{
    SlotMap<Key<16, 16, 0, int>> map;
    auto key1 = map.emplace(42);
    auto key2 = map.emplace(100);

    SUBCASE("move construction") {
        auto moved = std::move(map);

        CHECK(moved.size().value == 2);
        CHECK(moved.contains(key1));
        CHECK(moved.contains(key2));

        int v1 = 0, v2 = 0;
        moved.use(key1, [&](int const & v) { v1 = v; });
        moved.use(key2, [&](int const & v) { v2 = v; });
        CHECK(v1 == 42);
        CHECK(v2 == 100);
    }

    SUBCASE("move assignment") {
        SlotMap<Key<16, 16, 0, int>> other;
        other = std::move(map);

        CHECK(other.size().value == 2);
        CHECK(other.contains(key1));
        CHECK(other.contains(key2));
    }
}

// ============================================================================
// Property-Based Tests for Core Operations
// ============================================================================

TEST_CASE("SlotMap: property-based emplace/use roundtrip")
{
    rc::check("emplace then use returns same value", []() {
        SlotMap<Key<16, 15, 1, int>> map;
        auto const value = *rc::gen::arbitrary<int>();

        auto key = map.emplace(value);
        RC_ASSERT(not key.is_null());

        int found = 0;
        bool ok = map.use(key, [&](int const & v) { found = v; });
        RC_ASSERT(ok);
        RC_ASSERT(found == value);
    });
}

TEST_CASE("SlotMap: property-based multiple values")
{
    rc::check("multiple emplaces all accessible", []() {
        SlotMap<Key<16, 15, 1, int>> map;
        auto const values = *rc::gen::container<std::vector<int>>(
            100,
            rc::gen::arbitrary<int>());

        std::vector<Key<16, 15, 1, int>> keys;
        for (auto v : values) {
            keys.push_back(map.emplace(v));
        }

        RC_ASSERT(map.size().value == values.size());

        for (std::size_t i = 0; i < values.size(); ++i) {
            int found = 0;
            map.use(keys[i], [&](int const & v) { found = v; });
            RC_ASSERT(found == values[i]);
        }
    });
}

TEST_CASE("SlotMap: property-based erase invalidates key")
{
    rc::check("erased keys are no longer valid", []() {
        SlotMap<Key<16, 15, 1, int>> map;

        auto const count = *rc::gen::inRange<std::size_t>(1, 50);
        std::vector<Key<16, 15, 1, int>> keys;
        for (std::size_t i = 0; i < count; ++i) {
            keys.push_back(map.emplace(static_cast<int>(i)));
        }

        // Erase all
        for (auto key : keys) {
            RC_ASSERT(map.erase(key));
        }

        RC_ASSERT(map.is_empty());

        // None should be valid
        for (auto key : keys) {
            RC_ASSERT(not map.contains(key));
            RC_ASSERT(not map.erase(key));
        }
    });
}

TEST_CASE("SlotMap: property-based interleaved operations")
{
    rc::check("interleaved emplace/erase maintains consistency", []() {
        SlotMap<Key<16, 15, 1, int>> map;
        std::map<Key<16, 15, 1, int>, int> reference;

        auto const ops = *rc::gen::inRange<std::size_t>(10, 100);

        for (std::size_t i = 0; i < ops; ++i) {
            bool do_insert = *rc::gen::arbitrary<bool>();

            if (do_insert || reference.empty()) {
                auto value = *rc::gen::arbitrary<int>();
                auto key = map.emplace(value);
                RC_ASSERT(not key.is_null());
                reference[key] = value;
            } else {
                // Pick a random key to erase
                auto it = reference.begin();
                std::advance(
                    it,
                    *rc::gen::inRange<std::size_t>(0, reference.size()));

                RC_ASSERT(map.erase(it->first));
                reference.erase(it);
            }
        }

        RC_ASSERT(map.size().value == reference.size());

        for (auto const & [key, expected] : reference) {
            int found = 0;
            RC_ASSERT(map.use(key, [&](int const & v) { found = v; }));
            RC_ASSERT(found == expected);
        }
    });
}

// ============================================================================
// for_each Tests
// ============================================================================

TEST_CASE("SlotMap: for_each basic iteration")
{
    SlotMap<Key<16, 16, 0, int>> map;

    SUBCASE("empty map") {
        std::size_t count = 0;
        auto visited = map.for_each([&](int const &) { ++count; });
        CHECK(visited.value == 0);
        CHECK(count == 0);
    }

    SUBCASE("single element") {
        auto key = map.emplace(42);
        std::size_t count = 0;
        int found = 0;
        Key<16, 16, 0, int> found_key;

        map.for_each([&](auto k, int const & v) {
            ++count;
            found = v;
            found_key = k;
        });

        CHECK(count == 1);
        CHECK(found == 42);
        CHECK(found_key == key);
    }

    SUBCASE("multiple elements") {
        std::vector<Key<16, 16, 0, int>> keys;
        for (int i = 0; i < 10; ++i) {
            keys.push_back(map.emplace(i * 10));
        }

        std::set<int> found_values;
        auto visited = map.for_each(
            [&](int const & v) { found_values.insert(v); });

        CHECK(visited.value == 10);
        CHECK(found_values.size() == 10);
        for (int i = 0; i < 10; ++i) {
            CHECK(found_values.count(i * 10) == 1);
        }
    }
}

TEST_CASE("SlotMap: for_each early exit")
{
    SlotMap<Key<16, 16, 0, int>> map;

    for (int i = 0; i < 100; ++i) {
        map.emplace(i);
    }

    SUBCASE("stop after first element") {
        std::size_t count = 0;
        auto visited = map.for_each([&](int const &, Break & brk) {
            ++count;
            brk.stop = true;
        });

        CHECK(visited.value == 1);
        CHECK(count == 1);
    }

    SUBCASE("stop after N elements") {
        std::size_t count = 0;
        auto visited = map.for_each([&](int const &, Break & brk) {
            ++count;
            if (count >= 5) {
                brk.stop = true;
            }
        });

        CHECK(visited.value == 5);
        CHECK(count == 5);
    }
}

TEST_CASE("SlotMap: for_each with const map")
{
    SlotMap<Key<16, 16, 0, int>> map;
    map.emplace(42);
    map.emplace(100);

    auto const & cmap = map;

    std::set<int> found;
    cmap.for_each([&](int const & v) { found.insert(v); });

    CHECK(found.size() == 2);
    CHECK(found.count(42) == 1);
    CHECK(found.count(100) == 1);
}

TEST_CASE("SlotMap: for_each modification through non-const")
{
    SlotMap<Key<16, 16, 0, int>> map;
    auto key1 = map.emplace(10);
    auto key2 = map.emplace(20);

    map.for_each([](int & v) { v *= 2; });

    int v1 = 0, v2 = 0;
    map.use(key1, [&](int const & v) { v1 = v; });
    map.use(key2, [&](int const & v) { v2 = v; });

    CHECK(v1 == 20);
    CHECK(v2 == 40);
}

TEST_CASE("SlotMap: for_each returns valid keys")
{
    SlotMap<Key<16, 16, 0, int>> map;
    std::vector<Key<16, 16, 0, int>> original_keys;
    for (int i = 0; i < 10; ++i) {
        original_keys.push_back(map.emplace(i));
    }

    std::vector<Key<16, 16, 0, int>> iterated_keys;
    map.for_each([&](auto key, int const &) { iterated_keys.push_back(key); });

    // All iterated keys should be valid
    for (auto key : iterated_keys) {
        CHECK(map.contains(key));
    }

    // All iterated keys should match original keys
    std::set<Key<16, 16, 0, int>> original_set(
        original_keys.begin(),
        original_keys.end());
    std::set<Key<16, 16, 0, int>> iterated_set(
        iterated_keys.begin(),
        iterated_keys.end());
    CHECK(original_set == iterated_set);
}

TEST_CASE("SlotMap: for_each with sparse data")
{
    // Create slots then erase some to create gaps
    SlotMap<Key<16, 16, 0, int>> map(4u); // Small slab for testing

    std::vector<Key<16, 16, 0, int>> keys;
    for (int i = 0; i < 10; ++i) {
        keys.push_back(map.emplace(i));
    }

    // Erase every other element
    for (std::size_t i = 0; i < keys.size(); i += 2) {
        map.erase(keys[i]);
    }

    CHECK(map.size().value == 5);

    std::set<int> found;
    auto visited = map.for_each([&](int const & v) { found.insert(v); });

    CHECK(visited.value == 5);
    CHECK(found.size() == 5);
    // Odd values should remain
    for (int i = 1; i < 10; i += 2) {
        CHECK(found.count(i) == 1);
    }
}

// ============================================================================
// clear Tests
// ============================================================================

TEST_CASE("SlotMap: clear basic")
{
    SlotMap<Key<16, 16, 0, int>> map;

    SUBCASE("clear empty map") {
        map.clear();
        CHECK(map.is_empty());
        CHECK(map.size().value == 0);
    }

    SUBCASE("clear single element") {
        auto key = map.emplace(42);
        map.clear();

        CHECK(map.is_empty());
        CHECK(map.size().value == 0);
        CHECK(not map.contains(key));
    }

    SUBCASE("clear multiple elements") {
        std::vector<Key<16, 16, 0, int>> keys;
        for (int i = 0; i < 100; ++i) {
            keys.push_back(map.emplace(i));
        }

        map.clear();

        CHECK(map.is_empty());
        CHECK(map.size().value == 0);

        for (auto key : keys) {
            CHECK(not map.contains(key));
        }
    }
}

TEST_CASE("SlotMap: clear invalidates keys")
{
    SlotMap<Key<16, 16, 0, int>> map;
    auto key1 = map.emplace(42);
    auto key2 = map.emplace(100);

    map.clear();

    // Old keys should be invalid
    CHECK(not map.contains(key1));
    CHECK(not map.contains(key2));

    // New emplace should work
    auto key3 = map.emplace(200);
    CHECK(not key3.is_null());
    CHECK(map.contains(key3));
    CHECK(map.size().value == 1);
}

TEST_CASE("SlotMap: clear allows slot reuse")
{
    SlotMap<Key<16, 16, 0, int>> map(1u); // Single slot slab

    auto key1 = map.emplace(1);
    auto idx1 = key1.index();
    auto ver1 = key1.version();

    map.clear();

    auto key2 = map.emplace(2);
    auto idx2 = key2.index();
    auto ver2 = key2.version();

    // Same slot reused
    CHECK(idx2 == idx1);
    // Version incremented
    CHECK(ver2.value == ver1.value + 1);
}

TEST_CASE("SlotMap: clear destroys values")
{
    static int destructor_count = 0;

    struct Counter
    {
        ~Counter() { ++destructor_count; }
    };

    destructor_count = 0;

    {
        SlotMap<Key<16, 16, 0, Counter>> map;
        map.emplace();
        map.emplace();
        map.emplace();

        CHECK(destructor_count == 0);

        map.clear();

        CHECK(destructor_count == 3);
    }
}

TEST_CASE("SlotMap: clear with partial erases")
{
    SlotMap<Key<16, 16, 0, int>> map(4u);

    std::vector<Key<16, 16, 0, int>> keys;
    for (int i = 0; i < 10; ++i) {
        keys.push_back(map.emplace(i));
    }

    // Erase some
    map.erase(keys[0]);
    map.erase(keys[2]);
    map.erase(keys[4]);

    CHECK(map.size().value == 7);

    map.clear();

    CHECK(map.is_empty());

    // All original keys invalid
    for (auto key : keys) {
        CHECK(not map.contains(key));
    }
}

// ============================================================================
// reset Tests
// ============================================================================

TEST_CASE("SlotMap: reset basic")
{
    SlotMap<Key<16, 16, 0, int>> map;

    SUBCASE("reset empty map") {
        map.reset();
        CHECK(map.is_empty());
        CHECK(map.size().value == 0);
    }

    SUBCASE("reset with elements") {
        for (int i = 0; i < 100; ++i) {
            map.emplace(i);
        }

        map.reset();

        CHECK(map.is_empty());
        CHECK(map.size().value == 0);
    }
}

TEST_CASE("SlotMap: reset destroys values")
{
    static int destructor_count = 0;

    struct Counter
    {
        ~Counter() { ++destructor_count; }
    };

    destructor_count = 0;

    {
        SlotMap<Key<16, 16, 0, Counter>> map;
        map.emplace();
        map.emplace();
        map.emplace();

        CHECK(destructor_count == 0);

        map.reset();

        CHECK(destructor_count == 3);
    }
}

TEST_CASE("SlotMap: reset vs clear behavior")
{
    SlotMap<Key<16, 16, 0, int>> map;

    (void)map.emplace(42);

    // After reset, new emplace should start fresh (version starts at 1 for
    // slot 0)
    map.reset();

    auto key2 = map.emplace(100);
    // After reset, we're back to initial state - first slot gets version 1
    CHECK(key2.index().value == 0);
    CHECK(key2.version().value == 1);
}

// ============================================================================
// Property-Based Tests for for_each/clear/reset
// ============================================================================

TEST_CASE("SlotMap: property-based for_each visits all elements")
{
    rc::check("for_each visits exactly size() elements", []() {
        SlotMap<Key<16, 15, 1, int>> map;
        auto const count = *rc::gen::inRange<std::size_t>(0, 100);

        std::map<Key<16, 15, 1, int>, int> reference;
        for (std::size_t i = 0; i < count; ++i) {
            auto value = *rc::gen::arbitrary<int>();
            auto key = map.emplace(value);
            reference[key] = value;
        }

        std::map<Key<16, 15, 1, int>, int> visited;
        auto num_visited = map.for_each(
            [&](auto key, int const & v) { visited[key] = v; });

        RC_ASSERT(num_visited.value == reference.size());
        RC_ASSERT(visited == reference);
    });
}

TEST_CASE("SlotMap: property-based clear invalidates all keys")
{
    rc::check("clear invalidates all keys", []() {
        SlotMap<Key<16, 15, 1, int>> map;
        auto const count = *rc::gen::inRange<std::size_t>(1, 50);

        std::vector<Key<16, 15, 1, int>> keys;
        for (std::size_t i = 0; i < count; ++i) {
            keys.push_back(map.emplace(*rc::gen::arbitrary<int>()));
        }

        map.clear();

        RC_ASSERT(map.is_empty());
        for (auto key : keys) {
            RC_ASSERT(not map.contains(key));
        }
    });
}

TEST_CASE("SlotMap: property-based reset returns to initial state")
{
    rc::check("reset returns to initial state", []() {
        SlotMap<Key<16, 15, 1, int>> map;
        auto const count = *rc::gen::inRange<std::size_t>(1, 50);

        for (std::size_t i = 0; i < count; ++i) {
            map.emplace(*rc::gen::arbitrary<int>());
        }

        map.reset();

        RC_ASSERT(map.is_empty());

        // After reset, first emplace should get index 0 with version 1
        auto key = map.emplace(42);
        RC_ASSERT(key.index().value == 0);
        RC_ASSERT(key.version().value == 1);
    });
}

TEST_CASE("SlotMap: property-based for_each with sparse map")
{
    rc::check("for_each works with sparse maps", []() {
        SlotMap<Key<16, 15, 1, int>> map;

        std::map<Key<16, 15, 1, int>, int> reference;
        auto const insert_count = *rc::gen::inRange<std::size_t>(10, 50);

        for (std::size_t i = 0; i < insert_count; ++i) {
            auto value = *rc::gen::arbitrary<int>();
            auto key = map.emplace(value);
            reference[key] = value;
        }

        // Erase about half
        auto const erase_count = *rc::gen::inRange<std::size_t>(
            0,
            reference.size());
        for (std::size_t i = 0; i < erase_count && not reference.empty(); ++i) {
            auto it = reference.begin();
            std::advance(
                it,
                *rc::gen::inRange<std::size_t>(0, reference.size()));
            map.erase(it->first);
            reference.erase(it);
        }

        std::map<Key<16, 15, 1, int>, int> visited;
        map.for_each([&](auto key, int const & v) { visited[key] = v; });

        RC_ASSERT(visited == reference);
    });
}

// ============================================================================
// Property-Based Tests for Copy/Swap
// ============================================================================

TEST_CASE("SlotMap: property-based swap preserves data")
{
    rc::check("swap preserves all data in both maps", []() {
        SlotMap<Key<16, 15, 1, int>> map1;
        SlotMap<Key<16, 15, 1, int>> map2;

        std::map<Key<16, 15, 1, int>, int> ref1, ref2;

        auto const count1 = *rc::gen::inRange<std::size_t>(0, 50);
        auto const count2 = *rc::gen::inRange<std::size_t>(0, 50);

        for (std::size_t i = 0; i < count1; ++i) {
            auto value = *rc::gen::arbitrary<int>();
            auto key = map1.emplace(value);
            ref1[key] = value;
        }

        for (std::size_t i = 0; i < count2; ++i) {
            auto value = *rc::gen::arbitrary<int>();
            auto key = map2.emplace(value);
            ref2[key] = value;
        }

        map1.swap(map2);

        // Verify map1 now contains ref2's data
        RC_ASSERT(map1.size().value == ref2.size());
        for (auto const & [key, expected] : ref2) {
            int found = 0;
            RC_ASSERT(map1.use(key, [&](int const & v) { found = v; }));
            RC_ASSERT(found == expected);
        }

        // Verify map2 now contains ref1's data
        RC_ASSERT(map2.size().value == ref1.size());
        for (auto const & [key, expected] : ref1) {
            int found = 0;
            RC_ASSERT(map2.use(key, [&](int const & v) { found = v; }));
            RC_ASSERT(found == expected);
        }
    });
}

TEST_CASE("SlotMap: property-based copy creates exact duplicate")
{
    rc::check("copy constructor creates exact duplicate", []() {
        SlotMap<Key<16, 15, 1, int>> original;
        std::map<Key<16, 15, 1, int>, int> reference;

        auto const count = *rc::gen::inRange<std::size_t>(0, 100);

        for (std::size_t i = 0; i < count; ++i) {
            auto value = *rc::gen::arbitrary<int>();
            auto key = original.emplace(value);
            reference[key] = value;
        }

        SlotMap<Key<16, 15, 1, int>> copy(original);

        // Verify copy has same size
        RC_ASSERT(copy.size().value == reference.size());

        // Verify all keys and values are present
        for (auto const & [key, expected] : reference) {
            int found = 0;
            RC_ASSERT(copy.use(key, [&](int const & v) { found = v; }));
            RC_ASSERT(found == expected);
        }
    });
}

TEST_CASE("SlotMap: property-based copy independence")
{
    rc::check("copy is independent from original", []() {
        SlotMap<Key<16, 15, 1, int>> original;
        std::map<Key<16, 15, 1, int>, int> original_ref;

        auto const count = *rc::gen::inRange<std::size_t>(1, 50);

        for (std::size_t i = 0; i < count; ++i) {
            auto value = *rc::gen::arbitrary<int>();
            auto key = original.emplace(value);
            original_ref[key] = value;
        }

        SlotMap<Key<16, 15, 1, int>> copy(original);

        // Modify copy: erase some and add some
        auto const erase_count = *rc::gen::inRange<std::size_t>(
            0,
            original_ref.size());
        std::vector<Key<16, 15, 1, int>> keys_to_erase;
        for (auto const & [key, _] : original_ref) {
            if (keys_to_erase.size() < erase_count) {
                keys_to_erase.push_back(key);
            }
        }
        for (auto key : keys_to_erase) {
            copy.erase(key);
        }

        auto const add_count = *rc::gen::inRange<std::size_t>(0, 20);
        for (std::size_t i = 0; i < add_count; ++i) {
            copy.emplace(*rc::gen::arbitrary<int>());
        }

        // Original should be unchanged
        RC_ASSERT(original.size().value == original_ref.size());
        for (auto const & [key, expected] : original_ref) {
            int found = 0;
            RC_ASSERT(original.use(key, [&](int const & v) { found = v; }));
            RC_ASSERT(found == expected);
        }
    });
}

TEST_CASE("SlotMap: property-based copy assignment")
{
    rc::check("copy assignment replaces contents", []() {
        SlotMap<Key<16, 15, 1, int>> source;
        SlotMap<Key<16, 15, 1, int>> target;

        std::map<Key<16, 15, 1, int>, int> source_ref;

        auto const source_count = *rc::gen::inRange<std::size_t>(0, 50);
        auto const target_count = *rc::gen::inRange<std::size_t>(0, 50);

        for (std::size_t i = 0; i < source_count; ++i) {
            auto value = *rc::gen::arbitrary<int>();
            auto key = source.emplace(value);
            source_ref[key] = value;
        }

        for (std::size_t i = 0; i < target_count; ++i) {
            target.emplace(*rc::gen::arbitrary<int>());
        }

        target = source;

        // Verify target now matches source
        RC_ASSERT(target.size().value == source_ref.size());
        for (auto const & [key, expected] : source_ref) {
            int found = 0;
            RC_ASSERT(target.use(key, [&](int const & v) { found = v; }));
            RC_ASSERT(found == expected);
        }

        // Source should be unchanged
        RC_ASSERT(source.size().value == source_ref.size());
    });
}

} // anonymous namespace
