// ----------------------------------------------------------------------
// Copyright 2025 Jody Hagins
// Distributed under the MIT Software License
// See accompanying file LICENSE or copy at
// https://opensource.org/licenses/MIT
// ----------------------------------------------------------------------

#include "wjh/slotmap/SlotMap.hpp"

#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

#include "testing/doctest.hpp"
#include "testing/rapidcheck.hpp"

namespace {
using wjh::slotmap::Key;
using wjh::slotmap::Options;
using namespace wjh::slotmap::literals;

// Test helper that adds convenient constructors accepting raw integers.
// Named distinctly from wjh::slotmap::SlotMap to avoid confusion.
template <typename KeyT>
class TestSlotMap
: public wjh::slotmap::SlotMap<KeyT>
{
    using Base = wjh::slotmap::SlotMap<KeyT>;

public:
    using size_type = typename Base::size_type;
    using naked_size_type = typename size_type::value_type;
    using Base::Base;

    template <typename SizeT>
    explicit TestSlotMap(SizeT slots_per_slab)
    requires std::is_integral_v<SizeT>
    : Base(size_type{static_cast<naked_size_type>(slots_per_slab)})
    { }
};

// ============================================================================
// Exception Safety Tests
// ============================================================================

// Helper type that throws on construction after N successful constructions
struct ThrowOnConstruct
{
    inline static int constructions_allowed = 0;
    inline static int destruction_count = 0;
    int value;

    explicit ThrowOnConstruct(int v)
    : value(v)
    {
        if (constructions_allowed <= 0) {
            throw std::runtime_error("ThrowOnConstruct: no more allowed");
        }
        --constructions_allowed;
    }

    ThrowOnConstruct(ThrowOnConstruct const & other)
    : ThrowOnConstruct(other.value)
    { }

    ThrowOnConstruct & operator = (ThrowOnConstruct const &) = default;

    ~ThrowOnConstruct() { ++destruction_count; }

