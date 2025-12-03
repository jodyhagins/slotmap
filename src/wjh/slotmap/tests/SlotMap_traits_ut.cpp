// ----------------------------------------------------------------------
// Copyright 2025 Jody Hagins
// Distributed under the MIT Software License
// See accompanying file LICENSE or copy at
// https://opensource.org/licenses/MIT
// ----------------------------------------------------------------------

#include "../SlotMap.hpp"

#include <string>
#include <vector>

#include "testing/doctest.hpp"
#include "testing/rapidcheck.hpp"

namespace {
using namespace wjh::slotmap::literals;

// Test configurations with embedded alive bit (20 version bits in uint16_t)
using EmbeddedKey = wjh::slotmap::Key<int, 12_ib, 20_vb>;
using EmbeddedTraits = wjh::slotmap::Traits<
    EmbeddedKey,
    wjh::slotmap::SlotsPerSlab::All,
    wjh::slotmap::UseAliveBitForLookup::Yes>;
using EmbeddedMap = wjh::SlotMap<EmbeddedTraits>;

// Custom traits that disable alive-bit optimization
template <wjh::slotmap::KeyC KeyT>
struct DisabledAliveBitTraits
: wjh::slotmap::Traits<
      KeyT,
      wjh::slotmap::SlotsPerSlab::All,
      wjh::slotmap::UseAliveBitForLookup::Yes>
{
    using base = wjh::slotmap::Traits<
        KeyT,
        wjh::slotmap::SlotsPerSlab::All,
        wjh::slotmap::UseAliveBitForLookup::Yes>;
    using base::base;

    // Override to disable the alive-bit optimization
    static constexpr bool use_alive_bit_for_lookup = false;
};

using DisabledMap = wjh::SlotMap<DisabledAliveBitTraits<EmbeddedKey>>;

} // anonymous namespace

TEST_SUITE("alive_bit_trait - Trait Configuration")
{
    TEST_CASE("Default traits enable alive-bit for embedded configurations")
    {
        // When slot has embedded alive bit, trait should default to true
        static_assert(EmbeddedTraits::use_alive_bit_for_lookup);
    }

    TEST_CASE("Custom traits can disable alive-bit optimization")
    {
        // User can override trait to disable optimization
        static_assert(
            not DisabledAliveBitTraits<EmbeddedKey>::use_alive_bit_for_lookup);
    }
}

TEST_SUITE("alive_bit_trait - Basic Functionality")
{
    TEST_CASE("use() works with alive-bit optimization enabled (default)")
    {
        EmbeddedMap map;
        auto k1 = map.emplace(42);
        auto k2 = map.emplace(99);

        SUBCASE("lookup finds inserted elements") {
            bool found = false;
            map.use(k1, [&](int val) {
                found = true;
                CHECK(val == 42);
            });
            CHECK(found);

            found = false;
            map.use(k2, [&](int val) {
                found = true;
                CHECK(val == 99);
            });
            CHECK(found);
        }

        SUBCASE("erased elements not found") {
            CHECK(map.erase(k1));

            bool found = false;
            map.use(k1, [&](int) { found = true; });
            CHECK_FALSE(found);

            // k2 still accessible
            found = false;
            map.use(k2, [&](int val) {
                found = true;
                CHECK(val == 99);
            });
            CHECK(found);
        }

        SUBCASE("version mismatch prevents access") {
            // Erase and re-insert to increment version
            map.erase(k1);
            auto k1_new = map.emplace(100);

            // Old key should not work (version mismatch)
            bool found = false;
            map.use(k1, [&](int) { found = true; });
            CHECK_FALSE(found);

            // New key should work
            found = false;
            map.use(k1_new, [&](int val) {
                found = true;
                CHECK(val == 100);
            });
            CHECK(found);
        }
    }

    TEST_CASE("use() works with alive-bit optimization disabled")
    {
        DisabledMap map;
        auto k1 = map.emplace(42);
        auto k2 = map.emplace(99);

        SUBCASE("lookup finds inserted elements") {
            bool found = false;
            map.use(k1, [&](int val) {
                found = true;
                CHECK(val == 42);
            });
            CHECK(found);
        }

        SUBCASE("erased elements not found") {
            CHECK(map.erase(k1));

            bool found = false;
            map.use(k1, [&](int) { found = true; });
            CHECK_FALSE(found);

            // k2 still accessible
            found = false;
            map.use(k2, [&](int val) {
                found = true;
                CHECK(val == 99);
            });
            CHECK(found);
        }
    }
}

