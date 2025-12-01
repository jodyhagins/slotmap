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
using wjh::slotmap::Key;
using wjh::slotmap::Options;

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
    SlotMap<Key<int, 16, 16>> map;

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
        Key<int, 16, 16> found_key;

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
        std::vector<Key<int, 16, 16>> keys;
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
    SlotMap<Key<int, 16, 16>> map;

    for (int i = 0; i < 100; ++i) {
        (void)map.emplace(i);
    }

    SUBCASE("stop after first element") {
        std::size_t count = 0;
        auto visited = map.for_each([&](int const &, Options & opts) {
            ++count;
            opts.stop = true;
        });

        CHECK(visited.value == 1);
        CHECK(count == 1);
    }

    SUBCASE("stop after N elements") {
        std::size_t count = 0;
        auto visited = map.for_each([&](int const &, Options & opts) {
            ++count;
            if (count >= 5) {
                opts.stop = true;
            }
        });

        CHECK(visited.value == 5);
        CHECK(count == 5);
    }
}

TEST_CASE("for_each early exit via bool return")
{
    SlotMap<Key<int, 16, 16>> map;

    for (int i = 0; i < 100; ++i) {
        (void)map.emplace(i);
    }

    SUBCASE("return false stops after first element - value only") {
        std::size_t count = 0;
        auto visited = map.for_each([&](int const &) {
            ++count;
            return false;
        });

        CHECK(visited.value == 1);
        CHECK(count == 1);
    }

    SUBCASE("return false stops after N elements - value only") {
        std::size_t count = 0;
        auto visited = map.for_each([&](int const &) {
            ++count;
            return count < 5;
        });

        CHECK(visited.value == 5);
        CHECK(count == 5);
    }

    SUBCASE("return true continues iteration - value only") {
        std::size_t count = 0;
        auto visited = map.for_each([&](int const &) {
            ++count;
            return true;
        });

        CHECK(visited.value == 100);
        CHECK(count == 100);
    }

    SUBCASE("return false with key and value") {
        std::size_t count = 0;
        auto visited = map.for_each([&](auto, int const &) {
            ++count;
            return false;
        });

        CHECK(visited.value == 1);
        CHECK(count == 1);
    }

    SUBCASE("return false with value and options") {
        std::size_t count = 0;
        auto visited = map.for_each([&](int const &, Options &) {
            ++count;
            return false;
        });

        CHECK(visited.value == 1);
        CHECK(count == 1);
    }

    SUBCASE("return false with key, value, and options") {
        std::size_t count = 0;
        auto visited = map.for_each([&](auto, int const &, Options &) {
            ++count;
            return false;
        });

        CHECK(visited.value == 1);
        CHECK(count == 1);
    }

    SUBCASE("bool return with erase option") {
        // First 3 elements erased, then stop
        std::size_t count = 0;
        auto visited = map.for_each([&](int const &, Options & opts) {
            ++count;
            opts.erase = true;
            return count < 3;
        });

        CHECK(visited.value == 3);
        CHECK(count == 3);
        CHECK(map.size().value == 97);
    }

    SUBCASE("bool return on const map - value only") {
        auto const & cmap = map;
        std::size_t count = 0;
        auto visited = cmap.for_each([&](int const &) {
            ++count;
            return count < 10;
        });

        CHECK(visited.value == 10);
        CHECK(count == 10);
    }

    SUBCASE("bool return on const map - key and value") {
        auto const & cmap = map;
        std::size_t count = 0;
        auto visited = cmap.for_each([&](auto, int const &) {
            ++count;
            return count < 10;
        });

        CHECK(visited.value == 10);
        CHECK(count == 10);
    }
}

TEST_CASE("for_each with const map")
{
    SlotMap<Key<int, 16, 16>> map;
    (void)map.emplace(42);
    (void)map.emplace(100);

    auto const & cmap = map;

    std::set<int> found;
    cmap.for_each([&](int const & v) { found.insert(v); });

    CHECK(found.size() == 2);
    CHECK(found.count(42) == 1);
    CHECK(found.count(100) == 1);
}

TEST_CASE("for_each modification through non-const")
{
    SlotMap<Key<int, 16, 16>> map;
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
    SlotMap<Key<int, 16, 16>> map;
    std::vector<Key<int, 16, 16>> original_keys;
    for (int i = 0; i < 10; ++i) {
        original_keys.push_back(map.emplace(i));
    }

    std::vector<Key<int, 16, 16>> iterated_keys;
    map.for_each([&](auto key, int const &) { iterated_keys.push_back(key); });

    // All iterated keys should be valid
    for (auto key : iterated_keys) {
        CHECK(map.contains(key));
    }

    // All iterated keys should match original keys
    std::set<Key<int, 16, 16>> original_set(
        original_keys.begin(),
        original_keys.end());
    std::set<Key<int, 16, 16>> iterated_set(
        iterated_keys.begin(),
        iterated_keys.end());
    CHECK(original_set == iterated_set);
}