    static void reset(int allowed)
    {
        constructions_allowed = allowed;
        destruction_count = 0;
    }
};

TEST_CASE("SlotMap: exception safety - emplace strong guarantee")
{
    ThrowOnConstruct::reset(5);

    TestSlotMap<Key<ThrowOnConstruct, 16_ib, 16_vb>> map;

    // Emplace 5 elements successfully
    std::vector<Key<ThrowOnConstruct, 16_ib, 16_vb>> keys;
    for (int i = 0; i < 5; ++i) {
        auto key = map.emplace(i);
        CHECK(not key.is_null());
        keys.push_back(key);
    }

    CHECK(map.size().value == 5);
    auto const size_before = map.size();

    // Next emplace should throw
    bool threw = false;
    try {
        (void)map.emplace(99);
    } catch (std::runtime_error const &) {
        threw = true;
    }

    CHECK(threw);

    // Strong guarantee: size unchanged, all existing elements still valid
    CHECK(map.size() == size_before);
    for (auto key : keys) {
        CHECK(map.contains(key));
    }
}

TEST_CASE("SlotMap: exception safety - copy constructor strong guarantee")
{
    TestSlotMap<Key<ThrowOnConstruct, 16_ib, 16_vb>> original;

    // Build original with 10 elements
    ThrowOnConstruct::reset(100);
    std::vector<Key<ThrowOnConstruct, 16_ib, 16_vb>> original_keys;
    for (int i = 0; i < 10; ++i) {
        auto key = original.emplace(i);
        CHECK(not key.is_null());
        original_keys.push_back(key);
    }

    SUBCASE("copy succeeds when enough constructions allowed") {
        ThrowOnConstruct::reset(100);
        TestSlotMap<Key<ThrowOnConstruct, 16_ib, 16_vb>> copy(original);

        CHECK(copy.size().value == 10);
        for (auto key : original_keys) {
            CHECK(copy.contains(key));
        }
    }

    SUBCASE("copy throws when not enough constructions allowed") {
        ThrowOnConstruct::reset(5); // Only allow 5 copies, but need 10

        bool threw = false;
        try {
            TestSlotMap<Key<ThrowOnConstruct, 16_ib, 16_vb>> copy(original);
            (void)copy;
        } catch (std::runtime_error const &) {
            threw = true;
        }

        CHECK(threw);

        // Original should be unchanged
        CHECK(original.size().value == 10);
        for (auto key : original_keys) {
            CHECK(original.contains(key));
        }
    }
}

TEST_CASE("SlotMap: exception safety - copy assignment strong guarantee")
{
    ThrowOnConstruct::reset(100);

    TestSlotMap<Key<ThrowOnConstruct, 16_ib, 16_vb>> source;
    TestSlotMap<Key<ThrowOnConstruct, 16_ib, 16_vb>> target;

    // Build source
    std::vector<Key<ThrowOnConstruct, 16_ib, 16_vb>> source_keys;
    for (int i = 0; i < 10; ++i) {
        source_keys.push_back(source.emplace(i));
    }

    // Build target
    std::vector<Key<ThrowOnConstruct, 16_ib, 16_vb>> target_keys;
    for (int i = 0; i < 5; ++i) {
        target_keys.push_back(target.emplace(100 + i));
    }

    SUBCASE("copy assignment succeeds") {
        ThrowOnConstruct::reset(100);

        target = source;

        CHECK(target.size().value == 10);
        for (auto key : source_keys) {
            CHECK(target.contains(key));
        }
    }

    SUBCASE("copy assignment throws - target unchanged (strong guarantee)") {
        ThrowOnConstruct::reset(3); // Not enough for full copy

        auto const target_size_before = target.size();

        bool threw = false;
        try {
            target = source;
        } catch (std::runtime_error const &) {
            threw = true;
        }

        CHECK(threw);

        // Target should be unchanged due to copy-and-swap
        CHECK(target.size() == target_size_before);
        for (auto key : target_keys) {
            CHECK(target.contains(key));
        }
    }
}

// ============================================================================
// Edge Case Tests
// ============================================================================

TEST_CASE("SlotMap: edge case - 1-bit version field")
{
    // Key<31, 1_ib, 0_vb> = 32
    // bits total With 1-bit version, max_version = 1, so slots become dead
    // quickly
    TestSlotMap<Key<int, 31_ib, 1_vb>> map(4u);

    SUBCASE("slot becomes dead after single erase - can still emplace more") {
        auto key1 = map.emplace(1);
        CHECK(not key1.is_null());
        CHECK(map.contains(key1));

        map.erase(key1);
        CHECK(not map.contains(key1));

        // Even with 1-bit version causing quick slot death, we should still be
        // able to emplace more elements (from other slots or recycled slabs)
        auto key2 = map.try_emplace(2);
        CHECK(not key2.is_null());
        CHECK(map.contains(key2));

        // The old key should remain invalid
        CHECK(not map.contains(key1));
    }

    SUBCASE("continued emplace/erase works even with rapid slot death") {
        // With 1-bit version, slots die quickly. But recycling should allow
        // continued use. Test that we can do many emplace/erase cycles.
        for (int cycle = 0; cycle < 20; ++cycle) {
            auto key = map.emplace(cycle);
            CHECK(not key.is_null());
            CHECK(map.contains(key));

            int val = 0;
            (void)map.use(key, [&](int const & v) { val = v; });
            CHECK(val == cycle);

            map.erase(key);
            CHECK(not map.contains(key));
        }
    }
}

TEST_CASE("SlotMap: edge case - 1-bit index field")
{
    // Key<1, 31_ib, 0_vb> = 32
    // bits, only 2 possible indices (0 and 1)
    TestSlotMap<Key<int, 1_ib, 31_vb>> map(2u); // 2 slots per slab (max)

    SUBCASE("can only hold 2 elements") {
        auto key0 = map.emplace(0);
        auto key1 = map.emplace(1);

        CHECK(not key0.is_null());
        CHECK(not key1.is_null());
        CHECK(map.size().value == 2);

        // Third emplace should fail
        auto key2 = map.try_emplace(2);
        CHECK(key2.is_null());
    }

    SUBCASE("indices are 0 and 1") {
        auto key0 = map.emplace(0);
        auto key1 = map.emplace(1);

        // One should have index 0, other index 1
        std::set<unsigned> indices{key0.index().value, key1.index().value};
        CHECK(indices.count(0) == 1);
        CHECK(indices.count(1) == 1);
    }
}

TEST_CASE("SlotMap: edge case - maximum version values")
{
    // Key<24, 8_ib, 0_vb> = 32
    // bits, 8-bit version = max 255 After max_version uses of a slot, it
    // becomes dead
    TestSlotMap<Key<int, 24_ib, 8_vb>> map(1u); // 1 slot per slab

    SUBCASE("many emplace/erase cycles work correctly") {
        // With 8-bit version (max 255), a single slot can be reused many times.
        // After exhaustion, recycling allows continued operation.
        // Test that we can do 300+ cycles without failure.
        std::map<Key<int, 24_ib, 8_vb>, int> live_keys;

        for (int cycle = 0; cycle < 300; ++cycle) {
            auto key = map.emplace(cycle);
            CHECK(not key.is_null());
            CHECK(map.contains(key));

            // Verify the value is correct
            int val = 0;
            CHECK(map.use(key, [&](int const & v) { val = v; }));
            CHECK(val == cycle);

            map.erase(key);
            CHECK(not map.contains(key));
        }
    }

    SUBCASE("erased keys remain invalid after many cycles") {
        // Collect some keys, erase them, then do many more cycles.
        // The old keys should remain invalid throughout.
        std::vector<Key<int, 24_ib, 8_vb>> old_keys;

        for (int i = 0; i < 10; ++i) {
            auto key = map.emplace(i);
            CHECK(not key.is_null());
            old_keys.push_back(key);
            map.erase(key);
        }

        // Do many more cycles
        for (int cycle = 0; cycle < 300; ++cycle) {
            auto key = map.emplace(cycle + 100);
            CHECK(not key.is_null());
            map.erase(key);
        }

        // All old keys should still be invalid
        for (auto const & key : old_keys) {
            CHECK(not map.contains(key));
        }
    }
}

TEST_CASE("SlotMap: edge case - single slot slab")
{
    TestSlotMap<Key<int, 16_ib, 16_vb>> map(1u);

    SUBCASE("single element operations") {
        auto key = map.emplace(42);
        CHECK(not key.is_null());
        CHECK(map.size().value == 1);

        int val = 0;
        (void)map.use(key, [&](int const & v) { val = v; });
        CHECK(val == 42);

        map.erase(key);
        CHECK(map.is_empty());
    }

    SUBCASE("multiple slabs needed for multiple elements") {
        std::vector<Key<int, 16_ib, 16_vb>> keys;
        for (int i = 0; i < 10; ++i) {
            auto key = map.emplace(i);
            CHECK(not key.is_null());
            keys.push_back(key);
        }

        // Each element requires its own slab
        CHECK(map.size().value == 10);

        // All elements accessible
        for (int i = 0; i < 10; ++i) {
            int val = 0;
            (void)map.use(
                keys[static_cast<std::size_t>(i)],
                [&](int const & v) { val = v; });
            CHECK(val == i);
        }
    }
}

TEST_CASE("SlotMap: edge case - maximum slab size")
{
    // Use 8-bit index = 256 max slots, with slab size = 256
    // Max slab size for 8-bit index
    TestSlotMap<Key<int, 8_ib, 24_vb>> map(256u);

    SUBCASE("single slab holds all indices") {
        for (int i = 0; i < 256; ++i) {
            auto key = map.emplace(i);
            CHECK(not key.is_null());
        }

        CHECK(map.size().value == 256);

        // 257th should fail
        auto overflow = map.try_emplace(999);
        CHECK(overflow.is_null());
    }
}

TEST_CASE("SlotMap: edge case - very small key (4+4+24=32)")
{
    // 4-bit index = 16 slots, 4-bit version = 16 versions
    TestSlotMap<Key<int, 4_ib, 4_vb, 24_ub>> map(4u);

    SUBCASE("16 indices available") {
        std::vector<Key<int, 4_ib, 4_vb, 24_ub>> keys;
        for (int i = 0; i < 16; ++i) {
            auto key = map.emplace(i);
            CHECK(not key.is_null());
            keys.push_back(key);
        }

        CHECK(map.size().value == 16);

        // 17th should fail
        auto overflow = map.try_emplace(999);
        CHECK(overflow.is_null());
    }

    SUBCASE("4-bit version allows many emplace/erase cycles") {
        // With 4-bit version (max 15), each slot can be reused several times.
        // Test that we can do more cycles than max_version without failure.
        std::vector<Key<int, 4_ib, 4_vb, 24_ub>> erased_keys;

        for (int cycle = 0; cycle < 50; ++cycle) {
            auto key = map.emplace(cycle);
            CHECK(not key.is_null());
            CHECK(map.contains(key));

            int val = 0;
            CHECK(map.use(key, [&](int const & v) { val = v; }));
            CHECK(val == cycle);

            erased_keys.push_back(key);
            map.erase(key);
            CHECK(not map.contains(key));
        }

        // All erased keys should remain invalid
        for (auto const & key : erased_keys) {
            CHECK(not map.contains(key));
        }
    }
}

TEST_CASE("SlotMap: edge case - 64-bit key")
{
    // Key<20, 20_ib, 24_vb> = 64
    // bits
    TestSlotMap<Key<int, 20_ib, 20_vb, 24_ub>> map(1024u);

    SUBCASE("basic operations work with 64-bit key") {
        auto key = map.emplace(42);
        CHECK(not key.is_null());
        CHECK(map.contains(key));

        int val = 0;
        (void)map.use(key, [&](int const & v) { val = v; });
        CHECK(val == 42);

        map.erase(key);
        CHECK(not map.contains(key));
    }

    SUBCASE("user bits accessible") {
        auto key = map.emplace(42);
        CHECK(key.user().value == 0);

        // Create new key with different user bits
        auto key_with_user = key.with_user(
            Key<int, 20_ib, 20_vb, 24_ub>::user_type{0xFFu});
        CHECK(key_with_user.user().value == 0xFF);

        // Original key still works
        CHECK(map.contains(key));
        // Modified key also works (user bits ignored in lookup)
        CHECK(map.contains(key_with_user));
    }
}

// ============================================================================
// Static Assertions for Type Traits
// ============================================================================

TEST_CASE("SlotMap: static assertions for type traits")
{
    using Map32 = TestSlotMap<Key<int, 16_ib, 16_vb>>;
    using Map64 = TestSlotMap<Key<int, 20_ib, 20_vb, 24_ub>>;
    using MapString = TestSlotMap<Key<std::string, 16_ib, 16_vb>>;
    using MapUniquePtr = TestSlotMap<Key<std::unique_ptr<int>, 16_ib, 16_vb>>;

    // Default constructible
    static_assert(std::is_default_constructible_v<Map32>);
    static_assert(std::is_default_constructible_v<Map64>);
    static_assert(std::is_default_constructible_v<MapString>);
    static_assert(std::is_default_constructible_v<MapUniquePtr>);

    // Move constructible (all)
    static_assert(std::is_move_constructible_v<Map32>);
    static_assert(std::is_move_constructible_v<Map64>);
    static_assert(std::is_move_constructible_v<MapString>);
    static_assert(std::is_move_constructible_v<MapUniquePtr>);

    // Move assignable (all)
    static_assert(std::is_move_assignable_v<Map32>);
    static_assert(std::is_move_assignable_v<Map64>);
    static_assert(std::is_move_assignable_v<MapString>);
    static_assert(std::is_move_assignable_v<MapUniquePtr>);

    // Nothrow move (all)
    static_assert(std::is_nothrow_move_constructible_v<Map32>);
    static_assert(std::is_nothrow_move_constructible_v<Map64>);
    static_assert(std::is_nothrow_move_constructible_v<MapString>);
    static_assert(std::is_nothrow_move_constructible_v<MapUniquePtr>);

    static_assert(std::is_nothrow_move_assignable_v<Map32>);
    static_assert(std::is_nothrow_move_assignable_v<Map64>);
    static_assert(std::is_nothrow_move_assignable_v<MapString>);
    static_assert(std::is_nothrow_move_assignable_v<MapUniquePtr>);

    // Copy constructible (only if T is copyable)
    static_assert(std::is_copy_constructible_v<Map32>);
    static_assert(std::is_copy_constructible_v<Map64>);
    static_assert(std::is_copy_constructible_v<MapString>);
    static_assert(not std::is_copy_constructible_v<MapUniquePtr>);

    // Copy assignable (only if T is copyable)
    static_assert(std::is_copy_assignable_v<Map32>);
    static_assert(std::is_copy_assignable_v<Map64>);
    static_assert(std::is_copy_assignable_v<MapString>);
    static_assert(not std::is_copy_assignable_v<MapUniquePtr>);

    // Copy is not nothrow (vector operations may throw even for int)
    static_assert(not std::is_nothrow_copy_constructible_v<Map32>);
    static_assert(not std::is_nothrow_copy_constructible_v<MapString>);

    REQUIRE(true);
}

TEST_CASE("SlotMap: static assertions for key traits")
{
    // All keys should be trivially copyable
    static_assert(std::is_trivially_copyable_v<Key<int, 16_ib, 16_vb>>);
    static_assert(std::is_trivially_copyable_v<Key<int, 20_ib, 20_vb, 24_ub>>);
    static_assert(std::is_trivially_copyable_v<Key<int, 31_ib, 1_vb>>);
    static_assert(std::is_trivially_copyable_v<Key<int, 1_ib, 31_vb>>);

    // is_key_v trait
    static_assert(wjh::slotmap::is_key_v<Key<int, 16_ib, 16_vb>>);
    static_assert(
        wjh::slotmap::is_key_v<Key<std::string, 20_ib, 20_vb, 24_ub>>);
    static_assert(not wjh::slotmap::is_key_v<int>);
    static_assert(not wjh::slotmap::is_key_v<std::string>);

    // Key sizes match expected
    static_assert(sizeof(Key<int, 16_ib,
                             16_vb>) == 4); // 32 bits
    static_assert(sizeof(Key<int, 8_ib, 8_vb,
                             16_ub>) == 4); // 32 bits
    static_assert(sizeof(Key<int, 20_ib, 20_vb,
                             24_ub>) == 8); // 64 bits
    static_assert(sizeof(Key<int, 32_ib,
                             32_vb>) == 8); // 64 bits

    // Comparison operators
    static_assert(std::totally_ordered<Key<int, 16_ib, 16_vb>>);
    static_assert(std::equality_comparable<Key<int, 16_ib, 16_vb>>);

    REQUIRE(true);
}

// ============================================================================
// Additional Property-Based Tests
// ============================================================================

TEST_CASE("SlotMap: property-based version exhaustion and recycling")
{
    rc::check("version exhaustion triggers slot death", []() {
        // Use 2-bit version for quick exhaustion
        TestSlotMap<Key<int, 30_ib, 2_vb>> map(4u);

        auto const initial_count = *rc::gen::inRange<std::size_t>(1, 4);

        // Emplace initial elements
        std::vector<Key<int, 30_ib, 2_vb>> keys;
        for (std::size_t i = 0; i < initial_count; ++i) {
            auto key = map.emplace(static_cast<int>(i));
            RC_ASSERT(not key.is_null());
            keys.push_back(key);
        }

        // Erase and re-emplace until version exhaustion (3 cycles for 2-bit)
        for (int cycle = 0; cycle < 3; ++cycle) {
            for (auto & key : keys) {
                if (map.contains(key)) {
                    map.erase(key);
                    auto new_key = map.emplace(cycle * 100);
                    RC_ASSERT(not new_key.is_null());
                    key = new_key;
                }
            }
        }

        // All elements should still be accessible
        RC_ASSERT(map.size().value == initial_count);
    });
}

TEST_CASE("SlotMap: property-based clear preserves structure")
{
    rc::check("clear followed by refill works correctly", []() {
        TestSlotMap<Key<int, 16_ib, 15_vb, 1_ub>> map;

        auto const count1 = *rc::gen::inRange<std::size_t>(0, 50);
        auto const count2 = *rc::gen::inRange<std::size_t>(0, 50);

        // Fill first time
        std::vector<Key<int, 16_ib, 15_vb, 1_ub>> keys1;
        for (std::size_t i = 0; i < count1; ++i) {
            keys1.push_back(map.emplace(static_cast<int>(i)));
        }
        RC_ASSERT(map.size().value == count1);

        // Clear
        map.clear();
        RC_ASSERT(map.is_empty());

        // All old keys should be invalid
        for (auto key : keys1) {
            RC_ASSERT(not map.contains(key));
        }

        // Fill second time
        std::vector<Key<int, 16_ib, 15_vb, 1_ub>> keys2;
        std::map<Key<int, 16_ib, 15_vb, 1_ub>, int> reference;
        for (std::size_t i = 0; i < count2; ++i) {
            auto key = map.emplace(static_cast<int>(i + 1000));
            RC_ASSERT(not key.is_null());
            keys2.push_back(key);
            reference[key] = static_cast<int>(i + 1000);
        }
        RC_ASSERT(map.size().value == count2);

        // All new keys should be valid with correct values
        for (auto const & [key, expected] : reference) {
            int found = 0;
            RC_ASSERT(map.use(key, [&](int const & v) { found = v; }));
            RC_ASSERT(found == expected);
        }
    });
}

TEST_CASE("SlotMap: property-based for_each early exit")
{
    rc::check("for_each early exit visits correct count", []() {
        TestSlotMap<Key<int, 16_ib, 15_vb, 1_ub>> map;

        auto const count = *rc::gen::inRange<std::size_t>(1, 100);
        auto const stop_at = *rc::gen::inRange<std::size_t>(1, count + 1);

        for (std::size_t i = 0; i < count; ++i) {
            (void)map.emplace(static_cast<int>(i));
        }

        std::size_t visited = 0;
        auto result = map.for_each([&](int const &, Options & opts) {
            ++visited;
            if (visited >= stop_at) {
                opts.stop = true;
            }
        });

        RC_ASSERT(result.value == stop_at);
        RC_ASSERT(visited == stop_at);
    });
}

TEST_CASE("SlotMap: property-based reserve and emplace")
{
    rc::check("reserve allows emplace without reallocation", []() {
        using TestMap = TestSlotMap<Key<int, 12_ib, 12_vb, 8_ub>>;
        TestMap map(64u);

        auto const reserve_count = *rc::gen::inRange<std::size_t>(0, 256);
        auto const emplace_count = *rc::gen::inRange<std::size_t>(0, 256);

        // Reserve capacity
        map.reserve(TestMap::size_type(
            static_cast<TestMap::size_type::value_type>(reserve_count)));

        // Emplace elements
        std::map<Key<int, 12_ib, 12_vb, 8_ub>, int> reference;
        for (std::size_t i = 0;
             i < emplace_count && i < (1u << 12); // Don't exceed index space
             ++i)
        {
            auto key = map.emplace(static_cast<int>(i));
            if (not key.is_null()) {
                reference[key] = static_cast<int>(i);
            }
        }

        // Verify all emplaced elements are accessible
        for (auto const & [key, expected] : reference) {
            int found = 0;
            RC_ASSERT(map.use(key, [&](int const & v) { found = v; }));
            RC_ASSERT(found == expected);
        }
    });
}

} // anonymous namespace
