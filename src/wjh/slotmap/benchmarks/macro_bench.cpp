// ----------------------------------------------------------------------
// Copyright 2025 Jody Hagins
// Distributed under the MIT Software License
// See accompanying file LICENSE or copy at
// https://opensource.org/licenses/MIT
// ----------------------------------------------------------------------
//
// Macro-benchmarks: Realistic workload simulations
//
// These benchmarks simulate real-world usage patterns:
// - Game entity lifecycle (ECS-style spawn/despawn/query)
// - Static lookup tables (build once, query many)
// - Long-running session fragmentation
// - Mixed workloads (YCSB-inspired)
//
// All benchmarks test 4 configuration combinations:
//   - All_Alive:    SlotsPerSlab::All + UseAliveBitForLookup::Yes
//   - All_NoAlive:  SlotsPerSlab::All + UseAliveBitForLookup::No
//   - Dyn_Alive:    SlotsPerSlab::Dynamic + UseAliveBitForLookup::Yes
//   - Dyn_NoAlive:  SlotsPerSlab::Dynamic + UseAliveBitForLookup::No
//
// 32/32 keys only use Dynamic (All would require 4B+ slot allocation).
//
// ----------------------------------------------------------------------

#include "benchmark_common.hpp"

#include <deque>

namespace {

using namespace bench;

// ============================================================================
// GAME ENTITY LIFECYCLE BENCHMARK
// ============================================================================
//
// Simulates a game engine ECS pattern:
// - Start with N entities
// - Each "frame": delete ~10% randomly, spawn ~10% new
// - Query all entities each frame
// - Lookup specific entities by handle
//

/// Game entity lifecycle simulation for SlotMap (templated by size)
template <typename SlotMapT>
void
run_game_lifecycle(benchmark::State & state, std::size_t n)
{
    using key_type = typename SlotMapT::key_type;
    auto const frames = 100;
    auto const churn_rate = 0.10; // 10% churn per frame
    auto const churn_count = static_cast<std::size_t>(n * churn_rate);

    std::uint64_t total_ops = 0;

    for (auto _ : state) {
        state.PauseTiming();
        reset_rng();
        SlotMapT sm;
        std::vector<key_type> keys;
        keys.reserve(n * 2); // Extra capacity for churn
        for (std::size_t i = 0; i < n; ++i) {
            keys.push_back(sm.emplace(SmallValue{i, i}));
        }
        std::uniform_int_distribution<std::size_t> dist(0, keys.size() - 1);
        state.ResumeTiming();

        for (std::size_t frame = 0; frame < frames; ++frame) {
            // Delete random entities
            for (std::size_t i = 0; i < churn_count && not keys.empty(); ++i) {
                std::size_t idx = dist(get_rng()) % keys.size();
                sm.erase(keys[idx]);
                keys[idx] = keys.back();
                keys.pop_back();
                ++total_ops;
            }

            // Spawn new entities
            for (std::size_t i = 0; i < churn_count; ++i) {
                keys.push_back(sm.emplace(SmallValue{frame * 1000 + i, i}));
                ++total_ops;
            }

            // Query all entities (system update)
            std::uint64_t sum = 0;
            sm.for_each([&](SmallValue const & v) {
                sum += v.data;
                ++total_ops;
            });
            benchmark::DoNotOptimize(sum);

            // Random lookups (10% of entities)
            auto lookup_count = keys.size() / 10;
            for (std::size_t i = 0; i < lookup_count && not keys.empty(); ++i) {
                std::size_t idx = dist(get_rng()) % keys.size();
                (void)sm.use(keys[idx], [](SmallValue const & v) {
                    std::uint64_t tmp = v.data;
                    benchmark::DoNotOptimize(tmp);
                });
                ++total_ops;
            }
        }
    }

    set_items_processed(state, static_cast<std::int64_t>(total_ops));
}

// Register GameLifecycle benchmarks for all sizes and configs
// clang-format off
BENCH_SLOTMAP_ALL_CONFIGS(GameLifecycle, run_game_lifecycle, SmallKey100, 100, 100);
BENCH_SLOTMAP_ALL_CONFIGS(GameLifecycle, run_game_lifecycle, SmallKey1K, 1K, 1000);
BENCH_SLOTMAP_ALL_CONFIGS(GameLifecycle, run_game_lifecycle, SmallKey4K, 4K, 4096);
BENCH_SLOTMAP_ALL_CONFIGS(GameLifecycle, run_game_lifecycle, SmallKey32K, 32K, 32768);
BENCH_SLOTMAP_32_32(GameLifecycle, run_game_lifecycle, 1K, 1000);
BENCH_SLOTMAP_32_32(GameLifecycle, run_game_lifecycle, 32K, 32768);

// clang-format on

/// Game entity lifecycle simulation for unordered_map
void
run_unordered_map_game_lifecycle(benchmark::State & state, std::size_t n)
{
    auto const frames = 100;
    auto const churn_rate = 0.10;
    auto const churn_count = static_cast<std::size_t>(n * churn_rate);

    std::uint64_t total_ops = 0;

    for (auto _ : state) {
        state.PauseTiming();
        reset_rng();
        std::unordered_map<std::uint64_t, SmallValue> um;
        std::vector<std::uint64_t> keys;
        keys.reserve(n * 2);
        std::uint64_t next_id = 0;
        for (std::size_t i = 0; i < n; ++i) {
            um.emplace(next_id, SmallValue{next_id, next_id});
            keys.push_back(next_id);
            ++next_id;
        }
        std::uniform_int_distribution<std::size_t> dist(0, keys.size() - 1);
        state.ResumeTiming();

        for (std::size_t frame = 0; frame < frames; ++frame) {
            // Delete random entities
            for (std::size_t i = 0; i < churn_count && not keys.empty(); ++i) {
                std::size_t idx = dist(get_rng()) % keys.size();
                um.erase(keys[idx]);
                keys[idx] = keys.back();
                keys.pop_back();
                ++total_ops;
            }

            // Spawn new entities
            for (std::size_t i = 0; i < churn_count; ++i) {
                um.emplace(next_id, SmallValue{next_id, i});
                keys.push_back(next_id);
                ++next_id;
                ++total_ops;
            }

            // Query all entities
            std::uint64_t sum = 0;
            for (auto const & [key, value] : um) {
                sum += value.data;
                ++total_ops;
            }
            benchmark::DoNotOptimize(sum);

            // Random lookups
            auto lookup_count = keys.size() / 10;
            for (std::size_t i = 0; i < lookup_count && not keys.empty(); ++i) {
                std::size_t idx = dist(get_rng()) % keys.size();
                auto it = um.find(keys[idx]);
                if (it != um.end()) {
                    std::uint64_t tmp = it->second.data;
                    benchmark::DoNotOptimize(tmp);
                }
                ++total_ops;
            }
        }
    }

    set_items_processed(state, static_cast<std::int64_t>(total_ops));
}

void
BM_UnorderedMap_GameLifecycle_100(benchmark::State & state)
{
    run_unordered_map_game_lifecycle(state, 100);
}

BENCHMARK(BM_UnorderedMap_GameLifecycle_100);

void
BM_UnorderedMap_GameLifecycle_1K(benchmark::State & state)
{
    run_unordered_map_game_lifecycle(state, 1000);
}

BENCHMARK(BM_UnorderedMap_GameLifecycle_1K);

void
BM_UnorderedMap_GameLifecycle_4K(benchmark::State & state)
{
    run_unordered_map_game_lifecycle(state, 4096);
}

BENCHMARK(BM_UnorderedMap_GameLifecycle_4K);

void
BM_UnorderedMap_GameLifecycle_32K(benchmark::State & state)
{
    run_unordered_map_game_lifecycle(state, 32768);
}

BENCHMARK(BM_UnorderedMap_GameLifecycle_32K);

// ============================================================================
// STATIC LOOKUP TABLE BENCHMARK
// ============================================================================
//
// Simulates a resource table pattern:
// - Build a table of N resources once
// - Perform many random lookups (10x more lookups than elements)
// - No modifications after initial build
//

/// Static lookup table for SlotMap (templated)
template <typename SlotMapT>
void
run_slotmap_static_lookup(benchmark::State & state, std::size_t n)
{
    auto const lookups = n * 10;
    reset_rng();

    // Build table once
    SlotMapT sm;
    auto keys = populate_slotmap(sm, n);

    // Pre-generate random access pattern
    auto access_pattern = shuffled_indices(n);
    // Extend to desired lookup count
    std::vector<std::size_t> lookup_indices;
    lookup_indices.reserve(lookups);
    for (std::size_t i = 0; i < lookups; ++i) {
        lookup_indices.push_back(access_pattern[i % n]);
    }
    shuffle(lookup_indices);

    std::size_t found = 0;
    for (auto _ : state) {
        for (std::size_t idx : lookup_indices) {
            (void)sm.use(keys[idx], [&](SmallValue const & v) {
                found += v.data;
            });
        }
    }

    benchmark::DoNotOptimize(found);
    set_items_processed(
        state,
        state.iterations() * static_cast<std::int64_t>(lookups));
}

// Register StaticLookup benchmarks for all sizes and configs
// clang-format off
BENCH_SLOTMAP_ALL_CONFIGS(StaticLookup, run_slotmap_static_lookup, SmallKey100, 100, 100);
BENCH_SLOTMAP_ALL_CONFIGS(StaticLookup, run_slotmap_static_lookup, SmallKey1K, 1K, 1000);
BENCH_SLOTMAP_ALL_CONFIGS(StaticLookup, run_slotmap_static_lookup, SmallKey4K, 4K, 4096);
BENCH_SLOTMAP_ALL_CONFIGS(StaticLookup, run_slotmap_static_lookup, SmallKey32K, 32K, 32768);
BENCH_SLOTMAP_ALL_CONFIGS(StaticLookup, run_slotmap_static_lookup, SmallKey262K, 262K, 262144);
BENCH_SLOTMAP_32_32(StaticLookup, run_slotmap_static_lookup, 1K, 1000);
BENCH_SLOTMAP_32_32(StaticLookup, run_slotmap_static_lookup, 32K, 32768);

// clang-format on

/// Static lookup table for unordered_map
void
run_unordered_map_static_lookup(benchmark::State & state, std::size_t n)
{
    auto const lookups = n * 10;
    reset_rng();

    // Build table once
    std::unordered_map<std::uint64_t, SmallValue> um;
    auto keys = populate_unordered_map<SmallValue>(um, n);

    // Pre-generate random access pattern
    auto access_pattern = shuffled_indices(n);
    std::vector<std::size_t> lookup_indices;
    lookup_indices.reserve(lookups);
    for (std::size_t i = 0; i < lookups; ++i) {
        lookup_indices.push_back(access_pattern[i % n]);
    }
    shuffle(lookup_indices);

    std::size_t found = 0;
    for (auto _ : state) {
        for (std::size_t idx : lookup_indices) {
            auto it = um.find(keys[idx]);
            if (it != um.end()) {
                found += it->second.data;
            }
        }
    }

    benchmark::DoNotOptimize(found);
    set_items_processed(
        state,
        state.iterations() * static_cast<std::int64_t>(lookups));
}

void
BM_UnorderedMap_StaticLookup_100(benchmark::State & state)
{
    run_unordered_map_static_lookup(state, 100);
}

BENCHMARK(BM_UnorderedMap_StaticLookup_100);

void
BM_UnorderedMap_StaticLookup_1K(benchmark::State & state)
{
    run_unordered_map_static_lookup(state, 1000);
}

BENCHMARK(BM_UnorderedMap_StaticLookup_1K);

void
BM_UnorderedMap_StaticLookup_4K(benchmark::State & state)
{
    run_unordered_map_static_lookup(state, 4096);
}

BENCHMARK(BM_UnorderedMap_StaticLookup_4K);

void
BM_UnorderedMap_StaticLookup_32K(benchmark::State & state)
{
    run_unordered_map_static_lookup(state, 32768);
}

BENCHMARK(BM_UnorderedMap_StaticLookup_32K);

void
BM_UnorderedMap_StaticLookup_262K(benchmark::State & state)
{
    run_unordered_map_static_lookup(state, 262144);
}

BENCHMARK(BM_UnorderedMap_StaticLookup_262K);

// ============================================================================
// FRAGMENTATION OVER TIME BENCHMARK
// ============================================================================
//
// Simulates long-running application:
// - Start with N elements
// - Over many frames: delete random, insert new (maintaining ~N elements)
// - Measure iteration performance degradation
//

/// Fragmentation stress test for SlotMap
template <typename SlotMapT>
void
run_slotmap_fragmentation(benchmark::State & state, std::size_t n)
{
    using key_type = typename SlotMapT::key_type;
    auto const frames = 500;
    auto const ops_per_frame = n / 20; // 5% churn per frame

    std::uint64_t iteration_sum = 0;

    for (auto _ : state) {
        state.PauseTiming();
        reset_rng();
        SlotMapT sm;
        std::deque<key_type> keys; // Use deque for efficient front removal
        for (std::size_t i = 0; i < n; ++i) {
            keys.push_back(sm.emplace(SmallValue{i, i}));
        }
        state.ResumeTiming();

        for (std::size_t frame = 0; frame < frames; ++frame) {
            // Delete from front (oldest), insert at back
            // This creates maximum fragmentation
            for (std::size_t i = 0; i < ops_per_frame && not keys.empty(); ++i)
            {
                sm.erase(keys.front());
                keys.pop_front();
                keys.push_back(sm.emplace(SmallValue{frame * 1000 + i, i}));
            }

            // Measure iteration cost every 50 frames
            if (frame % 50 == 0) {
                sm.for_each(
                    [&](SmallValue const & v) { iteration_sum += v.data; });
            }
        }
    }

    benchmark::DoNotOptimize(iteration_sum);
    auto ops = frames * ops_per_frame * 2 + // insert + delete
        (frames / 50 + 1) * n; // iterations
    set_items_processed(
        state,
        state.iterations() * static_cast<std::int64_t>(ops));
}

// Register Fragmentation benchmarks for all sizes and configs
// clang-format off
BENCH_SLOTMAP_ALL_CONFIGS(Fragmentation, run_slotmap_fragmentation, SmallKey100, 100, 100);
BENCH_SLOTMAP_ALL_CONFIGS(Fragmentation, run_slotmap_fragmentation, SmallKey1K, 1K, 1000);
BENCH_SLOTMAP_ALL_CONFIGS(Fragmentation, run_slotmap_fragmentation, SmallKey4K, 4K, 4096);
BENCH_SLOTMAP_ALL_CONFIGS(Fragmentation, run_slotmap_fragmentation, SmallKey32K, 32K, 32768);
BENCH_SLOTMAP_32_32(Fragmentation, run_slotmap_fragmentation, 1K, 1000);

// clang-format on

/// Fragmentation stress test for unordered_map
void
run_unordered_map_fragmentation(benchmark::State & state, std::size_t n)
{
    auto const frames = 500;
    auto const ops_per_frame = n / 20;

    std::uint64_t iteration_sum = 0;

    for (auto _ : state) {
        state.PauseTiming();
        reset_rng();
        std::unordered_map<std::uint64_t, SmallValue> um;
        std::deque<std::uint64_t> keys;
        std::uint64_t next_id = 0;
        for (std::size_t i = 0; i < n; ++i) {
            um.emplace(next_id, SmallValue{next_id, next_id});
            keys.push_back(next_id);
            ++next_id;
        }
        state.ResumeTiming();

        for (std::size_t frame = 0; frame < frames; ++frame) {
            for (std::size_t i = 0; i < ops_per_frame && not keys.empty(); ++i)
            {
                um.erase(keys.front());
                keys.pop_front();
                um.emplace(next_id, SmallValue{next_id, i});
                keys.push_back(next_id);
                ++next_id;
            }

            if (frame % 50 == 0) {
                for (auto const & [key, value] : um) {
                    iteration_sum += value.data;
                }
            }
        }
    }

    benchmark::DoNotOptimize(iteration_sum);
    auto ops = frames * ops_per_frame * 2 + (frames / 50 + 1) * n;
    set_items_processed(
        state,
        state.iterations() * static_cast<std::int64_t>(ops));
}

void
BM_UnorderedMap_Fragmentation_100(benchmark::State & state)
{
    run_unordered_map_fragmentation(state, 100);
}

BENCHMARK(BM_UnorderedMap_Fragmentation_100);

void
BM_UnorderedMap_Fragmentation_1K(benchmark::State & state)
{
    run_unordered_map_fragmentation(state, 1000);
}

BENCHMARK(BM_UnorderedMap_Fragmentation_1K);

void
BM_UnorderedMap_Fragmentation_4K(benchmark::State & state)
{
    run_unordered_map_fragmentation(state, 4096);
}

BENCHMARK(BM_UnorderedMap_Fragmentation_4K);

void
BM_UnorderedMap_Fragmentation_32K(benchmark::State & state)
{
    run_unordered_map_fragmentation(state, 32768);
}

BENCHMARK(BM_UnorderedMap_Fragmentation_32K);

// ============================================================================
// MIXED WORKLOAD BENCHMARKS (YCSB-inspired)
// ============================================================================
//
// Based on Yahoo! Cloud Serving Benchmark workload patterns:
// - Workload A: Update heavy (50% reads, 50% updates)
// - Workload B: Read mostly (95% reads, 5% updates)
//

/// YCSB Workload A: 50% reads, 50% updates (SlotMap)
template <typename SlotMapT>
void
run_slotmap_ycsb_a(benchmark::State & state, std::size_t n)
{
    auto const ops = n * 10;
    reset_rng();

    SlotMapT sm;
    auto keys = populate_slotmap(sm, n);

    std::uniform_int_distribution<std::size_t> key_dist(0, n - 1);
    std::uniform_int_distribution<int> op_dist(0, 1); // 50/50

    std::uint64_t read_sum = 0;
    for (auto _ : state) {
        for (std::size_t i = 0; i < ops; ++i) {
            std::size_t idx = key_dist(get_rng());
            if (op_dist(get_rng()) == 0) {
                // Read
                (void)sm.use(keys[idx], [&](SmallValue const & v) {
                    read_sum += v.data;
                });
            } else {
                // Update (read-modify-write)
                (void)sm.use(keys[idx], [i](SmallValue & v) { v.data = i; });
            }
        }
    }

    benchmark::DoNotOptimize(read_sum);
    set_items_processed(
        state,
        state.iterations() * static_cast<std::int64_t>(ops));
}

// Register YCSB-A benchmarks for all sizes and configs
// clang-format off
BENCH_SLOTMAP_ALL_CONFIGS(YCSB_A, run_slotmap_ycsb_a, SmallKey100, 100, 100);
BENCH_SLOTMAP_ALL_CONFIGS(YCSB_A, run_slotmap_ycsb_a, SmallKey1K, 1K, 1000);
BENCH_SLOTMAP_ALL_CONFIGS(YCSB_A, run_slotmap_ycsb_a, SmallKey4K, 4K, 4096);
BENCH_SLOTMAP_ALL_CONFIGS(YCSB_A, run_slotmap_ycsb_a, SmallKey32K, 32K, 32768);
BENCH_SLOTMAP_32_32(YCSB_A, run_slotmap_ycsb_a, 1K, 1000);

// clang-format on

/// YCSB Workload A: 50% reads, 50% updates (unordered_map)
void
run_unordered_map_ycsb_a(benchmark::State & state, std::size_t n)
{
    auto const ops = n * 10;
    reset_rng();

    std::unordered_map<std::uint64_t, SmallValue> um;
    auto keys = populate_unordered_map<SmallValue>(um, n);

    std::uniform_int_distribution<std::size_t> key_dist(0, n - 1);
    std::uniform_int_distribution<int> op_dist(0, 1);

    std::uint64_t read_sum = 0;
    for (auto _ : state) {
        for (std::size_t i = 0; i < ops; ++i) {
            std::size_t idx = key_dist(get_rng());
            auto it = um.find(keys[idx]);
            if (it != um.end()) {
                if (op_dist(get_rng()) == 0) {
                    read_sum += it->second.data;
                } else {
                    it->second.data = i;
                }
            }
        }
    }

    benchmark::DoNotOptimize(read_sum);
    set_items_processed(
        state,
        state.iterations() * static_cast<std::int64_t>(ops));
}

void
BM_UnorderedMap_YCSB_A_100(benchmark::State & state)
{
    run_unordered_map_ycsb_a(state, 100);
}

BENCHMARK(BM_UnorderedMap_YCSB_A_100);

void
BM_UnorderedMap_YCSB_A_1K(benchmark::State & state)
{
    run_unordered_map_ycsb_a(state, 1000);
}

BENCHMARK(BM_UnorderedMap_YCSB_A_1K);

void
BM_UnorderedMap_YCSB_A_4K(benchmark::State & state)
{
    run_unordered_map_ycsb_a(state, 4096);
}

BENCHMARK(BM_UnorderedMap_YCSB_A_4K);

void
BM_UnorderedMap_YCSB_A_32K(benchmark::State & state)
{
    run_unordered_map_ycsb_a(state, 32768);
}

BENCHMARK(BM_UnorderedMap_YCSB_A_32K);

/// YCSB Workload B: 95% reads, 5% updates (SlotMap)
template <typename SlotMapT>
void
run_slotmap_ycsb_b(benchmark::State & state, std::size_t n)
{
    auto const ops = n * 10;
    reset_rng();

    SlotMapT sm;
    auto keys = populate_slotmap(sm, n);

    std::uniform_int_distribution<std::size_t> key_dist(0, n - 1);
    std::uniform_int_distribution<int> op_dist(0, 19); // 95/5 = 19:1

    std::uint64_t read_sum = 0;
    for (auto _ : state) {
        for (std::size_t i = 0; i < ops; ++i) {
            std::size_t idx = key_dist(get_rng());
            if (op_dist(get_rng()) != 0) {
                // Read (95%)
                (void)sm.use(keys[idx], [&](SmallValue const & v) {
                    read_sum += v.data;
                });
            } else {
                // Update (5%)
                (void)sm.use(keys[idx], [i](SmallValue & v) { v.data = i; });
            }
        }
    }

    benchmark::DoNotOptimize(read_sum);
    set_items_processed(
        state,
        state.iterations() * static_cast<std::int64_t>(ops));
}

// Register YCSB-B benchmarks for all sizes and configs
// clang-format off
BENCH_SLOTMAP_ALL_CONFIGS(YCSB_B, run_slotmap_ycsb_b, SmallKey100, 100, 100);
BENCH_SLOTMAP_ALL_CONFIGS(YCSB_B, run_slotmap_ycsb_b, SmallKey1K, 1K, 1000);
BENCH_SLOTMAP_ALL_CONFIGS(YCSB_B, run_slotmap_ycsb_b, SmallKey4K, 4K, 4096);
BENCH_SLOTMAP_ALL_CONFIGS(YCSB_B, run_slotmap_ycsb_b, SmallKey32K, 32K, 32768);
BENCH_SLOTMAP_32_32(YCSB_B, run_slotmap_ycsb_b, 1K, 1000);

// clang-format on

/// YCSB Workload B: 95% reads, 5% updates (unordered_map)
void
run_unordered_map_ycsb_b(benchmark::State & state, std::size_t n)
{
    auto const ops = n * 10;
    reset_rng();

    std::unordered_map<std::uint64_t, SmallValue> um;
    auto keys = populate_unordered_map<SmallValue>(um, n);

    std::uniform_int_distribution<std::size_t> key_dist(0, n - 1);
    std::uniform_int_distribution<int> op_dist(0, 19);

    std::uint64_t read_sum = 0;
    for (auto _ : state) {
        for (std::size_t i = 0; i < ops; ++i) {
            std::size_t idx = key_dist(get_rng());
            auto it = um.find(keys[idx]);
            if (it != um.end()) {
                if (op_dist(get_rng()) != 0) {
                    read_sum += it->second.data;
                } else {
                    it->second.data = i;
                }
            }
        }
    }

    benchmark::DoNotOptimize(read_sum);
    set_items_processed(
        state,
        state.iterations() * static_cast<std::int64_t>(ops));
}

void
BM_UnorderedMap_YCSB_B_100(benchmark::State & state)
{
    run_unordered_map_ycsb_b(state, 100);
}

BENCHMARK(BM_UnorderedMap_YCSB_B_100);

void
BM_UnorderedMap_YCSB_B_1K(benchmark::State & state)
{
    run_unordered_map_ycsb_b(state, 1000);
}

BENCHMARK(BM_UnorderedMap_YCSB_B_1K);

void
BM_UnorderedMap_YCSB_B_4K(benchmark::State & state)
{
    run_unordered_map_ycsb_b(state, 4096);
}

BENCHMARK(BM_UnorderedMap_YCSB_B_4K);

void
BM_UnorderedMap_YCSB_B_32K(benchmark::State & state)
{
    run_unordered_map_ycsb_b(state, 32768);
}

BENCHMARK(BM_UnorderedMap_YCSB_B_32K);

// ============================================================================
// INSERT/ERASE INTERLEAVED BENCHMARK
// ============================================================================
//
// Tests performance when insertions and deletions are interleaved,
// which is common in object pools and resource managers.
//

/// Interleaved insert/erase for SlotMap
template <typename SlotMapT>
void
run_slotmap_interleaved(benchmark::State & state, std::size_t n)
{
    using key_type = typename SlotMapT::key_type;
    auto const ops = n * 2;
    reset_rng();

    SlotMapT sm;
    std::vector<key_type> keys;
    keys.reserve(n);

    // Pre-fill half
    for (std::size_t i = 0; i < n / 2; ++i) {
        keys.push_back(sm.emplace(SmallValue{i, i}));
    }

    std::uniform_int_distribution<int> op_dist(0, 1);

    for (auto _ : state) {
        for (std::size_t i = 0; i < ops; ++i) {
            if (keys.empty() || (keys.size() < n && op_dist(get_rng()) == 0)) {
                // Insert
                auto key = sm.emplace(SmallValue{i, i});
                keys.push_back(key);
                benchmark::DoNotOptimize(key);
            } else {
                // Erase random
                std::uniform_int_distribution<std::size_t> idx_dist(
                    0,
                    keys.size() - 1);
                std::size_t idx = idx_dist(get_rng());
                sm.erase(keys[idx]);
                keys[idx] = keys.back();
                keys.pop_back();
            }
        }
    }

    set_items_processed(
        state,
        state.iterations() * static_cast<std::int64_t>(ops));
}

// Register Interleaved benchmarks for all sizes and configs
// clang-format off
BENCH_SLOTMAP_ALL_CONFIGS(Interleaved, run_slotmap_interleaved, SmallKey100, 100, 100);
BENCH_SLOTMAP_ALL_CONFIGS(Interleaved, run_slotmap_interleaved, SmallKey1K, 1K, 1000);
BENCH_SLOTMAP_ALL_CONFIGS(Interleaved, run_slotmap_interleaved, SmallKey4K, 4K, 4096);
BENCH_SLOTMAP_ALL_CONFIGS(Interleaved, run_slotmap_interleaved, SmallKey32K, 32K, 32768);
BENCH_SLOTMAP_32_32(Interleaved, run_slotmap_interleaved, 1K, 1000);

// clang-format on

/// Interleaved insert/erase for unordered_map
void
run_unordered_map_interleaved(benchmark::State & state, std::size_t n)
{
    auto const ops = n * 2;
    reset_rng();

    std::unordered_map<std::uint64_t, SmallValue> um;
    std::vector<std::uint64_t> keys;
    keys.reserve(n);
    std::uint64_t next_id = 0;

    // Pre-fill half
    for (std::size_t i = 0; i < n / 2; ++i) {
        um.emplace(next_id, SmallValue{next_id, next_id});
        keys.push_back(next_id);
        ++next_id;
    }

    std::uniform_int_distribution<int> op_dist(0, 1);

    for (auto _ : state) {
        for (std::size_t i = 0; i < ops; ++i) {
            if (keys.empty() || (keys.size() < n && op_dist(get_rng()) == 0)) {
                // Insert
                auto [it, inserted] = um.emplace(
                    next_id,
                    SmallValue{next_id, next_id});
                keys.push_back(next_id);
                ++next_id;
                benchmark::DoNotOptimize(it);
            } else {
                // Erase random
                std::uniform_int_distribution<std::size_t> idx_dist(
                    0,
                    keys.size() - 1);
                std::size_t idx = idx_dist(get_rng());
                um.erase(keys[idx]);
                keys[idx] = keys.back();
                keys.pop_back();
            }
        }
    }

    set_items_processed(
        state,
        state.iterations() * static_cast<std::int64_t>(ops));
}

void
BM_UnorderedMap_Interleaved_100(benchmark::State & state)
{
    run_unordered_map_interleaved(state, 100);
}

BENCHMARK(BM_UnorderedMap_Interleaved_100);

void
BM_UnorderedMap_Interleaved_1K(benchmark::State & state)
{
    run_unordered_map_interleaved(state, 1000);
}

BENCHMARK(BM_UnorderedMap_Interleaved_1K);

void
BM_UnorderedMap_Interleaved_4K(benchmark::State & state)
{
    run_unordered_map_interleaved(state, 4096);
}

BENCHMARK(BM_UnorderedMap_Interleaved_4K);

void
BM_UnorderedMap_Interleaved_32K(benchmark::State & state)
{
    run_unordered_map_interleaved(state, 32768);
}

BENCHMARK(BM_UnorderedMap_Interleaved_32K);

// ============================================================================
// RESERVE/BULK INSERT BENCHMARK
// ============================================================================
//
// Tests the benefit of pre-allocating memory before bulk insertions.
//

/// Bulk insert with reserve (SlotMap) - using 1M key for large sizes
template <typename SlotMapT>
void
run_slotmap_bulk_insert_reserve(benchmark::State & state, std::size_t n)
{
    using size_type = typename SlotMapT::size_type;

    for (auto _ : state) {
        SlotMapT sm;
        sm.reserve(size_type{static_cast<typename size_type::value_type>(n)});
        for (std::size_t i = 0; i < n; ++i) {
            auto key = sm.emplace(SmallValue{i, i * 2});
            benchmark::DoNotOptimize(key);
        }
        benchmark::ClobberMemory();
    }

    set_items_processed(
        state,
        state.iterations() * static_cast<std::int64_t>(n));
}

// Register BulkInsertReserve benchmarks for all sizes and configs
// clang-format off
BENCH_SLOTMAP_ALL_CONFIGS(BulkInsertReserve, run_slotmap_bulk_insert_reserve, SmallKey1K, 1K, 1000);
BENCH_SLOTMAP_ALL_CONFIGS(BulkInsertReserve, run_slotmap_bulk_insert_reserve, SmallKey4K, 4K, 4096);
BENCH_SLOTMAP_ALL_CONFIGS(BulkInsertReserve, run_slotmap_bulk_insert_reserve, SmallKey32K, 32K, 32768);
BENCH_SLOTMAP_ALL_CONFIGS(BulkInsertReserve, run_slotmap_bulk_insert_reserve, SmallKey262K, 262K, 262144);

// clang-format on

/// Bulk insert without reserve (SlotMap)
template <typename SlotMapT>
void
run_slotmap_bulk_insert_no_reserve(benchmark::State & state, std::size_t n)
{
    for (auto _ : state) {
        SlotMapT sm;
        // No reserve call
        for (std::size_t i = 0; i < n; ++i) {
            auto key = sm.emplace(SmallValue{i, i * 2});
            benchmark::DoNotOptimize(key);
        }
        benchmark::ClobberMemory();
    }

    set_items_processed(
        state,
        state.iterations() * static_cast<std::int64_t>(n));
}

// Register BulkInsertNoReserve benchmarks for all sizes and configs
// clang-format off
BENCH_SLOTMAP_ALL_CONFIGS(BulkInsertNoReserve, run_slotmap_bulk_insert_no_reserve, SmallKey1K, 1K, 1000);
BENCH_SLOTMAP_ALL_CONFIGS(BulkInsertNoReserve, run_slotmap_bulk_insert_no_reserve, SmallKey4K, 4K, 4096);
BENCH_SLOTMAP_ALL_CONFIGS(BulkInsertNoReserve, run_slotmap_bulk_insert_no_reserve, SmallKey32K, 32K, 32768);
BENCH_SLOTMAP_ALL_CONFIGS(BulkInsertNoReserve, run_slotmap_bulk_insert_no_reserve, SmallKey262K, 262K, 262144);
// clang-format on

} // anonymous namespace
