// ----------------------------------------------------------------------
// Copyright 2025 Jody Hagins
// Distributed under the MIT Software License
// See accompanying file LICENSE or copy at
// https://opensource.org/licenses/MIT
// ----------------------------------------------------------------------
//
// SlotMap core operation tests: emplace, erase, use, contains,
// slab recycling, version/capacity exhaustion, property-based tests
//

#include "wjh/slotmap/SlotMap.hpp"

#include <cstdint>
#include <map>
#include <string>
#include <type_traits>
#include <vector>

#include "testing/doctest.hpp"
#include "testing/rapidcheck.hpp"

namespace {
using wjh::slotmap::Key;

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
// Slab Recycling Tests
// ============================================================================

TEST_CASE("SlotMap: slab recycling")
{
    // Use 32-bit key with 1-bit version (31 index bits) to force quick version
    // exhaustion. Valid key sizes are 32, 64, 128.
    using TestKey = Key<int, 31, 1>;
    using TestMap = SlotMap<TestKey>;

    SUBCASE("dead slots are not added to free list") {
        TestMap map(4u);

        // Emplace and erase twice to exhaust version on slot 0
        auto key1 = map.emplace(1);
        CHECK(key1.version().value == 1);
        map.erase(key1);

        auto key2 = map.emplace(2);
        // With 1-bit version, slot 0 now at version 1 (max)
        // After erase, it becomes dead
        map.erase(key2);

        // Next emplace should use a different slot
        auto key3 = map.emplace(3);
        CHECK(key3 != TestKey::null());

        // The index should be different since slot 0 is dead
        // (Unless recycling occurred, but we're just testing the slot is dead)
    }

    SUBCASE("slab can be recycled when all slots dead") {
        TestMap map(4u);

        // Exhaust all 4 slots in the slab
        for (int cycle = 0; cycle < 2; ++cycle) {
            for (int i = 0; i < 4; ++i) {
                auto key = map.emplace(i);
                CHECK(key != TestKey::null());
                map.erase(key);
            }
        }

        // All 4 slots should now be dead
        // The slab should be recycled when we try to emplace more

        // Emplace 4 more elements - should succeed via recycling
        std::vector<TestKey> keys;
        for (int i = 0; i < 4; ++i) {
            auto key = map.emplace(i + 100);
            CHECK(key != TestKey::null());
            keys.push_back(key);
        }

        CHECK(map.size().value == 4);

        // All values should be accessible
        for (int i = 0; i < 4; ++i) {
            int val = 0;
            CHECK(
                map.use(keys[static_cast<std::size_t>(i)], [&](int const & v) {
                    val = v;
                }));
            CHECK(val == i + 100);
        }
    }

    SUBCASE("version exhausted key becomes invalid") {
        // Use a key type with more version bits so we can test reuse
        using Key2Bit = Key<int, 30, 2>;
        using Map2Bit = SlotMap<Key2Bit>;
        Map2Bit map(4u);

        // First emplace uses slot 0, version starts at 1
        auto key1 = map.emplace(1);
        CHECK(key1.version().value == 1);
        map.erase(key1);

        // Second emplace reuses slot 0, version is now 2
        auto key2 = map.emplace(2);
        CHECK(key2.index() == key1.index()); // Same slot
        CHECK(key2.version().value == 2);

        // key1 should be invalid (version mismatch)
        CHECK(not map.contains(key1));
        CHECK(map.contains(key2));

        map.erase(key2);

        // key2 should also be invalid now
        CHECK(not map.contains(key2));

        // Third emplace reuses slot 0, version is now 3 (max for 2-bit)
        auto key3 = map.emplace(3);
        CHECK(key3.index() == key1.index());
        CHECK(key3.version().value == 3);

        map.erase(key3);

        // Slot 0 is now dead (version exhausted)
        // Fourth emplace should use a different slot
        auto key4 = map.emplace(4);
        CHECK(key4.index() != key1.index()); // Different slot
        CHECK(key4 != Key2Bit::null());
    }
}

// ============================================================================
// Type Traits
// ============================================================================

TEST_CASE("SlotMap: type traits")
{
    using Map = SlotMap<Key<int, 16, 16>>;

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
        using NonCopyMap = SlotMap<Key<std::unique_ptr<int>, 16, 16>>;
        static_assert(not std::is_copy_constructible_v<NonCopyMap>);
    }