TEST_SUITE("alive_bit_trait - Combined Version+Alive Check")
{
    TEST_CASE("Combined check correctly validates alive+version match")
    {
        EmbeddedMap map;
        auto k = map.emplace(42);

        SUBCASE("alive slot with matching version") {
            bool found = false;
            map.use(k, [&](int val) {
                found = true;
                CHECK(val == 42);
            });
            CHECK(found);
        }

        SUBCASE("dead slot with matching version fails") {
            map.erase(k);

            bool found = false;
            map.use(k, [&](int) { found = true; });
            CHECK_FALSE(found);
        }

        SUBCASE("alive slot with wrong version fails") {
            // Create another key for different slot
            auto k2 = map.emplace(100);

            // Manually construct key with wrong version for k2's slot
            auto wrong_key = EmbeddedKey(
                k2.index(),
                EmbeddedKey::version_type{
                    static_cast<typename EmbeddedKey::version_type::value_type>(
                        k2.version().value + 1)},
                k2.user());

            bool found = false;
            map.use(wrong_key, [&](int) { found = true; });
            CHECK_FALSE(found);
        }
    }

    TEST_CASE("Combined check handles version wrapping")
    {
        // Use small version bits to force wrapping
        using SmallVersionKey = wjh::slotmap::Key<
            int,
            12_ib,
            4_vb>; // Only 4 version bits
        using SmallTraits = wjh::slotmap::Traits<
            SmallVersionKey,
            wjh::slotmap::SlotsPerSlab::All,
            wjh::slotmap::UseAliveBitForLookup::Yes>;
        using SmallMap = wjh::SlotMap<SmallTraits>;

        SmallMap map;

        // Insert and erase multiple times to cycle versions
        std::vector<SmallVersionKey> old_keys;
        for (int i = 0; i < 20; ++i) { // More than 2^4 = 16
            auto k = map.emplace(i);
            old_keys.push_back(k);
            map.erase(k);
        }

        // Insert new value - should reuse slot with wrapped version
        auto k_new = map.emplace(999);

        SUBCASE("old keys don't match (version mismatch)") {
            for (auto const & old_key : old_keys) {
                bool found = false;
                map.use(old_key, [&](int) { found = true; });
                CHECK_FALSE(found);
            }
        }

        SUBCASE("new key works correctly") {
            bool found = false;
            map.use(k_new, [&](int val) {
                found = true;
                CHECK(val == 999);
            });
            CHECK(found);
        }
    }
}

TEST_SUITE("alive_bit_trait - Edge Cases")
{
    TEST_CASE("Null key never matches")
    {
        EmbeddedMap map;
        (void)map.emplace(42);

        auto null_key = EmbeddedKey::null();
        bool found = false;
        map.use(null_key, [&](int) { found = true; });
        CHECK_FALSE(found);
    }

    TEST_CASE("Invalid index never matches")
    {
        EmbeddedMap map;
        auto k = map.emplace(42);

        // Create key with invalid index
        auto invalid_key = EmbeddedKey(
            EmbeddedKey::index_type{
                static_cast<typename EmbeddedKey::index_type::value_type>(
                    k.index().value + 100)},
            k.version(),
            k.user());

        bool found = false;
        map.use(invalid_key, [&](int) { found = true; });
        CHECK_FALSE(found);
    }

    TEST_CASE("Multiple erases and reinserts work correctly")
    {
        EmbeddedMap map;

        // First generation
        auto k1 = map.emplace(1);
        CHECK(map.contains(k1));
        map.erase(k1);
        CHECK_FALSE(map.contains(k1));

        // Second generation (same slot, different version)
        auto k2 = map.emplace(2);
        CHECK(map.contains(k2));
        CHECK_FALSE(map.contains(k1)); // Old key still invalid

        // Third generation
        map.erase(k2);
        auto k3 = map.emplace(3);
        CHECK(map.contains(k3));
        CHECK_FALSE(map.contains(k1));
        CHECK_FALSE(map.contains(k2));
    }
}