TEST_CASE("for_each with sparse data")
{
    // Create slots then erase some to create gaps
    SlotMap<Key<int, 16, 16>> map(4u); // Small slab for testing

    std::vector<Key<int, 16, 16>> keys;
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

TEST_CASE("for_each with erase option")
{
    SlotMap<Key<int, 16, 16>> map(4u);

    SUBCASE("erase all elements") {
        for (int i = 0; i < 10; ++i) {
            (void)map.emplace(i);
        }

        auto visited = map.for_each(
            [](int const &, Options & opts) { opts.erase = true; });

        CHECK(visited.value == 10);
        CHECK(map.is_empty());
        CHECK(map.size().value == 0);
    }

    SUBCASE("erase elements matching predicate") {
        std::vector<Key<int, 16, 16>> keys;
        for (int i = 0; i < 10; ++i) {
            keys.push_back(map.emplace(i));
        }

        // Erase odd values
        map.for_each([](int const & v, Options & opts) {
            if (v % 2 == 1) {
                opts.erase = true;
            }
        });

        CHECK(map.size().value == 5);

        // Even values should remain
        for (std::size_t i = 0; i < keys.size(); i += 2) {
            CHECK(map.contains(keys[i]));
        }
        // Odd values should be erased
        for (std::size_t i = 1; i < keys.size(); i += 2) {
            CHECK(not map.contains(keys[i]));
        }
    }

    SUBCASE("erase single element") {
        std::vector<Key<int, 16, 16>> keys;
        for (int i = 0; i < 5; ++i) {
            keys.push_back(map.emplace(i));
        }

        // Erase only value 2
        map.for_each([](int const & v, Options & opts) {
            if (v == 2) {
                opts.erase = true;
            }
        });

        CHECK(map.size().value == 4);
        CHECK(map.contains(keys[0]));
        CHECK(map.contains(keys[1]));
        CHECK(not map.contains(keys[2]));
        CHECK(map.contains(keys[3]));
        CHECK(map.contains(keys[4]));
    }

    SUBCASE("erase with key access") {
        std::vector<Key<int, 16, 16>> keys;
        for (int i = 0; i < 5; ++i) {
            keys.push_back(map.emplace(i * 10));
        }

        Key<int, 16, 16> target_key = keys[2];
        std::set<Key<int, 16, 16>> erased_keys;

        // Erase element with specific key
        map.for_each([&](auto key, int const &, Options & opts) {
            if (key == target_key) {
                opts.erase = true;
                erased_keys.insert(key);
            }
        });

        CHECK(erased_keys.size() == 1);
        CHECK(erased_keys.count(target_key) == 1);
        CHECK(map.size().value == 4);
        CHECK(not map.contains(target_key));
    }

    SUBCASE("erase combined with stop") {
        for (int i = 0; i < 10; ++i) {
            (void)map.emplace(i);
        }

        std::size_t count = 0;
        map.for_each([&](int const &, Options & opts) {
            ++count;
            opts.erase = true;
            if (count >= 3) {
                opts.stop = true;
            }
        });

        // Should have visited and erased exactly 3
        CHECK(count == 3);
        CHECK(map.size().value == 7);
    }

    SUBCASE("erase with value modification before erase") {
        static int destructor_sum = 0;

        struct Value
        {
            int data;

            explicit Value(int d)
            : data(d)
            { }

            ~Value() { destructor_sum += data; }
        };

        destructor_sum = 0;

        {
            SlotMap<Key<Value, 16, 16>> vmap;
            (void)vmap.emplace(1);
            (void)vmap.emplace(2);
            (void)vmap.emplace(3);

            // Modify values before erasing
            vmap.for_each([](Value & v, Options & opts) {
                v.data *= 10;
                opts.erase = true;
            });

            CHECK(vmap.is_empty());
        }

        // Destructors should have been called with modified values
        CHECK(destructor_sum == 60); // 10 + 20 + 30
    }
}

TEST_CASE("for_each erase across multiple slabs")
{
    SlotMap<Key<int, 16, 16>> map(4u); // 4 slots per slab

    // Create 16 elements across 4 slabs
    std::vector<Key<int, 16, 16>> keys;
    for (int i = 0; i < 16; ++i) {
        keys.push_back(map.emplace(i));
    }

    CHECK(map.size().value == 16);

    // Erase elements in first and third slab (indices 0-3 and 8-11)
    map.for_each([](auto key, int const &, Options & opts) {
        auto idx = key.index().value;
        if (idx < 4 || (idx >= 8 && idx < 12)) {
            opts.erase = true;
        }
    });

    CHECK(map.size().value == 8);

    // Verify correct elements remain
    for (std::size_t i = 0; i < 16; ++i) {
        bool should_exist = (i >= 4 && i < 8) || (i >= 12);
        CHECK(map.contains(keys[i]) == should_exist);
    }
}

// ============================================================================
// clear Tests
// ============================================================================

TEST_CASE("clear basic")
{
    SlotMap<Key<int, 16, 16>> map;

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
        std::vector<Key<int, 16, 16>> keys;
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
    SlotMap<Key<int, 16, 16>> map;
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
    SlotMap<Key<int, 16, 16>> map(1u); // Single slot slab

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
        SlotMap<Key<Counter, 16, 16>> map;
        (void)map.emplace();
        (void)map.emplace();
        (void)map.emplace();

        CHECK(destructor_count == 0);

        map.clear();

        CHECK(destructor_count == 3);
    }
}

