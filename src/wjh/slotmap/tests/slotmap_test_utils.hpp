// ----------------------------------------------------------------------
// Copyright 2025 Jody Hagins
// Distributed under the MIT Software License
// See accompanying file LICENSE or copy at
// https://opensource.org/licenses/MIT
// ----------------------------------------------------------------------
#ifndef WJH_SLOTMAP_B999CB0478DB4B9D946C2B87F06FC062
#define WJH_SLOTMAP_B999CB0478DB4B9D946C2B87F06FC062

#include "testing/rapidcheck.hpp"

namespace wjh::slotmap::test {

/**
 * Validates that all statistics invariants hold.
 *
 * This function checks mathematical relationships that must always be true:
 * 1. active + free + dead = allocated
 * 2. allocated + unallocated = max_slots
 * 3. available_slots = free_slots
 * 4. remaining_slots = max_slots - dead_slots
 * 5. objects_created + objects_remaining = max_objects
 * 6. active_slots matches size()
 *
 * Use with RC_ASSERT in property-based tests.
 */
template <typename MapT>
void
validate_statistics(MapT const & map)
{
    auto const stats = map.statistics();

    // Basic slot accounting: active + free + dead = allocated
    auto const computed_allocated = stats.active_slots + stats.free_slots +
        stats.dead_slots;
    RC_ASSERT(computed_allocated == stats.allocated_slots);

    // Index space accounting: allocated + unallocated = max_slots
    auto const computed_max = stats.allocated_slots + stats.unallocated_slots;
    RC_ASSERT(computed_max == stats.max_slots);

    // Capacity metrics
    RC_ASSERT(stats.available_slots == stats.free_slots);
    auto const computed_remaining = stats.max_slots - stats.dead_slots;
    RC_ASSERT(stats.remaining_slots == computed_remaining);

    // Object lifetime accounting
    auto const computed_total_objects = stats.objects_created +
        stats.objects_remaining;
    RC_ASSERT(computed_total_objects == stats.max_objects);

    // Cross-check with public API
    RC_ASSERT(stats.active_slots == map.size().value);

    // Objects created should be >= active
    RC_ASSERT(stats.objects_created >= stats.active_slots);

    // Dead slots can't exceed allocated slots
    RC_ASSERT(stats.dead_slots <= stats.allocated_slots);

    // Derived metrics should be in valid ranges
    if (stats.remaining_slots > 0) {
        RC_ASSERT(stats.slot_utilization >= 0.0);
        RC_ASSERT(stats.slot_utilization <= 1.0);
    }

    if (stats.allocated_slots > 0) {
        RC_ASSERT(stats.dead_slot_ratio >= 0.0);
        RC_ASSERT(stats.dead_slot_ratio <= 1.0);
    }

    if (stats.max_objects > 0) {
        RC_ASSERT(stats.lifetime_exhaustion >= 0.0);
        RC_ASSERT(stats.lifetime_exhaustion <= 1.0);
    }
}

} // namespace wjh::slotmap::test

#endif // WJH_SLOTMAP_B999CB0478DB4B9D946C2B87F06FC062