    SUBCASE("is not copy assignable for non-copyable types") {
        using NonCopyMap = SlotMap<Key<std::unique_ptr<int>, 16, 16>>;
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
// Property-Based Tests for Construction
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
            SlotMap<Key<int, 16, 16>> map(size_hint);
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
            SlotMap<Key<int, 16, 16>> map(size);
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
    wjh::SlotMap<wjh::SlotMapKey<int, 16, 16>> map;

    CHECK(map.is_empty());
    CHECK(map.size() == std::uint16_t(0));
}

// ============================================================================
// Emplace Tests
// ============================================================================

TEST_CASE("SlotMap: emplace returns valid key")
{
    SlotMap<Key<int, 16, 16>> map;

    SUBCASE("single emplace") {
        auto key = map.emplace(42);

        CHECK(not key.is_null());
        CHECK(map.contains(key));
        CHECK(map.size().value == 1);
        CHECK(not map.is_empty());
    }

    SUBCASE("emplace with various types") {
        SlotMap<Key<std::string, 16, 16>> str_map;
        auto key = str_map.emplace("hello");

        CHECK(not key.is_null());
        CHECK(str_map.contains(key));
    }

    SUBCASE("emplace with move-only type") {
        SlotMap<Key<std::unique_ptr<int>, 16, 16>> ptr_map;
        auto key = ptr_map.emplace(std::make_unique<int>(42));

        CHECK(not key.is_null());
        CHECK(ptr_map.contains(key));
    }
}

TEST_CASE("SlotMap: emplace constructs value correctly")
{
    SlotMap<Key<int, 16, 16>> map;

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
    SlotMap<Key<int, 16, 16>> map(4u);

    // First emplace should get index 0 but version >= 1
    auto key = map.emplace(1);
    CHECK(key.index().value == 0);
    CHECK(key.version().value >= 1);
}

TEST_CASE("SlotMap: multiple emplaces")
{
    SlotMap<Key<int, 16, 16>> map;
    std::vector<Key<int, 16, 16>> keys;

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
// Try Emplace Tests
// ============================================================================

TEST_CASE("SlotMap: try_emplace returns valid key when capacity available")
{
    SlotMap<Key<int, 16, 16>> map;

    SUBCASE("single try_emplace") {
        auto key = map.try_emplace(42);

        CHECK(not key.is_null());
        CHECK(map.contains(key));
        CHECK(map.size().value == 1);
        CHECK(not map.is_empty());
    }

    SUBCASE("try_emplace with various types") {
        SlotMap<Key<std::string, 16, 16>> str_map;
        auto key = str_map.try_emplace("hello");

        CHECK(not key.is_null());
        CHECK(str_map.contains(key));
    }
}

TEST_CASE("SlotMap: try_emplace returns null key when capacity exhausted")
{
    // Small index space: 8 bits = 256 usable indices
    SlotMap<Key<int, 8, 24>> map(64u);

    using key_type = Key<int, 8, 24>;
    std::vector<key_type> keys;

    // Fill the map to capacity
    for (int i = 0; i < 256; ++i) {
        auto key = map.try_emplace(i);
        CHECK(not key.is_null());
        keys.push_back(key);
    }

    CHECK(map.size().value == 256);

    // Next try_emplace should return null key
    auto overflow_key = map.try_emplace(999);
    CHECK(overflow_key.is_null());
    CHECK(map.size().value == 256);
}

TEST_CASE("SlotMap: try_emplace constructs value correctly")
{
    SlotMap<Key<int, 16, 16>> map;

    auto key = map.try_emplace(42);
    CHECK(not key.is_null());

    bool found = false;
    map.use(key, [&](int const & v) {
        found = true;
        CHECK(v == 42);
    });
    CHECK(found);
}

// ============================================================================
// Emplace vs Try Emplace Tests
// ============================================================================

TEST_CASE("SlotMap: emplace throws when capacity exhausted")
{
    // Small index space: 8 bits = 256 usable indices
    SlotMap<Key<int, 8, 24>> map(64u);

    using key_type = Key<int, 8, 24>;
    std::vector<key_type> keys;

    // Fill the map to capacity
    for (int i = 0; i < 256; ++i) {
        auto key = map.emplace(i);
        CHECK(not key.is_null());
        keys.push_back(key);
    }

    CHECK(map.size().value == 256);

    // Next emplace should throw
    bool threw = false;
    try {
        (void)map.emplace(999);
    } catch (std::length_error const & e) {
        threw = true;
        // Verify the error message is descriptive
        std::string msg = e.what();
        CHECK(msg.find("capacity") != std::string::npos);
    } catch (...) {
        FAIL("emplace threw wrong exception type");
    }

    CHECK(threw);
    CHECK(map.size().value == 256); // Size unchanged
}

TEST_CASE("SlotMap: emplace and try_emplace behave identically when capacity "
          "available")
{
    SlotMap<Key<int, 16, 16>> map1;
    SlotMap<Key<int, 16, 16>> map2;

    // Both should succeed when there's room
    auto key1 = map1.emplace(42);
    auto key2 = map2.try_emplace(42);

    CHECK(not key1.is_null());
    CHECK(not key2.is_null());
    CHECK(map1.size() == map2.size());
    CHECK(map1.contains(key1));
    CHECK(map2.contains(key2));
}

TEST_CASE("SlotMap: emplace throws, try_emplace returns null on exhaustion")
{
    // Very small index space for quick exhaustion
    SlotMap<Key<int, 4, 28>> map1(4u);
    SlotMap<Key<int, 4, 28>> map2(4u);

    // Fill both maps
    for (int i = 0; i < 16; ++i) {
        (void)map1.emplace(i);
        (void)map2.try_emplace(i);
    }

    // emplace should throw
    bool emplace_threw = false;
    try {
        (void)map1.emplace(100);
    } catch (std::length_error const &) {
        emplace_threw = true;
    }
    CHECK(emplace_threw);

    // try_emplace should return null
    auto key = map2.try_emplace(100);
    CHECK(key.is_null());
}

// ============================================================================
// Erase Tests
// ============================================================================

TEST_CASE("SlotMap: erase removes element")
{
    SlotMap<Key<int, 16, 16>> map;

    auto key = map.emplace(42);
    CHECK(map.erase(key));
    CHECK(not map.contains(key));
    CHECK(map.size().value == 0);
    CHECK(map.is_empty());
}

TEST_CASE("SlotMap: erase returns false for invalid key")
{
    SlotMap<Key<int, 16, 16>> map;

    auto key = map.emplace(42);
    map.erase(key);

    // Try erasing already-erased key
    CHECK(not map.erase(key));
}

TEST_CASE("SlotMap: erase returns false for null key")
{
    SlotMap<Key<int, 16, 16>> map;

    CHECK(not map.erase(Key<int, 16, 16>::null()));
}

TEST_CASE("SlotMap: key version increments after erase and re-emplace")
{
    SlotMap<Key<int, 16, 16>> map(1u); // Single slot slab

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
        SlotMap<Key<Counter, 16, 16>> map;
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
    SlotMap<Key<int, 16, 16>> map;
    auto null_key = Key<int, 16, 16>::null();

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
    SlotMap<Key<int, 16, 16>> map;
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
    SlotMap<Key<int, 16, 16>> map(1u);

    auto key1 = map.emplace(1);
    map.erase(key1);
    (void)map.emplace(2); // Reuses same slot

    bool called = false;
    CHECK(not map.use(key1, [&](int &) { called = true; }));
    CHECK(not called);
}

// ============================================================================
// Contains Tests
// ============================================================================

TEST_CASE("SlotMap: contains")
{
    SlotMap<Key<int, 16, 16>> map;

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
        SlotMap<Key<int, 16, 16>> small_map(1u);
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

TEST_CASE("SlotMap: capacity exhaustion with try_emplace returns null key")
{
    // Small index space: 8 bits = 256 usable indices (Key<8, 24, 0> = 32 bits)
    SlotMap<Key<int, 8, 24>> map(64u);

    using key_type = Key<int, 8, 24>;
    std::vector<key_type> keys;
    for (int i = 0; i < 256; ++i) {
        auto key = map.try_emplace(i);
        CHECK(not key.is_null());
        keys.push_back(key);
    }

    CHECK(map.size().value == 256);

    // 257th try_emplace should return null
    auto overflow_key = map.try_emplace(999);
    CHECK(overflow_key.is_null());
    CHECK(map.size().value == 256);
}

TEST_CASE("SlotMap: can reuse slots after erase")
{
    // 8-bit index space = 256 slots
    SlotMap<Key<int, 8, 24>> map(64u);

    using key_type = Key<int, 8, 24>;

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
    SlotMap<Key<int, 30, 2>> map(4u);

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
    SlotMap<Key<int, 16, 16>> map;
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
        SlotMap<Key<int, 16, 16>> other;
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
        SlotMap<Key<int, 16, 15, 1>> map;
        auto const value = *rc::gen::arbitrary<int>();

        auto key = map.emplace(value);
        RC_ASSERT(not key.is_null());

        int found = 0;
        bool ok = map.use(key, [&](int const & v) { found = v; });
        RC_ASSERT(ok);
        RC_ASSERT(found == value);
    });
}

TEST_CASE("SlotMap: property-based try_emplace/use roundtrip")
{
    rc::check("try_emplace then use returns same value", []() {
        SlotMap<Key<int, 16, 15, 1>> map;
        auto const value = *rc::gen::arbitrary<int>();

        auto key = map.try_emplace(value);
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
        SlotMap<Key<int, 16, 15, 1>> map;
        auto const values = *rc::gen::container<std::vector<int>>(
            100,
            rc::gen::arbitrary<int>());

        std::vector<Key<int, 16, 15, 1>> keys;
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
        SlotMap<Key<int, 16, 15, 1>> map;

        auto const count = *rc::gen::inRange<std::size_t>(1, 50);
        std::vector<Key<int, 16, 15, 1>> keys;
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
        SlotMap<Key<int, 16, 15, 1>> map;
        std::map<Key<int, 16, 15, 1>, int> reference;

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
// 16-bit Key SlotMap Tests
// ============================================================================

TEST_CASE("SlotMap 16-bit: value_type is uint16_t")
{
    using K = Key<int, 10, 6>;
    static_assert(std::is_same_v<K::value_type, std::uint16_t>);

    using Map = SlotMap<K>;
    Map map;

    REQUIRE(map.is_empty());
}

TEST_CASE("SlotMap 16-bit: basic emplace and use")
{
    using K = Key<int, 10, 4, 2>;
    SlotMap<K> map;

    SUBCASE("single emplace") {
        auto key = map.emplace(42);

        CHECK(not key.is_null());
        CHECK(map.contains(key));
        CHECK(map.size().value == 1);

        int value = 0;
        bool found = map.use(key, [&](int const & v) { value = v; });
        CHECK(found);
        CHECK(value == 42);
    }

    SUBCASE("multiple emplaces") {
        std::vector<K> keys;
        for (int i = 0; i < 50; ++i) {
            auto key = map.emplace(i * 10);
            CHECK(not key.is_null());
            keys.push_back(key);
        }

        CHECK(map.size().value == 50);

        for (std::size_t i = 0; i < keys.size(); ++i) {
            int value = -1;
            bool found = map.use(keys[i], [&](int const & v) { value = v; });
            CHECK(found);
            CHECK(value == static_cast<int>(i) * 10);
        }
    }
}

TEST_CASE("SlotMap 16-bit: erase and slot reuse")
{
    using K = Key<int, 10, 4, 2>;
    SlotMap<K> map(4u);

    auto key1 = map.emplace(1);
    CHECK(key1.version().value == 1);
    map.erase(key1);

    auto key2 = map.emplace(2);
    // Same index reused, version incremented
    CHECK(key2.index() == key1.index());
    CHECK(key2.version().value == 2);

    // Old key invalid
    CHECK(not map.contains(key1));
    CHECK(map.contains(key2));
}

TEST_CASE("SlotMap 16-bit: version exhaustion")
{
    // 10 index bits, 4 version bits, 2 user bits = 16 bits total
    // 4-bit version: versions 1-15, max_version = 15
    using K = Key<int, 10, 4, 2>;
    SlotMap<K> map(4u);

    auto key1 = map.emplace(1);
    auto idx = key1.index();
    CHECK(key1.version().value == 1);

    // Exhaust all 15 versions on this slot
    for (unsigned v = 2; v <= 15; ++v) {
        map.erase(key1);
        key1 = map.emplace(static_cast<int>(v));
        CHECK(key1.index() == idx);
        CHECK(key1.version().value == v);
    }

    // After erasing version 15, slot is dead
    map.erase(key1);

    // Next emplace should use a different slot
    auto key_new = map.emplace(100);
    CHECK(key_new.index() != idx);
}

TEST_CASE("SlotMap 16-bit: capacity exhaustion with small index space")
{
    // 6 index bits = 64 usable indices
    // 8 version bits, 2 user bits = 16 bits total
    using K = Key<int, 6, 8, 2>;
    SlotMap<K> map(16u);

    std::vector<K> keys;
    for (int i = 0; i < 64; ++i) {
        auto key = map.try_emplace(i);
        CHECK(not key.is_null());
        keys.push_back(key);
    }

    CHECK(map.size().value == 64);

    // 65th try_emplace should return null
    auto overflow_key = map.try_emplace(999);
    CHECK(overflow_key.is_null());
    CHECK(map.size().value == 64);

    // emplace should throw
    bool threw = false;
    try {
        (void)map.emplace(999);
    } catch (std::length_error const &) {
        threw = true;
    }
    CHECK(threw);
}

TEST_CASE("SlotMap 16-bit: different bit configurations")
{
    SUBCASE("8/8/0 configuration") {
        using K = Key<int, 8, 8>;
        SlotMap<K> map;

        auto key = map.emplace(42);
        CHECK(not key.is_null());
        CHECK(map.contains(key));

        int value = 0;
        map.use(key, [&](int const & v) { value = v; });
        CHECK(value == 42);
    }

    SUBCASE("12/4/0 configuration") {
        using K = Key<int, 12, 4>;
        SlotMap<K> map;

        auto key = map.emplace(42);
        CHECK(not key.is_null());

        // 4-bit version starts at 1
        CHECK(key.version().value == 1);
    }

    SUBCASE("6/6/4 configuration") {
        using K = Key<int, 6, 6, 4>;
        SlotMap<K> map;

        auto key = map.emplace(42);
        CHECK(not key.is_null());
        CHECK(key.user() == 0); // User bits default to 0
    }
}

TEST_CASE("SlotMap 16-bit: type traits")
{
    using Map = SlotMap<Key<int, 10, 6>>;

    static_assert(std::is_default_constructible_v<Map>);
    static_assert(std::is_copy_constructible_v<Map>);
    static_assert(std::is_copy_assignable_v<Map>);
    static_assert(std::is_move_constructible_v<Map>);
    static_assert(std::is_move_assignable_v<Map>);
    static_assert(std::is_nothrow_move_constructible_v<Map>);
    static_assert(std::is_nothrow_move_assignable_v<Map>);

    REQUIRE(true);
}

TEST_CASE("SlotMap 16-bit: move operations preserve data")
{
    using K = Key<int, 10, 6>;
    SlotMap<K> map;

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
        SlotMap<K> other;
        other = std::move(map);

        CHECK(other.size().value == 2);
        CHECK(other.contains(key1));
        CHECK(other.contains(key2));
    }
}

TEST_CASE("SlotMap 16-bit: null key handling")
{
    using K = Key<int, 10, 6>;
    SlotMap<K> map;
    auto null_key = K::null();

    CHECK(not map.contains(null_key));

    bool called = false;
    CHECK(not map.use(null_key, [&](int &) { called = true; }));
    CHECK(not called);

    CHECK(not map.erase(null_key));
}

TEST_CASE("SlotMap 16-bit: property-based emplace/use roundtrip")
{
    rc::check("16-bit: emplace then use returns same value", []() {
        using K = Key<int, 10, 4, 2>;
        SlotMap<K> map;
        auto const value = *rc::gen::arbitrary<int>();

        auto key = map.emplace(value);
        RC_ASSERT(not key.is_null());

        int found = 0;
        bool ok = map.use(key, [&](int const & v) { found = v; });
        RC_ASSERT(ok);
        RC_ASSERT(found == value);
    });
}

TEST_CASE("SlotMap 16-bit: property-based multiple values")
{
    rc::check("16-bit: multiple emplaces all accessible", []() {
        using K = Key<int, 10, 4, 2>;
        SlotMap<K> map;

        // Limited to 50 to stay well within 10-bit index space (1024)
        auto const values = *rc::gen::container<std::vector<int>>(
            50,
            rc::gen::arbitrary<int>());

        std::vector<K> keys;
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

TEST_CASE("SlotMap 16-bit: property-based interleaved operations")
{
    rc::check("16-bit: interleaved emplace/erase maintains consistency", []() {
        using K = Key<int, 10, 4, 2>;
        SlotMap<K> map;
        std::map<K, int> reference;

        auto const ops = *rc::gen::inRange<std::size_t>(10, 50);

        for (std::size_t i = 0; i < ops; ++i) {
            bool do_insert = *rc::gen::arbitrary<bool>();

            if (do_insert || reference.empty()) {
                auto value = *rc::gen::arbitrary<int>();
                auto key = map.emplace(value);
                RC_ASSERT(not key.is_null());
                reference[key] = value;
            } else {
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

} // anonymous namespace