TEST_CASE("clear with partial erases")
{
    SlotMap<Key<int, 16, 16>> map(4u);

    std::vector<Key<int, 16, 16>> keys;
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
    SlotMap<Key<int, 16, 16>> map;

    SUBCASE("reset empty map") {
        map.reset();
        CHECK(map.is_empty());
        CHECK(map.size().value == 0);
    }

    SUBCASE("reset with elements") {
        for (int i = 0; i < 100; ++i) {
            (void)map.emplace(i);
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
        SlotMap<Key<Counter, 16, 16>> map;
        (void)map.emplace();
        (void)map.emplace();
        (void)map.emplace();

        CHECK(destructor_count == 0);

        map.reset();

        CHECK(destructor_count == 3);
    }
}

TEST_CASE("reset vs clear behavior")
{
    SlotMap<Key<int, 16, 16>> map;

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
        SlotMap<Key<int, 16, 15, 1>> map;
        auto const count = *rc::gen::inRange<std::size_t>(0, 100);

        std::map<Key<int, 16, 15, 1>, int> reference;
        for (std::size_t i = 0; i < count; ++i) {
            auto value = *rc::gen::arbitrary<int>();
            auto key = map.emplace(value);
            reference[key] = value;
        }

        std::map<Key<int, 16, 15, 1>, int> visited;
        auto num_visited = map.for_each(
            [&](auto key, int const & v) { visited[key] = v; });

        RC_ASSERT(num_visited.value == reference.size());
        RC_ASSERT(visited == reference);
    });
}

