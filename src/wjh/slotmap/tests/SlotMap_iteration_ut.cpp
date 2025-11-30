// ----------------------------------------------------------------------
// Copyright 2025 Jody Hagins
// Distributed under the MIT Software License
// See accompanying file LICENSE or copy at
// https://opensource.org/licenses/MIT
// ----------------------------------------------------------------------

#include "wjh/slotmap/SlotMap.hpp"

#include <map>
#include <set>
#include <string>
#include <type_traits>
#include <vector>

#include "testing/doctest.hpp"
#include "testing/rapidcheck.hpp"

namespace {
using wjh::slotmap::Break;
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
// for_each Tests
// ============================================================================

TEST_CASE("for_each basic iteration")
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

TEST_CASE("for_each early exit")
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

TEST_CASE("for_each with const map")
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

TEST_CASE("for_each modification through non-const")
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

TEST_CASE("for_each returns valid keys")
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

TEST_CASE("for_each with sparse data")
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

TEST_CASE("clear basic")
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

TEST_CASE("clear invalidates keys")
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

TEST_CASE("clear allows slot reuse")
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

TEST_CASE("clear destroys values")
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

TEST_CASE("clear with partial erases")
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

TEST_CASE("reset basic")
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

TEST_CASE("reset destroys values")
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

TEST_CASE("reset vs clear behavior")
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

TEST_CASE("property-based for_each visits all elements")
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

TEST_CASE("property-based clear invalidates all keys")
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

TEST_CASE("property-based reset returns to initial state")
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

TEST_CASE("property-based for_each with sparse map")
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

TEST_CASE("property-based swap preserves data")
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

TEST_CASE("property-based copy creates exact duplicate")
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

TEST_CASE("property-based copy independence")
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

TEST_CASE("property-based copy assignment")
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

// ============================================================================
// Property-Based Tests for Pop
// ============================================================================

TEST_CASE("property-based pop returns correct values")
{
    rc::check("pop returns the correct value", []() {
        SlotMap<Key<16, 15, 1, int>> map;
        std::map<Key<16, 15, 1, int>, int> reference;

        auto const count = *rc::gen::inRange<std::size_t>(1, 50);

        for (std::size_t i = 0; i < count; ++i) {
            auto value = *rc::gen::arbitrary<int>();
            auto key = map.emplace(value);
            reference[key] = value;
        }

        // Pop random elements
        auto const pop_count = *rc::gen::inRange<std::size_t>(1, count + 1);
        std::vector<Key<16, 15, 1, int>> keys_to_pop;
        for (auto const & [key, _] : reference) {
            if (keys_to_pop.size() < pop_count) {
                keys_to_pop.push_back(key);
            }
        }

        for (auto key : keys_to_pop) {
            auto result = map.pop(key);
            RC_ASSERT(result.has_value());
            RC_ASSERT(result.value() == reference[key]);
            reference.erase(key);
        }

        RC_ASSERT(map.size().value == reference.size());

        // Remaining elements should still be accessible
        for (auto const & [key, expected] : reference) {
            int found = 0;
            RC_ASSERT(map.use(key, [&](int const & v) { found = v; }));
            RC_ASSERT(found == expected);
        }
    });
}

TEST_CASE("property-based pop vs erase equivalence")
{
    rc::check("pop and erase have same effect on map state", []() {
        SlotMap<Key<16, 15, 1, int>> map1;
        SlotMap<Key<16, 15, 1, int>> map2;

        auto const count = *rc::gen::inRange<std::size_t>(1, 30);
        std::vector<Key<16, 15, 1, int>> keys;

        // Build identical maps
        for (std::size_t i = 0; i < count; ++i) {
            auto value = *rc::gen::arbitrary<int>();
            auto key1 = map1.emplace(value);
            auto key2 = map2.emplace(value);
            RC_ASSERT(key1 == key2);
            keys.push_back(key1);
        }

        // Remove some elements: use pop on map1, erase on map2
        auto const remove_count = *rc::gen::inRange<std::size_t>(0, count);
        for (std::size_t i = 0; i < remove_count; ++i) {
            auto key = keys[i];
            (void)map1.pop(key);
            map2.erase(key);
        }

        // Maps should have same size
        RC_ASSERT(map1.size().value == map2.size().value);

        // Same keys should be valid/invalid in both
        for (auto key : keys) {
            RC_ASSERT(map1.contains(key) == map2.contains(key));
        }
    });
}

} // anonymous namespace
