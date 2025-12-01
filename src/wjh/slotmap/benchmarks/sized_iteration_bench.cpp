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
// The standard benchmarks use Key<T, 20, 12> for all sizes, which means
// small collections (100 elements) still iterate over 4096-slot slabs,
// checking is_alive() for each slot. This is unfair to SlotMap for small
// sizes.
//
// Key bit totals must be 16, 32, 64, or 128. We use 32-bit keys for
// smaller sizes and use index bits appropriate to the collection size:
//
//   - Key<T, 8, 24>   for 100 elements   (256 max slots)
//   - Key<T, 10, 22>  for 1000 elements  (1024 max slots)
//   - Key<T, 13, 19>  for 4096 elements  (8192 max slots)
//   - Key<T, 16, 16>  for 32K elements   (64K max slots)
//   - Key<T, 20, 12>  for 262K/1M        (1M max slots)
//
// ----------------------------------------------------------------------

#include <wjh/slotmap/SlotMap.hpp>

#include "benchmarking/benchmark.hpp"

#include <unordered_map>
#include <vector>

namespace {

// ============================================================================
// Value Type
// ============================================================================

struct Value
{
    std::uint64_t id;
    std::uint64_t data;
};

// ============================================================================
// Sized Key Types - Index bits sized to collection (all sum to 32 bits)
// ============================================================================

// For ~100 elements: 8 index bits = 256 max slots
using Key100 = wjh::slotmap::Key<Value, 8, 24>;
using SlotMap100 = wjh::SlotMap<Key100>;

// For ~1000 elements: 10 index bits = 1024 max slots
using Key1K = wjh::slotmap::Key<Value, 10, 22>;
using SlotMap1K = wjh::SlotMap<Key1K>;

// For ~4096 elements: 13 index bits = 8192 max slots
using Key4K = wjh::slotmap::Key<Value, 13, 19>;
using SlotMap4K = wjh::SlotMap<Key4K>;

// For ~32K elements: 16 index bits = 65536 max slots
using Key32K = wjh::slotmap::Key<Value, 16, 16>;
using SlotMap32K = wjh::SlotMap<Key32K>;

// For ~262K and 1M elements: 20 index bits = 1048576 max slots
using Key1M = wjh::slotmap::Key<Value, 20, 12>;
using SlotMap1M = wjh::SlotMap<Key1M>;

// ============================================================================
// Helper: Populate and iterate
// ============================================================================

template <typename SlotMapT>
void
run_iteration_benchmark(benchmark::State & state, std::size_t n)
{
    SlotMapT sm;
    for (std::size_t i = 0; i < n; ++i) {
        (void)sm.emplace(Value{i, i * 2});
    }

    std::uint64_t sum = 0;
    for (auto _ : state) {
        sm.for_each([&](Value const & v) { sum += v.data; });
    }

    benchmark::DoNotOptimize(sum);
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(n));
}

// ============================================================================
// SlotMap Iteration - Sized Keys (appropriately sized for collection)
// ============================================================================

void
BM_SlotMap_Iterate_Sized_100(benchmark::State & state)
{
    run_iteration_benchmark<SlotMap100>(state, 100);
}

BENCHMARK(BM_SlotMap_Iterate_Sized_100);

void
BM_SlotMap_Iterate_Sized_1K(benchmark::State & state)
{
    run_iteration_benchmark<SlotMap1K>(state, 1000);
}

BENCHMARK(BM_SlotMap_Iterate_Sized_1K);

void
BM_SlotMap_Iterate_Sized_4K(benchmark::State & state)
{
    run_iteration_benchmark<SlotMap4K>(state, 4096);
}

BENCHMARK(BM_SlotMap_Iterate_Sized_4K);

void
BM_SlotMap_Iterate_Sized_32K(benchmark::State & state)
{
    run_iteration_benchmark<SlotMap32K>(state, 32768);
}

BENCHMARK(BM_SlotMap_Iterate_Sized_32K);

void
BM_SlotMap_Iterate_Sized_262K(benchmark::State & state)
{
    run_iteration_benchmark<SlotMap1M>(state, 262144);
}

BENCHMARK(BM_SlotMap_Iterate_Sized_262K);

void
BM_SlotMap_Iterate_Sized_1M(benchmark::State & state)
{
    run_iteration_benchmark<SlotMap1M>(state, 1048576);
}

BENCHMARK(BM_SlotMap_Iterate_Sized_1M);

// ============================================================================
// SlotMap Iteration - Oversized Keys (original benchmark style)
// Uses 20-bit index even for small collections - wasteful iteration
// ============================================================================

void
BM_SlotMap_Iterate_Oversized_100(benchmark::State & state)
{
    run_iteration_benchmark<SlotMap1M>(state, 100);
}

BENCHMARK(BM_SlotMap_Iterate_Oversized_100);

void
BM_SlotMap_Iterate_Oversized_1K(benchmark::State & state)
{
    run_iteration_benchmark<SlotMap1M>(state, 1000);
}

BENCHMARK(BM_SlotMap_Iterate_Oversized_1K);

void
BM_SlotMap_Iterate_Oversized_4K(benchmark::State & state)
{
    run_iteration_benchmark<SlotMap1M>(state, 4096);
}

BENCHMARK(BM_SlotMap_Iterate_Oversized_4K);

// ============================================================================
// unordered_map Iteration (baseline)
// ============================================================================

void
run_unordered_map_iteration(benchmark::State & state, std::size_t n)
{
    std::unordered_map<std::uint64_t, Value> um;
    um.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        um.emplace(i, Value{i, i * 2});
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