TEST_CASE("property-based clear invalidates all keys")
{
    rc::check("clear invalidates all keys", []() {
        SlotMap<Key<int, 16, 15, 1>> map;
        auto const count = *rc::gen::inRange<std::size_t>(1, 50);

        std::vector<Key<int, 16, 15, 1>> keys;
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
        SlotMap<Key<int, 16, 15, 1>> map;
        auto const count = *rc::gen::inRange<std::size_t>(1, 50);

        for (std::size_t i = 0; i < count; ++i) {
            (void)map.emplace(*rc::gen::arbitrary<int>());
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
        SlotMap<Key<int, 16, 15, 1>> map;

        std::map<Key<int, 16, 15, 1>, int> reference;
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

        std::map<Key<int, 16, 15, 1>, int> visited;
        map.for_each([&](auto key, int const & v) { visited[key] = v; });

        RC_ASSERT(visited == reference);
    });
}

TEST_CASE("property-based for_each erase by predicate")
{
    rc::check("for_each erase removes matching elements", []() {
        SlotMap<Key<int, 16, 15, 1>> map;
        std::map<Key<int, 16, 15, 1>, int> reference;

        auto const count = *rc::gen::inRange<std::size_t>(1, 100);

        for (std::size_t i = 0; i < count; ++i) {
            auto value = *rc::gen::inRange<int>(0, 1000);
            auto key = map.emplace(value);
            reference[key] = value;
        }

        // Pick a random threshold to erase values below
        auto const threshold = *rc::gen::inRange<int>(0, 1000);

        // Erase via for_each
        map.for_each([threshold](int const & v, Options & opts) {
            if (v < threshold) {
                opts.erase = true;
            }
        });

        // Update reference
        std::erase_if(reference, [threshold](auto const & p) {
            return p.second < threshold;
        });

        // Verify sizes match
        RC_ASSERT(map.size().value == reference.size());

        // Verify remaining elements match
        std::map<Key<int, 16, 15, 1>, int> remaining;
        map.for_each([&](auto key, int const & v) { remaining[key] = v; });
        RC_ASSERT(remaining == reference);
    });
}

TEST_CASE("property-based for_each erase equivalence with manual erase")
{
    rc::check("for_each erase equivalent to collecting and erasing", []() {
        SlotMap<Key<int, 16, 15, 1>> map1;
        SlotMap<Key<int, 16, 15, 1>> map2;

        auto const count = *rc::gen::inRange<std::size_t>(1, 50);

        // Build identical maps
        for (std::size_t i = 0; i < count; ++i) {
            auto value = *rc::gen::inRange<int>(0, 100);
            auto key1 = map1.emplace(value);
            auto key2 = map2.emplace(value);
            RC_ASSERT(key1 == key2);
        }

        auto const threshold = *rc::gen::inRange<int>(0, 100);

        // map1: use for_each erase
        map1.for_each([threshold](int const & v, Options & opts) {
            if (v < threshold) {
                opts.erase = true;
            }
        });

        // map2: collect keys then erase manually
        std::vector<Key<int, 16, 15, 1>> keys_to_erase;
        map2.for_each([&, threshold](auto key, int const & v) {
            if (v < threshold) {
                keys_to_erase.push_back(key);
            }
        });
        for (auto key : keys_to_erase) {
            map2.erase(key);
        }

        // Both should have same size
        RC_ASSERT(map1.size().value == map2.size().value);

        // Both should contain the same elements
        std::map<Key<int, 16, 15, 1>, int> data1, data2;
        map1.for_each([&](auto key, int const & v) { data1[key] = v; });
        map2.for_each([&](auto key, int const & v) { data2[key] = v; });
        RC_ASSERT(data1 == data2);
    });
}

// ============================================================================
// Property-Based Tests for Copy/Swap
// ============================================================================

TEST_CASE("property-based swap preserves data")
{
    rc::check("swap preserves all data in both maps", []() {
        SlotMap<Key<int, 16, 15, 1>> map1;
        SlotMap<Key<int, 16, 15, 1>> map2;

        std::map<Key<int, 16, 15, 1>, int> ref1, ref2;

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
        SlotMap<Key<int, 16, 15, 1>> original;
        std::map<Key<int, 16, 15, 1>, int> reference;

        auto const count = *rc::gen::inRange<std::size_t>(0, 100);

        for (std::size_t i = 0; i < count; ++i) {
            auto value = *rc::gen::arbitrary<int>();
            auto key = original.emplace(value);
            reference[key] = value;
        }

        SlotMap<Key<int, 16, 15, 1>> copy(original);

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
        SlotMap<Key<int, 16, 15, 1>> original;
        std::map<Key<int, 16, 15, 1>, int> original_ref;

        auto const count = *rc::gen::inRange<std::size_t>(1, 50);

        for (std::size_t i = 0; i < count; ++i) {
            auto value = *rc::gen::arbitrary<int>();
            auto key = original.emplace(value);
            original_ref[key] = value;
        }

        SlotMap<Key<int, 16, 15, 1>> copy(original);

        // Modify copy: erase some and add some
        auto const erase_count = *rc::gen::inRange<std::size_t>(
            0,
            original_ref.size());
        std::vector<Key<int, 16, 15, 1>> keys_to_erase;
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
            (void)copy.emplace(*rc::gen::arbitrary<int>());
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
        SlotMap<Key<int, 16, 15, 1>> source;
        SlotMap<Key<int, 16, 15, 1>> target;

        std::map<Key<int, 16, 15, 1>, int> source_ref;

        auto const source_count = *rc::gen::inRange<std::size_t>(0, 50);
        auto const target_count = *rc::gen::inRange<std::size_t>(0, 50);

        for (std::size_t i = 0; i < source_count; ++i) {
            auto value = *rc::gen::arbitrary<int>();
            auto key = source.emplace(value);
            source_ref[key] = value;
        }

        for (std::size_t i = 0; i < target_count; ++i) {
            (void)target.emplace(*rc::gen::arbitrary<int>());
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
        SlotMap<Key<int, 16, 15, 1>> map;
        std::map<Key<int, 16, 15, 1>, int> reference;

        auto const count = *rc::gen::inRange<std::size_t>(1, 50);

        for (std::size_t i = 0; i < count; ++i) {
            auto value = *rc::gen::arbitrary<int>();
            auto key = map.emplace(value);
            reference[key] = value;
        }

        // Pop random elements
        auto const pop_count = *rc::gen::inRange<std::size_t>(1, count + 1);
        std::vector<Key<int, 16, 15, 1>> keys_to_pop;
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
        SlotMap<Key<int, 16, 15, 1>> map1;
        SlotMap<Key<int, 16, 15, 1>> map2;

        auto const count = *rc::gen::inRange<std::size_t>(1, 30);
        std::vector<Key<int, 16, 15, 1>> keys;

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

// ============================================================================
// Property-Based Tests for bool return early exit
// ============================================================================

TEST_CASE("property-based for_each bool return early exit")
{
    rc::check("bool return stops at correct count", []() {
        SlotMap<Key<int, 16, 15, 1>> map;
        auto const count = *rc::gen::inRange<std::size_t>(10, 100);

        for (std::size_t i = 0; i < count; ++i) {
            (void)map.emplace(*rc::gen::arbitrary<int>());
        }

        auto const stop_after = *rc::gen::inRange<std::size_t>(1, count + 1);

        std::size_t visited_count = 0;
        auto result = map.for_each([&](int const &) {
            ++visited_count;
            return visited_count < stop_after;
        });

        RC_ASSERT(result.value == stop_after);
        RC_ASSERT(visited_count == stop_after);
    });
}

TEST_CASE("property-based for_each bool return equivalence with Options.stop")
{
    rc::check("bool return equivalent to Options.stop", []() {
        SlotMap<Key<int, 16, 15, 1>> map1;
        SlotMap<Key<int, 16, 15, 1>> map2;

        auto const count = *rc::gen::inRange<std::size_t>(10, 50);

        // Build identical maps
        for (std::size_t i = 0; i < count; ++i) {
            auto value = *rc::gen::arbitrary<int>();
            auto key1 = map1.emplace(value);
            auto key2 = map2.emplace(value);
            RC_ASSERT(key1 == key2);
        }

        auto const stop_after = *rc::gen::inRange<std::size_t>(1, count + 1);

        // Method 1: bool return
        std::size_t count1 = 0;
        std::vector<int> values1;
        auto result1 = map1.for_each([&](int const & v) {
            ++count1;
            values1.push_back(v);
            return count1 < stop_after;
        });

        // Method 2: Options.stop
        std::size_t count2 = 0;
        std::vector<int> values2;
        auto result2 = map2.for_each([&](int const & v, Options & opts) {
            ++count2;
            values2.push_back(v);
            if (count2 >= stop_after) {
                opts.stop = true;
            }
        });

        RC_ASSERT(result1.value == result2.value);
        RC_ASSERT(count1 == count2);
        RC_ASSERT(values1 == values2);
    });
}

TEST_CASE("property-based for_each bool return with erase")
{
    rc::check("bool return with erase removes correct elements", []() {
        SlotMap<Key<int, 16, 15, 1>> map;

        auto const count = *rc::gen::inRange<std::size_t>(10, 50);

        for (std::size_t i = 0; i < count; ++i) {
            (void)map.emplace(*rc::gen::arbitrary<int>());
        }

        auto const erase_count = *rc::gen::inRange<std::size_t>(1, count + 1);

        std::size_t visited = 0;
        auto result = map.for_each([&](int const &, Options & opts) {
            ++visited;
            opts.erase = true;
            return visited < erase_count;
        });

        RC_ASSERT(result.value == erase_count);
        RC_ASSERT(visited == erase_count);
        RC_ASSERT(map.size().value == count - erase_count);
    });
}

TEST_CASE("property-based for_each bool return true visits all")
{
    rc::check("returning true visits all elements", []() {
        SlotMap<Key<int, 16, 15, 1>> map;
        std::map<Key<int, 16, 15, 1>, int> reference;

        auto const count = *rc::gen::inRange<std::size_t>(0, 100);

        for (std::size_t i = 0; i < count; ++i) {
            auto value = *rc::gen::arbitrary<int>();
            auto key = map.emplace(value);
            reference[key] = value;
        }

        std::map<Key<int, 16, 15, 1>, int> visited;
        auto result = map.for_each([&](auto key, int const & v) {
            visited[key] = v;
            return true;
        });

        RC_ASSERT(result.value == count);
        RC_ASSERT(visited == reference);
    });
}

} // anonymous namespace
