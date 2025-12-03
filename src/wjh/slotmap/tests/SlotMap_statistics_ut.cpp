// ----------------------------------------------------------------------
// Copyright 2025 Jody Hagins
// Distributed under the MIT Software License
// See accompanying file LICENSE or copy at
// https://opensource.org/licenses/MIT
// ----------------------------------------------------------------------
//
// SlotMap statistics tests: basic functionality and invariant validation
//

#include "wjh/slotmap/SlotMap.hpp"
#include "wjh/slotmap/tests/slotmap_test_utils.hpp"

#include <cstdint>
#include <type_traits>
#include <vector>

#include "testing/doctest.hpp"
#include "testing/rapidcheck.hpp"

namespace {
using wjh::slotmap::Key;
using wjh::slotmap::test::validate_statistics;
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

// Doctest-compatible version for non-rapidcheck tests
template <typename MapT>
void
check_statistics_invariants(MapT const & map)
{
    auto const stats = map.statistics();

    // Basic slot accounting: active + free + dead = allocated
    auto const computed_allocated = stats.active_slots + stats.free_slots +
        stats.dead_slots;
    CHECK(computed_allocated == stats.allocated_slots);

    // Index space accounting: allocated + unallocated = max_slots
    auto const computed_max = stats.allocated_slots + stats.unallocated_slots;
    CHECK(computed_max == stats.max_slots);

    // Capacity metrics
    CHECK(stats.available_slots == stats.free_slots);

    auto const computed_remaining = stats.max_slots - stats.dead_slots;
    CHECK(stats.remaining_slots == computed_remaining);

    // Object lifetime accounting
    auto const computed_total_objects = stats.objects_created +
        stats.objects_remaining;
    CHECK(computed_total_objects == stats.max_objects);

    // Cross-check with public API
    CHECK(stats.active_slots == map.size().value);
}

// ============================================================================
// Basic Statistics Tests
// ============================================================================

TEST_CASE("SlotMap statistics: empty map")
{
    using K = Key<int, 16_ib, 16_vb>;
    SlotMap<K> map(64u);

    auto const stats = map.statistics();

    SUBCASE("configuration values") {
        CHECK(stats.slots_per_slab == 64);
        CHECK(stats.max_slots == 65536); // 2^16
        // max_objects = 2^16 * 2^16 - 1 = 4294967295
        CHECK(stats.max_objects == 4294967295ULL);
    }

    SUBCASE("slot accounting") {
        CHECK(stats.active_slots == 0);
        CHECK(stats.dead_slots == 0);
        // No slabs allocated yet for empty map
        CHECK(stats.allocated_slots == 0);
        CHECK(stats.free_slots == 0);
        CHECK(stats.unallocated_slots == 65536);
    }

    SUBCASE("capacity metrics") {
        CHECK(stats.available_slots == 0);
        CHECK(stats.remaining_slots == 65536);
    }

    SUBCASE("object lifetime") {
        CHECK(stats.objects_created == 0);
        CHECK(stats.objects_remaining == stats.max_objects);
    }

    SUBCASE("slab metrics") {
        CHECK(stats.slab_count == 0);
    }

    SUBCASE("derived metrics for empty") {
        CHECK(stats.slot_utilization == 0.0);
        CHECK(stats.dead_slot_ratio == 0.0);
        CHECK(stats.lifetime_exhaustion == 0.0);
        CHECK(stats.bytes_per_object == 0.0);
    }

    check_statistics_invariants(map);
}

TEST_CASE("SlotMap statistics: after single emplace")
{
    using K = Key<int, 16_ib, 16_vb>;
    SlotMap<K> map(64u);

    (void)map.emplace(42);
    auto const stats = map.statistics();

    CHECK(stats.active_slots == 1);
    CHECK(stats.objects_created == 1);
    CHECK(stats.allocated_slots == 64); // One slab allocated
    CHECK(stats.free_slots == 63);
    CHECK(stats.dead_slots == 0);
    CHECK(stats.slab_count == 1);

    check_statistics_invariants(map);
}

TEST_CASE("SlotMap statistics: after emplace and erase")
{
    using K = Key<int, 16_ib, 16_vb>;
    SlotMap<K> map(64u);

    auto key = map.emplace(42);
    map.erase(key);
    auto const stats = map.statistics();

    CHECK(stats.active_slots == 0);
    CHECK(stats.objects_created == 1); // Still 1 - we created one
    CHECK(stats.free_slots == 64);
    CHECK(stats.dead_slots == 0); // Not dead, just erased

    check_statistics_invariants(map);
}

TEST_CASE("SlotMap statistics: slot reuse tracking")
{
    using K = Key<int, 16_ib, 16_vb>;
    SlotMap<K> map(4u);

    // Create and destroy same slot multiple times
    for (int i = 0; i < 5; ++i) {
        auto key = map.emplace(i);
        map.erase(key);
    }

    auto const stats = map.statistics();
    CHECK(stats.active_slots == 0);
    CHECK(stats.objects_created == 5); // 5 objects created over time
    CHECK(stats.dead_slots == 0); // Not exhausted yet

    check_statistics_invariants(map);
}

TEST_CASE("SlotMap statistics: version exhaustion creates dead slots")
{
    // 2-bit version means each slot can hold 4 objects before dying
    // (versions 0, 1, 2, 3 -> exhausted at version 4)
    // But slot 0 starts at version 1, so it only gets 3 objects
    using K = Key<int, 8_ib, 2_vb,
                  22_ub>; // 8 index, 2 version
    SlotMap<K> map(4u);

    // Exhaust slot 0 (starts at version 1, so 3 cycles)
    for (int cycle = 0; cycle < 3; ++cycle) {
        auto key = map.emplace(cycle);
        CHECK(key.index().value == 0);
        map.erase(key);
    }

    auto const stats = map.statistics();
    CHECK(stats.dead_slots == 1); // Slot 0 is dead
    CHECK(stats.objects_created == 3);

    check_statistics_invariants(map);
}

TEST_CASE("SlotMap statistics: max_objects calculation")
{
    SUBCASE("32-bit key: 16 index, 16 version") {
        using K = Key<int, 16_ib, 16_vb>;
        SlotMap<K> map;
        auto const stats = map.statistics();

        // max = 2^16 * 2^16 - 1 = 4294967295
        CHECK(stats.max_objects == 4294967295ULL);
    }

    SUBCASE("16-bit key: 8 index, 8 version") {
        using K = Key<int, 8_ib, 8_vb>;
        SlotMap<K> map;
        auto const stats = map.statistics();

        // max = 2^8 * 2^8 - 1 = 65535
        CHECK(stats.max_objects == 65535ULL);
    }

    SUBCASE("small key: 4 index, 4 version") {
        using K = Key<int, 4_ib, 4_vb, 24_ub>;
        SlotMap<K> map;
        auto const stats = map.statistics();

        // max = 2^4 * 2^4 - 1 = 255
        CHECK(stats.max_objects == 255ULL);
    }
}

TEST_CASE("SlotMap statistics: memory tracking")
{
    using K = Key<int, 16_ib, 16_vb>;
    SlotMap<K> map(64u);

    auto const empty_stats = map.statistics();
    CHECK(empty_stats.slab_memory_bytes == 0);

    // After emplace, should have slab memory
    (void)map.emplace(42);
    auto const with_data = map.statistics();
    CHECK(with_data.slab_memory_bytes > 0);
    CHECK(with_data.slab_count == 1);

    check_statistics_invariants(map);
}

TEST_CASE("SlotMap statistics: copy preserves stats")
{
    using K = Key<int, 16_ib, 16_vb>;
    SlotMap<K> original(64u);

    for (int i = 0; i < 10; ++i) {
        (void)original.emplace(i);
    }

    SlotMap<K> copy(original);
    auto const orig_stats = original.statistics();
    auto const copy_stats = copy.statistics();

    CHECK(orig_stats.active_slots == copy_stats.active_slots);
    CHECK(orig_stats.objects_created == copy_stats.objects_created);
    CHECK(orig_stats.dead_slots == copy_stats.dead_slots);

    check_statistics_invariants(copy);
}

TEST_CASE("SlotMap statistics: move transfers stats")
{
    using K = Key<int, 16_ib, 16_vb>;
    SlotMap<K> original(64u);

    for (int i = 0; i < 10; ++i) {
        (void)original.emplace(i);
    }

    auto const before_move = original.statistics();
    SlotMap<K> moved(std::move(original));
    auto const after_move = moved.statistics();

    CHECK(after_move.active_slots == before_move.active_slots);
    CHECK(after_move.objects_created == before_move.objects_created);

    // Original should be empty
    auto const orig_after = original.statistics();
    CHECK(orig_after.active_slots == 0);
    CHECK(orig_after.objects_created == 0);

    check_statistics_invariants(moved);
}

TEST_CASE("SlotMap statistics: clear preserves objects_created")
{
    using K = Key<int, 16_ib, 16_vb>;
    SlotMap<K> map(64u);

    for (int i = 0; i < 10; ++i) {
        (void)map.emplace(i);
    }

    auto const before_clear = map.statistics();
    CHECK(before_clear.objects_created == 10);

    map.clear();

    auto const after_clear = map.statistics();
    CHECK(after_clear.active_slots == 0);
    // objects_created is preserved through clear
    CHECK(after_clear.objects_created == 10);

    check_statistics_invariants(map);
}

TEST_CASE("SlotMap statistics: reset clears everything")
{
    using K = Key<int, 16_ib, 16_vb>;
    SlotMap<K> map(64u);

    for (int i = 0; i < 10; ++i) {
        (void)map.emplace(i);
    }

    map.reset();

    auto const stats = map.statistics();
    CHECK(stats.active_slots == 0);
    CHECK(stats.objects_created == 0);
    CHECK(stats.dead_slots == 0);
    CHECK(stats.allocated_slots == 0);
    CHECK(stats.slab_count == 0);

    check_statistics_invariants(map);
}

// ============================================================================
// Property-Based Statistics Tests
// ============================================================================

TEST_CASE(
    "SlotMap statistics: property-based invariants after random operations")
{
    rc::check("invariants hold after random emplace/erase", []() {
        using K = Key<int, 10_ib, 6_vb>;
        SlotMap<K> map(16u);

        auto const ops = *rc::gen::inRange<std::size_t>(0, 100);

        std::vector<K> keys;
        for (std::size_t i = 0; i < ops; ++i) {
            bool do_insert = keys.empty() || *rc::gen::arbitrary<bool>();

            if (do_insert) {
                auto key = map.try_emplace(*rc::gen::arbitrary<int>());
                if (not key.is_null()) {
                    keys.push_back(key);
                }
            } else {
                auto idx = *rc::gen::inRange<std::size_t>(0, keys.size());
                map.erase(keys[idx]);
                keys.erase(keys.begin() + static_cast<std::ptrdiff_t>(idx));
            }

            validate_statistics(map);
        }

        // Final check
        auto const stats = map.statistics();
        RC_ASSERT(stats.active_slots == keys.size());
    });
}

TEST_CASE("SlotMap statistics: property-based objects_created tracking")
{
    rc::check("objects_created equals total emplaces", []() {
        using K = Key<int, 10_ib, 6_vb>;
        SlotMap<K> map(16u);

        auto const emplace_count = *rc::gen::inRange<std::size_t>(0, 200);
        std::size_t successful_emplaces = 0;

        std::vector<K> keys;
        for (std::size_t i = 0; i < emplace_count; ++i) {
            auto key = map.try_emplace(static_cast<int>(i));
            if (not key.is_null()) {
                ++successful_emplaces;
                keys.push_back(key);
            }

            // Randomly erase some
            if (not keys.empty() && *rc::gen::inRange<int>(0, 3) == 0) {
                auto idx = *rc::gen::inRange<std::size_t>(0, keys.size());
                map.erase(keys[idx]);
                keys.erase(keys.begin() + static_cast<std::ptrdiff_t>(idx));
            }
        }

        auto const stats = map.statistics();
        RC_ASSERT(stats.objects_created == successful_emplaces);
        validate_statistics(map);
    });
}

TEST_CASE("SlotMap statistics: property-based version exhaustion")
{
    rc::check("dead slots tracked correctly during exhaustion", []() {
        // Small version bits to force exhaustion
        using K = Key<int, 8_ib, 2_vb, 22_ub>;
        SlotMap<K> map(4u);

        auto const cycles = *rc::gen::inRange<std::size_t>(1, 20);

        for (std::size_t i = 0; i < cycles; ++i) {
            auto key = map.try_emplace(static_cast<int>(i));
            if (not key.is_null()) {
                map.erase(key);
            }
            validate_statistics(map);
        }

        auto const stats = map.statistics();
        // dead_slots + free_slots + active_slots = allocated_slots
        auto const sum = stats.dead_slots + stats.free_slots +
            stats.active_slots;
        RC_ASSERT(sum == stats.allocated_slots);
    });
}

TEST_CASE("SlotMap statistics: property-based 16-bit key")
{
    rc::check("16-bit key statistics invariants", []() {
        using K = Key<int, 8_ib, 6_vb, 2_ub>;
        SlotMap<K> map(8u);

        auto const ops = *rc::gen::inRange<std::size_t>(0, 50);

        std::vector<K> keys;
        for (std::size_t i = 0; i < ops; ++i) {
            bool do_insert = keys.empty() || *rc::gen::arbitrary<bool>();

            if (do_insert) {
                auto key = map.try_emplace(static_cast<int>(i));
                if (not key.is_null()) {
                    keys.push_back(key);
                }
            } else if (not keys.empty()) {
                auto idx = *rc::gen::inRange<std::size_t>(0, keys.size());
                map.erase(keys[idx]);
                keys.erase(keys.begin() + static_cast<std::ptrdiff_t>(idx));
            }
        }

        validate_statistics(map);
    });
}

} // anonymous namespace
