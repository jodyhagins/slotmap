// ----------------------------------------------------------------------
// Copyright 2025 Jody Hagins
// Distributed under the MIT Software License
// See accompanying file LICENSE or copy at
// https://opensource.org/licenses/MIT
// ----------------------------------------------------------------------
//
// Sized Iteration Benchmarks
//
// This benchmark uses appropriately-sized index bits for each collection
// size to provide a fair comparison against std::unordered_map.
//
// The standard benchmarks use Key<T, wjh::slotmap::IndexBits(20),
// wjh::slotmap::VersionBits(12)> for all sizes, which means small collections
// (100 elements) still iterate over 4096-slot slabs, checking is_alive() for
// each slot. This is unfair to SlotMap for small sizes.
//
// Key bit totals must be 16, 32, 64, or 128. We use 32-bit keys for
// smaller sizes and use index bits appropriate to the collection size:
//
//   - Key<T, wjh::slotmap::IndexBits(7), wjh::slotmap::VersionBits(25)>   for
//   100 elements   (128 max slots)
//   - Key<T, wjh::slotmap::IndexBits(10), wjh::slotmap::VersionBits(22)>  for
//   1000 elements  (1024 max slots)
//   - Key<T, wjh::slotmap::IndexBits(13), wjh::slotmap::VersionBits(19)>  for
//   4096 elements  (8192 max slots)
//   - Key<T, wjh::slotmap::IndexBits(15), wjh::slotmap::VersionBits(17)>  for
//   32K elements   (32K max slots)
//   - Key<T, wjh::slotmap::IndexBits(18), wjh::slotmap::VersionBits(14)>  for
//   262K elements  (262K max slots)
//   - Key<T, wjh::slotmap::IndexBits(20), wjh::slotmap::VersionBits(12)>  for
//   1M elements    (1M max slots)
//
// All benchmarks test 4 configuration combinations:
//   - All_Alive:    SlotsPerSlab::All + UseAliveBitForLookup::Yes
//   - All_NoAlive:  SlotsPerSlab::All + UseAliveBitForLookup::No
//   - Dyn_Alive:    SlotsPerSlab::Dynamic + UseAliveBitForLookup::Yes
//   - Dyn_NoAlive:  SlotsPerSlab::Dynamic + UseAliveBitForLookup::No
//
// ----------------------------------------------------------------------

#include "benchmark_common.hpp"

namespace {

using namespace bench;

// ============================================================================
// Helper: Populate and iterate
// ============================================================================

template <typename SlotMapT>
void
run_iteration_benchmark(benchmark::State & state, std::size_t n)
{
    SlotMapT sm;
    for (std::size_t i = 0; i < n; ++i) {
        (void)sm.emplace(SmallValue{i, i * 2});
    }

    std::uint64_t sum = 0;
    for (auto _ : state) {
        sm.for_each([&](SmallValue const & v) { sum += v.data; });
    }

    benchmark::DoNotOptimize(sum);
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(n));
}

// ============================================================================
// SlotMap Iteration - Sized Keys (appropriately sized for collection)
// ============================================================================

// Register Sized iteration benchmarks for all sizes and configs
// clang-format off
BENCH_SLOTMAP_ALL_CONFIGS(Iterate_Sized, run_iteration_benchmark, SmallKey100, 100, 100);
BENCH_SLOTMAP_ALL_CONFIGS(Iterate_Sized, run_iteration_benchmark, SmallKey1K, 1K, 1000);
BENCH_SLOTMAP_ALL_CONFIGS(Iterate_Sized, run_iteration_benchmark, SmallKey4K, 4K, 4096);
BENCH_SLOTMAP_ALL_CONFIGS(Iterate_Sized, run_iteration_benchmark, SmallKey32K, 32K, 32768);
BENCH_SLOTMAP_ALL_CONFIGS(Iterate_Sized, run_iteration_benchmark, SmallKey262K, 262K, 262144);
BENCH_SLOTMAP_ALL_CONFIGS(Iterate_Sized, run_iteration_benchmark, SmallKey1M, 1M, 1048576);
// clang-format on

// ============================================================================
// SlotMap Iteration - Oversized Keys (original benchmark style)
// Uses 20-bit index even for small collections - wasteful iteration
// This demonstrates the cost of over-provisioning index bits
// ============================================================================

// Register Oversized iteration benchmarks for all configs
// clang-format off
BENCH_SLOTMAP_ALL_CONFIGS(Iterate_Oversized, run_iteration_benchmark, SmallKey1M, 100, 100);
BENCH_SLOTMAP_ALL_CONFIGS(Iterate_Oversized, run_iteration_benchmark, SmallKey1M, 1K, 1000);
BENCH_SLOTMAP_ALL_CONFIGS(Iterate_Oversized, run_iteration_benchmark, SmallKey1M, 4K, 4096);

// clang-format on

// ============================================================================
// unordered_map Iteration (baseline)
// ============================================================================

void
run_unordered_map_iteration(benchmark::State & state, std::size_t n)
{
    std::unordered_map<std::uint64_t, SmallValue> um;
    um.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        um.emplace(i, SmallValue{i, i * 2});
    }

    std::uint64_t sum = 0;
    for (auto _ : state) {
        for (auto const & [key, value] : um) {
            sum += value.data;
        }
    }

    benchmark::DoNotOptimize(sum);
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(n));
}

void
BM_UnorderedMap_Iterate_100(benchmark::State & state)
{
    run_unordered_map_iteration(state, 100);
}

BENCHMARK(BM_UnorderedMap_Iterate_100);

void
BM_UnorderedMap_Iterate_1K(benchmark::State & state)
{
    run_unordered_map_iteration(state, 1000);
}

BENCHMARK(BM_UnorderedMap_Iterate_1K);

void
BM_UnorderedMap_Iterate_4K(benchmark::State & state)
{
    run_unordered_map_iteration(state, 4096);
}

BENCHMARK(BM_UnorderedMap_Iterate_4K);

void
BM_UnorderedMap_Iterate_32K(benchmark::State & state)
{
    run_unordered_map_iteration(state, 32768);
}

BENCHMARK(BM_UnorderedMap_Iterate_32K);

void
BM_UnorderedMap_Iterate_262K(benchmark::State & state)
{
    run_unordered_map_iteration(state, 262144);
}

BENCHMARK(BM_UnorderedMap_Iterate_262K);

void
BM_UnorderedMap_Iterate_1M(benchmark::State & state)
{
    run_unordered_map_iteration(state, 1048576);
}

BENCHMARK(BM_UnorderedMap_Iterate_1M);

} // anonymous namespace
