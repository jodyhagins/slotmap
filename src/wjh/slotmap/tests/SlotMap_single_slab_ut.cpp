// ----------------------------------------------------------------------
// Copyright 2025 Jody Hagins
// Distributed under the MIT Software License
// See accompanying file LICENSE or copy at
// https://opensource.org/licenses/MIT
// ----------------------------------------------------------------------
//
// Single-slab storage tests: verifies behavior when SlotsPerSlab::All is used,
// ensuring no vector indirection overhead for small index spaces.
//

#include "wjh/slotmap/SlotMap.hpp"
#include "wjh/slotmap/tests/slotmap_test_utils.hpp"

#include <algorithm>
#include <cstdint>
#include <type_traits>
#include <vector>

#include "testing/doctest.hpp"
#include "testing/rapidcheck.hpp"

namespace {
using wjh::slotmap::Key;
using wjh::slotmap::SlotsPerSlab;
using wjh::slotmap::Traits;
using wjh::slotmap::UseAliveBitForLookup;
using wjh::slotmap::test::validate_statistics;
using namespace wjh::slotmap::literals;

// Single-slab SlotMap with 8-bit index (256 slots max) - 32 bits total
using SmallKey = Key<int, 8_ib, 8_vb>;
using SmallTraits =
    Traits<SmallKey, SlotsPerSlab::All, UseAliveBitForLookup::Yes>;
using SmallSlotMap = wjh::slotmap::SlotMap<SmallTraits>;

// Single-slab SlotMap with 16-bit index (64K slots max) - explicitly use All
using MediumKey = Key<int, 16_ib, 16_vb>;
using MediumTraits =
    Traits<MediumKey, SlotsPerSlab::All, UseAliveBitForLookup::Yes>;
using MediumSlotMap = wjh::slotmap::SlotMap<MediumTraits>;

// Multi-slab SlotMap (default) for comparison
using DefaultKey = Key<int, 16_ib, 16_vb>;
using DefaultSlotMap = wjh::slotmap::SlotMap<DefaultKey>;

// ============================================================================
// Static assertions for single-slab detection
// ============================================================================

static_assert(
    SmallTraits::is_single_slab,
    "8-bit index with SlotsPerSlab::All should use single-slab storage");
static_assert(
    MediumTraits::is_single_slab,
    "16-bit index with SlotsPerSlab::All should use single-slab storage");
static_assert(
    not wjh::slotmap::Traits<
        DefaultKey,
        SlotsPerSlab::Dynamic,
        UseAliveBitForLookup::Yes>::is_single_slab,
    "16-bit index with SlotsPerSlab::Dynamic should use multi-slab storage");

// ============================================================================
// Single-slab basic operations
// ============================================================================

TEST_CASE("Single-slab SlotMap: type traits")
{
    // Verify the storage policy is correctly selected
    static_assert(SmallSlotMap::traits_type::is_single_slab);
    static_assert(MediumSlotMap::traits_type::is_single_slab);
    static_assert(not DefaultSlotMap::traits_type::is_single_slab);

    // Verify types are correct
    static_assert(std::is_same_v<SmallSlotMap::key_type, SmallKey>);
    static_assert(std::is_same_v<SmallSlotMap::mapped_type, int>);
}

TEST_CASE("Single-slab SlotMap: basic emplace and use")
{
    SmallSlotMap map;

    CHECK(map.is_empty());
    CHECK(map.size().value == 0);

    auto key1 = map.emplace(42);
    CHECK(not map.is_empty());
    CHECK(map.size().value == 1);
    CHECK(key1 != SmallKey::null());

    bool found = false;
    (void)map.use(key1, [&](int value) {
        CHECK(value == 42);
        found = true;
    });
    CHECK(found);
}

TEST_CASE("Single-slab SlotMap: erase and slot reuse")
{
    SmallSlotMap map;

    auto key1 = map.emplace(1);
    auto key2 = map.emplace(2);
    auto key3 = map.emplace(3);

    CHECK(map.size().value == 3);

    // Erase middle element
    CHECK(map.erase(key2));
    CHECK(map.size().value == 2);
    CHECK(not map.contains(key2));

    // Emplace should reuse the slot
    auto key4 = map.emplace(4);
    CHECK(map.size().value == 3);
    CHECK(key4 != key2); // Different key (version incremented)

    // Original keys still valid
    CHECK(map.contains(key1));
    CHECK(map.contains(key3));
    CHECK(map.contains(key4));
}

TEST_CASE("Single-slab SlotMap: capacity exhaustion")
{
    // Use an 8-bit index space (256 slots max) with single-slab storage
    SmallSlotMap map;

    // Fill all 256 slots
    std::vector<SmallKey> keys;
    for (int i = 0; i < 256; ++i) {
        auto key = map.try_emplace(i);
        CHECK(key != SmallKey::null());
        keys.push_back(key);
    }

    CHECK(map.size().value == 256);

    // Next emplace should fail
    auto overflow_key = map.try_emplace(100);
    CHECK(overflow_key == SmallKey::null());

    // But after erase, we can emplace again
    CHECK(map.erase(keys[0]));
    auto new_key = map.try_emplace(999);
    CHECK(new_key != SmallKey::null());
}

TEST_CASE("Single-slab SlotMap: iteration with for_each")
{
    SmallSlotMap map;

    // Emplace several values
    std::vector<SmallKey> keys;
    for (int i = 0; i < 10; ++i) {
        keys.push_back(map.emplace(i * 10));
    }

    // Iterate and verify all values
    int count = 0;
    int sum = 0;
    map.for_each([&](SmallKey /*key*/, int value) {
        ++count;
        sum += value;
    });

    CHECK(count == 10);
    CHECK(sum == (0 + 10 + 20 + 30 + 40 + 50 + 60 + 70 + 80 + 90));

    // Erase some and iterate again
    map.erase(keys[2]);
    map.erase(keys[5]);
    map.erase(keys[8]);

    count = 0;
    map.for_each([&](int /*value*/) { ++count; });
    CHECK(count == 7);
}

TEST_CASE("Single-slab SlotMap: clear and reset")
{
    SmallSlotMap map;

    for (int i = 0; i < 20; ++i) {
        (void)map.emplace(i);
    }
    CHECK(map.size().value == 20);

    SUBCASE("clear preserves capacity") {
        map.clear();
        CHECK(map.is_empty());
        CHECK(map.size().value == 0);

        // Should be able to emplace again
        [[maybe_unused]] auto key = map.emplace(42);
        CHECK(key != SmallKey::null());
        CHECK(map.size().value == 1);
    }

    SUBCASE("reset deallocates") {
        map.reset();
        CHECK(map.is_empty());
        CHECK(map.size().value == 0);

        // Should be able to emplace again (will reallocate)
        [[maybe_unused]] auto key = map.emplace(42);
        CHECK(key != SmallKey::null());
        CHECK(map.size().value == 1);
    }
}

TEST_CASE("Single-slab SlotMap: copy operations")
{
    SmallSlotMap map1;

    std::vector<SmallKey> keys;
    for (int i = 0; i < 10; ++i) {
        keys.push_back(map1.emplace(i * 100));
    }

    SUBCASE("copy constructor") {
        SmallSlotMap map2(map1);

        CHECK(map2.size() == map1.size());

        // Both maps should have same values
        for (std::size_t i = 0; i < keys.size(); ++i) {
            int val1 = 0;
            int val2 = 0;
            (void)map1.use(keys[i], [&](int v) { val1 = v; });
            (void)map2.use(keys[i], [&](int v) { val2 = v; });
            CHECK(val1 == val2);
        }

        // Modifications to one don't affect the other
        map2.erase(keys[0]);
        CHECK(map1.contains(keys[0]));
        CHECK(not map2.contains(keys[0]));
    }

    SUBCASE("copy assignment") {
        SmallSlotMap map2;
        (void)map2.emplace(9999);

        map2 = map1;

        CHECK(map2.size() == map1.size());
        for (auto key : keys) {
            CHECK(map2.contains(key));
        }
    }
}

TEST_CASE("Single-slab SlotMap: move operations")
{
    SmallSlotMap map1;

    std::vector<SmallKey> keys;
    for (int i = 0; i < 10; ++i) {
        keys.push_back(map1.emplace(i * 100));
    }

    SUBCASE("move constructor") {
        SmallSlotMap map2(std::move(map1));

        CHECK(map2.size().value == 10);
        for (auto key : keys) {
            CHECK(map2.contains(key));
        }

        // Source should be empty after move
        CHECK(map1.is_empty()); // NOLINT(bugprone-use-after-move)
    }

    SUBCASE("move assignment") {
        SmallSlotMap map2;
        (void)map2.emplace(9999);

        map2 = std::move(map1);

        CHECK(map2.size().value == 10);
        for (auto key : keys) {
            CHECK(map2.contains(key));
        }

        CHECK(map1.is_empty()); // NOLINT(bugprone-use-after-move)
    }
}

TEST_CASE("Single-slab SlotMap: statistics")
{
    SmallSlotMap map;

    // Empty map
    auto stats = map.statistics();
    CHECK(stats.active_slots == 0);
    CHECK(stats.slab_count == 0);
    CHECK(stats.slab_vector_size == 0);
    CHECK(stats.vector_memory_bytes == 0); // No vector overhead!

    // After some emplaces
    for (int i = 0; i < 10; ++i) {
        (void)map.emplace(i);
    }

    stats = map.statistics();
    CHECK(stats.active_slots == 10);
    CHECK(stats.slab_count == 1);
    CHECK(stats.slab_vector_size == 1);
    CHECK(stats.vector_memory_bytes == 0); // Still no vector overhead

    validate_statistics(map);
}

TEST_CASE("Single-slab SlotMap: swap")
{
    SmallSlotMap map1;
    SmallSlotMap map2;

    auto k1 = map1.emplace(111);
    auto k2 = map1.emplace(222);

    auto k3 = map2.emplace(333);

    // Get actual values before swap
    int val1_before = 0;
    int val2_before = 0;
    int val3_before = 0;
    (void)map1.use(k1, [&](int v) { val1_before = v; });
    (void)map1.use(k2, [&](int v) { val2_before = v; });
    (void)map2.use(k3, [&](int v) { val3_before = v; });

    map1.swap(map2);

    // After swap, sizes should be exchanged
    CHECK(map1.size().value == 1);
    CHECK(map2.size().value == 2);

    // map1 now has map2's original content (value 333)
    // map2 now has map1's original content (values 111, 222)
    int found_val = 0;
    int count = 0;
    map1.for_each([&](int v) {
        found_val = v;
        ++count;
    });
    CHECK(count == 1);
    CHECK(found_val == 333);

    // Check map2 has the original map1 values
    std::vector<int> map2_vals;
    map2.for_each([&](int v) { map2_vals.push_back(v); });
    CHECK(map2_vals.size() == 2);
    std::sort(map2_vals.begin(), map2_vals.end());
    CHECK(map2_vals[0] == 111);
    CHECK(map2_vals[1] == 222);
}

// ============================================================================
// Property-based tests
// ============================================================================

TEST_CASE("Single-slab SlotMap: property-based emplace/use roundtrip")
{
    rc::check("emplace followed by use returns same value", [](int value) {
        SmallSlotMap map;
        auto key = map.emplace(value);
        int retrieved = 0;
        bool found = map.use(key, [&](int v) { retrieved = v; });
        RC_ASSERT(found);
        RC_ASSERT(retrieved == value);
    });
}

TEST_CASE("Single-slab SlotMap: property-based erase invalidates key")
{
    rc::check("erase followed by use returns false", [](int value) {
        SmallSlotMap map;
        auto key = map.emplace(value);
        RC_ASSERT(map.contains(key));
        RC_ASSERT(map.erase(key));
        RC_ASSERT(not map.contains(key));
        RC_ASSERT(not map.use(key, [](int) {}));
    });
}

TEST_CASE("Single-slab SlotMap: property-based multiple values")
{
    rc::check(
        "multiple emplaces create distinct keys with correct values",
        [](std::vector<int> const & values) {
            RC_PRE(values.size() <= 200); // Keep within 8-bit index space

            SmallSlotMap map;
            std::vector<SmallKey> keys;

            for (int v : values) {
                keys.push_back(map.emplace(v));
            }

            RC_ASSERT(map.size().value == values.size());

            for (std::size_t i = 0; i < values.size(); ++i) {
                int retrieved = 0;
                bool found = map.use(keys[i], [&](int v) { retrieved = v; });
                RC_ASSERT(found);
                RC_ASSERT(retrieved == values[i]);
            }
        });
}

TEST_CASE("Single-slab SlotMap: property-based interleaved operations")
{
    rc::check("interleaved emplace and erase maintain consistency", []() {
        SmallSlotMap map;
        std::map<SmallKey, int> reference;

        for (int i = 0; i < 100; ++i) {
            if (*rc::gen::arbitrary<bool>()) {
                // Emplace
                int value = *rc::gen::arbitrary<int>();
                auto key = map.emplace(value);
                reference[key] = value;
            } else if (not reference.empty()) {
                // Erase random key
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
            int actual = 0;
            RC_ASSERT(map.use(key, [&](int v) { actual = v; }));
            RC_ASSERT(actual == expected);
        }
    });
}

// ============================================================================
// Regression test: SlotsPerSlab::All with large index space (> 2MB slab)
//
// This tests a bug where SlotsPerSlab::All combined with an index space that
// would create a slab larger than 2MB would fail. The bug was that the
// single-slab constructor used compute_default_slab_size() which has a 2MB
// limit and falls back to 4096 slots. But single_slab_storage_policy only
// supports one slab, so capacity was exhausted after 4096 elements.
//
// The fix ensures single-slab storage always uses end_of_free_list (max slots)
// for slots_per_slab_, regardless of the resulting slab size.
// ============================================================================

// Large index space key: 18 bits = 262,144 max slots
// With a 16-byte value type, this creates a slab well over 2MB
struct LargeValue
{
    std::uint64_t id;
    std::uint64_t data;
};

using LargeIndexKey = Key<LargeValue, 18_ib, 14_vb>;
using LargeIndexTraits =
    Traits<LargeIndexKey, SlotsPerSlab::All, UseAliveBitForLookup::Yes>;
using LargeIndexSlotMap = wjh::slotmap::SlotMap<LargeIndexTraits>;

static_assert(
    LargeIndexTraits::is_single_slab,
    "18-bit index with SlotsPerSlab::All should use single-slab storage");

TEST_CASE("Single-slab SlotMap: large index space (> 2MB slab) regression test")
{
    // This test verifies the fix for a bug where SlotsPerSlab::All with a
    // large index space (where the slab exceeds 2MB) would fail after only
    // 4096 insertions because compute_default_slab_size() fell back to 4096.

    LargeIndexSlotMap map;

    // Verify statistics show correct slots_per_slab
    auto stats = map.statistics();
    CHECK(stats.slots_per_slab == 262144); // 2^18, NOT 4096
    CHECK(stats.max_slots == 262144);

    // Insert more than 4096 elements - this would fail before the fix
    constexpr std::size_t test_count = 5000; // More than 4096 to catch the bug
    std::vector<LargeIndexKey> keys;
    keys.reserve(test_count);

    for (std::size_t i = 0; i < test_count; ++i) {
        LargeValue v{i, i * 2};
        auto key = map.try_emplace(std::move(v));
        REQUIRE(key != LargeIndexKey::null());
        keys.push_back(key);
    }

    CHECK(map.size().value == test_count);

    // Verify all values are accessible
    for (std::size_t i = 0; i < test_count; ++i) {
        bool found = map.use(keys[i], [i](LargeValue const & v) {
            CHECK(v.id == i);
            CHECK(v.data == i * 2);
        });
        CHECK(found);
    }

    // Verify we still have single-slab storage
    stats = map.statistics();
    CHECK(stats.slab_count == 1);
    CHECK(stats.vector_memory_bytes == 0); // No vector overhead
}

TEST_CASE("Single-slab SlotMap: can fill entire large index space")
{
    // Use a smaller but still > 2MB case for practical testing
    // 16-bit index = 65536 slots, which with 16-byte values is ~1MB
    // but we want to verify the full capacity works

    using MediumLargeKey = Key<LargeValue, 16_ib, 16_vb>;
    using MediumLargeTraits =
        Traits<MediumLargeKey, SlotsPerSlab::All, UseAliveBitForLookup::Yes>;
    using MediumLargeSlotMap = wjh::slotmap::SlotMap<MediumLargeTraits>;

    static_assert(MediumLargeTraits::is_single_slab);

    MediumLargeSlotMap map;

    auto stats = map.statistics();
    CHECK(stats.slots_per_slab == 65536); // 2^16
    CHECK(stats.max_slots == 65536);

    // Fill completely
    for (std::size_t i = 0; i < 65536; ++i) {
        LargeValue v{i, i};
        auto key = map.try_emplace(std::move(v));
        REQUIRE(key != MediumLargeKey::null());
    }

    CHECK(map.size().value == 65536);

    // Next insertion should fail
    auto overflow = map.try_emplace(LargeValue{0, 0});
    CHECK(overflow == MediumLargeKey::null());

    // Verify single slab
    stats = map.statistics();
    CHECK(stats.slab_count == 1);
}

} // anonymous namespace