TEST_SUITE("alive_bit_trait - Property-Based Tests")
{
    TEST_CASE("Enabled trait maintains correctness across operations")
    {
        rc::check(
            "all operations work correctly with alive-bit optimization",
            []() {
                EmbeddedMap map;
                std::vector<EmbeddedKey> keys;
                std::vector<int> values;

                // Generate random operations
                auto num_ops = *rc::gen::inRange(10, 50);

                for (int i = 0; i < num_ops; ++i) {
                    auto value = *rc::gen::inRange(0, 1000);
                    auto k = map.emplace(value);
                    keys.push_back(k);
                    values.push_back(value);
                }

                // Verify all keys are valid
                for (std::size_t i = 0; i < keys.size(); ++i) {
                    bool found = false;
                    int seen_value = -1;
                    map.use(keys[i], [&](int val) {
                        found = true;
                        seen_value = val;
                    });
                    RC_ASSERT(found);
                    RC_ASSERT(seen_value == values[i]);
                }

                // Erase some randomly
                auto num_to_erase = *rc::gen::inRange(
                    0,
                    static_cast<int>(keys.size()));
                std::vector<bool> erased(keys.size(), false);
                for (int i = 0; i < num_to_erase; ++i) {
                    auto idx = *rc::gen::inRange(
                        0,
                        static_cast<int>(keys.size()));
                    if (not erased[static_cast<std::size_t>(idx)]) {
                        RC_ASSERT(
                            map.erase(keys[static_cast<std::size_t>(idx)]));
                        erased[static_cast<std::size_t>(idx)] = true;
                    }
                }

                // Verify correct keys remain
                for (std::size_t i = 0; i < keys.size(); ++i) {
                    bool found = false;
                    map.use(keys[i], [&](int) { found = true; });
                    RC_ASSERT(found == not erased[i]);
                }
            });
    }

    TEST_CASE("Disabled trait maintains correctness across operations")
    {
        rc::check("all operations work correctly with bitmap-only check", []() {
            DisabledMap map;
            std::vector<EmbeddedKey> keys;
            std::vector<int> values;

            auto num_ops = *rc::gen::inRange(10, 50);

            for (int i = 0; i < num_ops; ++i) {
                auto value = *rc::gen::inRange(0, 1000);
                auto k = map.emplace(value);
                keys.push_back(k);
                values.push_back(value);
            }

            // Verify all keys are valid
            for (std::size_t i = 0; i < keys.size(); ++i) {
                bool found = false;
                int seen_value = -1;
                map.use(keys[i], [&](int val) {
                    found = true;
                    seen_value = val;
                });
                RC_ASSERT(found);
                RC_ASSERT(seen_value == values[i]);
            }
        });
    }

    TEST_CASE("Both implementations produce same results")
    {
        rc::check("enabled and disabled traits behave identically", []() {
            EmbeddedMap enabled_map;
            DisabledMap disabled_map;

            // Same sequence of operations on both maps
            std::vector<EmbeddedKey> keys;
            auto num_ops = *rc::gen::inRange(10, 30);

            for (int i = 0; i < num_ops; ++i) {
                auto value = *rc::gen::inRange(0, 1000);
                auto k1 = enabled_map.emplace(value);
                auto k2 = disabled_map.emplace(value);
                keys.push_back(k1);

                // Keys should have same index and version
                RC_ASSERT(k1.index() == k2.index());
                RC_ASSERT(k1.version() == k2.version());
            }

            // Both maps should find same keys
            for (auto const & key : keys) {
                bool found1 = false, found2 = false;
                int val1 = -1, val2 = -1;

                enabled_map.use(key, [&](int v) {
                    found1 = true;
                    val1 = v;
                });
                disabled_map.use(key, [&](int v) {
                    found2 = true;
                    val2 = v;
                });

                RC_ASSERT(found1 == found2);
                RC_ASSERT(val1 == val2);
            }

            // Erase from both
            auto num_to_erase = *rc::gen::inRange(
                0,
                static_cast<int>(keys.size()));
            for (int i = 0; i < num_to_erase; ++i) {
                auto idx = *rc::gen::inRange(0, static_cast<int>(keys.size()));
                auto result1 = enabled_map.erase(
                    keys[static_cast<std::size_t>(idx)]);
                auto result2 = disabled_map.erase(
                    keys[static_cast<std::size_t>(idx)]);
                RC_ASSERT(result1 == result2);
            }

            // Both should have same results for all keys
            for (auto const & key : keys) {
                bool found1 = enabled_map.contains(key);
                bool found2 = disabled_map.contains(key);
                RC_ASSERT(found1 == found2);
            }
        });
    }
}

TEST_SUITE("alive_bit_trait - Integration with Other Operations")
{
    TEST_CASE("contains() works with alive-bit optimization")
    {
        EmbeddedMap map;
        auto k = map.emplace(42);

        CHECK(map.contains(k));
        map.erase(k);
        CHECK_FALSE(map.contains(k));
    }

    TEST_CASE("pop() works with alive-bit optimization")
    {
        EmbeddedMap map;
        auto k = map.emplace(42);

        auto result = map.pop(k);
        REQUIRE(result.has_value());
        CHECK(*result == 42);
        CHECK_FALSE(map.contains(k));

        // Second pop should return nullopt
        auto result2 = map.pop(k);
        CHECK_FALSE(result2.has_value());
    }

    TEST_CASE("for_each() iterates correctly with alive-bit optimization")
    {
        EmbeddedMap map;
        std::vector<int> values = {10, 20, 30, 40, 50};
        std::vector<EmbeddedKey> keys;

        for (auto v : values) {
            keys.push_back(map.emplace(v));
        }

        SUBCASE("iterate all elements") {
            std::vector<int> seen;
            map.for_each([&](int val) { seen.push_back(val); });

            std::sort(seen.begin(), seen.end());
            CHECK(seen == values);
        }

        SUBCASE("iterate after erasing some") {
            map.erase(keys[1]); // Erase 20
            map.erase(keys[3]); // Erase 40

            std::vector<int> seen;
            map.for_each([&](int val) { seen.push_back(val); });

            std::sort(seen.begin(), seen.end());
            std::vector<int> expected = {10, 30, 50};
            CHECK(seen == expected);
        }
    }

    TEST_CASE("const use() works with alive-bit optimization")
    {
        EmbeddedMap map;
        auto k = map.emplace(42);

        // Access through const reference
        EmbeddedMap const & const_map = map;

        bool found = false;
        const_map.use(k, [&](int const & val) {
            found = true;
            CHECK(val == 42);
        });
        CHECK(found);

        // Erased key should not be found via const access
        map.erase(k);
        found = false;
        const_map.use(k, [&](int const &) { found = true; });
        CHECK_FALSE(found);
    }
}
