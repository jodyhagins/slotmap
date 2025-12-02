// ----------------------------------------------------------------------
// Copyright 2025 Jody Hagins
// Distributed under the MIT Software License
// See accompanying file LICENSE or copy at
// https://opensource.org/licenses/MIT
// ----------------------------------------------------------------------
//
// Micro-benchmarks: Core operations (insert, lookup, erase, iterate)
// Compares SlotMap vs std::unordered_map on fundamental operations.
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

namespace {

using namespace bench;

// ============================================================================
// INSERT BENCHMARKS
// ============================================================================

template <typename SlotMapT>
void
run_insert_benchmark(benchmark::State & state, std::size_t n)
{
    using value_type = typename SlotMapT::mapped_type;

    for (auto _ : state) {
        SlotMapT sm;
        for (std::size_t i = 0; i < n; ++i) {
            value_type v{};
            v.id = i;
            auto key = sm.emplace(std::move(v));
            benchmark::DoNotOptimize(key);
        }
        benchmark::ClobberMemory();
    }

    set_items_processed(
        state,
        state.iterations() * static_cast<std::int64_t>(n));
}

// Register INSERT benchmarks for all sizes and configs
// clang-format off
BENCH_SLOTMAP_ALL_CONFIGS(Insert, run_insert_benchmark, SmallKey100, 100, 100);
BENCH_SLOTMAP_ALL_CONFIGS(Insert, run_insert_benchmark, SmallKey1K, 1K, 1000);
BENCH_SLOTMAP_ALL_CONFIGS(Insert, run_insert_benchmark, SmallKey4K, 4K, 4096);
BENCH_SLOTMAP_ALL_CONFIGS(Insert, run_insert_benchmark, SmallKey32K, 32K, 32768);
BENCH_SLOTMAP_ALL_CONFIGS(Insert, run_insert_benchmark, SmallKey262K, 262K, 262144);
BENCH_SLOTMAP_ALL_CONFIGS(Insert, run_insert_benchmark, SmallKey1M, 1M, 1048576);
BENCH_SLOTMAP_32_32(Insert, run_insert_benchmark, 1M, 1048576);

// clang-format on

// unordered_map baseline
template <typename ValueT>
void
run_unordered_insert(benchmark::State & state, std::size_t n)
{
    for (auto _ : state) {
        std::unordered_map<std::uint64_t, ValueT> um;
        um.reserve(n);
        for (std::size_t i = 0; i < n; ++i) {
            ValueT v{};
            v.id = i;
            auto [it, inserted] = um.emplace(i, std::move(v));
            benchmark::DoNotOptimize(it);
        }
        benchmark::ClobberMemory();
    }

    set_items_processed(
        state,
        state.iterations() * static_cast<std::int64_t>(n));
}

void
BM_UnorderedMap_Insert_100(benchmark::State & state)
{
    run_unordered_insert<SmallValue>(state, 100);
}

BENCHMARK(BM_UnorderedMap_Insert_100);

void
BM_UnorderedMap_Insert_1K(benchmark::State & state)
{
    run_unordered_insert<SmallValue>(state, 1000);
}

BENCHMARK(BM_UnorderedMap_Insert_1K);

void
BM_UnorderedMap_Insert_4K(benchmark::State & state)
{
    run_unordered_insert<SmallValue>(state, 4096);
}

BENCHMARK(BM_UnorderedMap_Insert_4K);

void
BM_UnorderedMap_Insert_32K(benchmark::State & state)
{
    run_unordered_insert<SmallValue>(state, 32768);
}

BENCHMARK(BM_UnorderedMap_Insert_32K);

void
BM_UnorderedMap_Insert_262K(benchmark::State & state)
{
    run_unordered_insert<SmallValue>(state, 262144);
}

BENCHMARK(BM_UnorderedMap_Insert_262K);

void
BM_UnorderedMap_Insert_1M(benchmark::State & state)
{
    run_unordered_insert<SmallValue>(state, 1048576);
}

BENCHMARK(BM_UnorderedMap_Insert_1M);

// ============================================================================
// LOOKUP BENCHMARKS - Random Access
// ============================================================================

template <typename SlotMapT>
void
run_lookup_random(benchmark::State & state, std::size_t n)
{
    reset_rng();
    SlotMapT sm;
    auto keys = populate_slotmap(sm, n);
    auto access_order = shuffled_indices(n);

    std::size_t found = 0;
    for (auto _ : state) {
        for (std::size_t idx : access_order) {
            sm.use(keys[idx], [&](auto const & v) { found += v.data; });
        }
    }

    benchmark::DoNotOptimize(found);
    set_items_processed(
        state,
        state.iterations() * static_cast<std::int64_t>(n));
}

// Register LOOKUP benchmarks for all sizes and configs
// clang-format off
BENCH_SLOTMAP_ALL_CONFIGS(Lookup_Random, run_lookup_random, SmallKey100, 100, 100);
BENCH_SLOTMAP_ALL_CONFIGS(Lookup_Random, run_lookup_random, SmallKey1K, 1K, 1000);
BENCH_SLOTMAP_ALL_CONFIGS(Lookup_Random, run_lookup_random, SmallKey4K, 4K, 4096);
BENCH_SLOTMAP_ALL_CONFIGS(Lookup_Random, run_lookup_random, SmallKey32K, 32K, 32768);
BENCH_SLOTMAP_ALL_CONFIGS(Lookup_Random, run_lookup_random, SmallKey262K, 262K, 262144);
BENCH_SLOTMAP_ALL_CONFIGS(Lookup_Random, run_lookup_random, SmallKey1M, 1M, 1048576);
BENCH_SLOTMAP_32_32(Lookup_Random, run_lookup_random, 1M, 1048576);

// clang-format on

// unordered_map baseline
template <typename ValueT>
void
run_unordered_lookup_random(benchmark::State & state, std::size_t n)
{
    reset_rng();
    std::unordered_map<std::uint64_t, ValueT> um;
    auto keys = populate_unordered_map<ValueT>(um, n);
    auto access_order = shuffled_indices(n);

    std::size_t found = 0;
    for (auto _ : state) {
        for (std::size_t idx : access_order) {
            auto it = um.find(keys[idx]);
            if (it != um.end()) {
                found += it->second.data;
            }
        }
    }

    benchmark::DoNotOptimize(found);
    set_items_processed(
        state,
        state.iterations() * static_cast<std::int64_t>(n));
}

void
BM_UnorderedMap_Lookup_Random_100(benchmark::State & state)
{
    run_unordered_lookup_random<SmallValue>(state, 100);
}

BENCHMARK(BM_UnorderedMap_Lookup_Random_100);

void
BM_UnorderedMap_Lookup_Random_1K(benchmark::State & state)
{
    run_unordered_lookup_random<SmallValue>(state, 1000);
}

BENCHMARK(BM_UnorderedMap_Lookup_Random_1K);

void
BM_UnorderedMap_Lookup_Random_4K(benchmark::State & state)
{
    run_unordered_lookup_random<SmallValue>(state, 4096);
}

BENCHMARK(BM_UnorderedMap_Lookup_Random_4K);

void
BM_UnorderedMap_Lookup_Random_32K(benchmark::State & state)
{
    run_unordered_lookup_random<SmallValue>(state, 32768);
}

BENCHMARK(BM_UnorderedMap_Lookup_Random_32K);

void
BM_UnorderedMap_Lookup_Random_262K(benchmark::State & state)
{
    run_unordered_lookup_random<SmallValue>(state, 262144);
}

BENCHMARK(BM_UnorderedMap_Lookup_Random_262K);

void
BM_UnorderedMap_Lookup_Random_1M(benchmark::State & state)
{
    run_unordered_lookup_random<SmallValue>(state, 1048576);
}

BENCHMARK(BM_UnorderedMap_Lookup_Random_1M);

// ============================================================================
// ERASE BENCHMARKS
// ============================================================================

template <typename SlotMapT>
void
run_erase_random(benchmark::State & state, std::size_t n)
{
    for (auto _ : state) {
        state.PauseTiming();
        reset_rng();
        SlotMapT sm;
        auto keys = populate_slotmap(sm, n);
        shuffle(keys);
        state.ResumeTiming();

        for (auto const & key : keys) {
            bool erased = sm.erase(key);
            benchmark::DoNotOptimize(erased);
        }
    }

    set_items_processed(
        state,
        state.iterations() * static_cast<std::int64_t>(n));
}

// Register ERASE benchmarks for all sizes and configs
// clang-format off
BENCH_SLOTMAP_ALL_CONFIGS(Erase, run_erase_random, SmallKey100, 100, 100);
BENCH_SLOTMAP_ALL_CONFIGS(Erase, run_erase_random, SmallKey1K, 1K, 1000);
BENCH_SLOTMAP_ALL_CONFIGS(Erase, run_erase_random, SmallKey4K, 4K, 4096);
BENCH_SLOTMAP_ALL_CONFIGS(Erase, run_erase_random, SmallKey32K, 32K, 32768);
BENCH_SLOTMAP_ALL_CONFIGS(Erase, run_erase_random, SmallKey262K, 262K, 262144);
BENCH_SLOTMAP_ALL_CONFIGS(Erase, run_erase_random, SmallKey1M, 1M, 1048576);
BENCH_SLOTMAP_32_32(Erase, run_erase_random, 1M, 1048576);

// clang-format on

// unordered_map baseline
template <typename ValueT>
void
run_unordered_erase(benchmark::State & state, std::size_t n)
{
    for (auto _ : state) {
        state.PauseTiming();
        reset_rng();
        std::unordered_map<std::uint64_t, ValueT> um;
        auto keys = populate_unordered_map<ValueT>(um, n);
        shuffle(keys);
        state.ResumeTiming();

        for (auto key : keys) {
            auto erased = um.erase(key);
            benchmark::DoNotOptimize(erased);
        }
    }

    set_items_processed(
        state,
        state.iterations() * static_cast<std::int64_t>(n));
}

void
BM_UnorderedMap_Erase_100(benchmark::State & state)
{
    run_unordered_erase<SmallValue>(state, 100);
}

BENCHMARK(BM_UnorderedMap_Erase_100);

void
BM_UnorderedMap_Erase_1K(benchmark::State & state)
{
    run_unordered_erase<SmallValue>(state, 1000);
}

BENCHMARK(BM_UnorderedMap_Erase_1K);

void
BM_UnorderedMap_Erase_4K(benchmark::State & state)
{
    run_unordered_erase<SmallValue>(state, 4096);
}

BENCHMARK(BM_UnorderedMap_Erase_4K);

void
BM_UnorderedMap_Erase_32K(benchmark::State & state)
{
    run_unordered_erase<SmallValue>(state, 32768);
}

BENCHMARK(BM_UnorderedMap_Erase_32K);

void
BM_UnorderedMap_Erase_262K(benchmark::State & state)
{
    run_unordered_erase<SmallValue>(state, 262144);
}

BENCHMARK(BM_UnorderedMap_Erase_262K);

void
BM_UnorderedMap_Erase_1M(benchmark::State & state)
{
    run_unordered_erase<SmallValue>(state, 1048576);
}

BENCHMARK(BM_UnorderedMap_Erase_1M);

// ============================================================================
// ITERATION BENCHMARKS
// ============================================================================

template <typename SlotMapT>
void
run_iterate(benchmark::State & state, std::size_t n)
{
    reset_rng();
    SlotMapT sm;
    populate_slotmap(sm, n);

    std::uint64_t sum = 0;
    for (auto _ : state) {
        sm.for_each([&](auto const & v) { sum += v.data; });
    }

    benchmark::DoNotOptimize(sum);
    set_items_processed(
        state,
        state.iterations() * static_cast<std::int64_t>(n));
}

// Register ITERATE benchmarks for all sizes and configs
// clang-format off
BENCH_SLOTMAP_ALL_CONFIGS(Iterate, run_iterate, SmallKey100, 100, 100);
BENCH_SLOTMAP_ALL_CONFIGS(Iterate, run_iterate, SmallKey1K, 1K, 1000);
BENCH_SLOTMAP_ALL_CONFIGS(Iterate, run_iterate, SmallKey4K, 4K, 4096);
BENCH_SLOTMAP_ALL_CONFIGS(Iterate, run_iterate, SmallKey32K, 32K, 32768);
BENCH_SLOTMAP_ALL_CONFIGS(Iterate, run_iterate, SmallKey262K, 262K, 262144);
BENCH_SLOTMAP_ALL_CONFIGS(Iterate, run_iterate, SmallKey1M, 1M, 1048576);
BENCH_SLOTMAP_32_32(Iterate, run_iterate, 1M, 1048576);

// clang-format on

// unordered_map baseline
template <typename ValueT>
void
run_unordered_iterate(benchmark::State & state, std::size_t n)
{
    reset_rng();
    std::unordered_map<std::uint64_t, ValueT> um;
    populate_unordered_map<ValueT>(um, n);

    std::uint64_t sum = 0;
    for (auto _ : state) {
        for (auto const & [key, value] : um) {
            sum += value.data;
        }
    }

    benchmark::DoNotOptimize(sum);
    set_items_processed(
        state,
        state.iterations() * static_cast<std::int64_t>(n));
}

void
BM_UnorderedMap_Iterate_100(benchmark::State & state)
{
    run_unordered_iterate<SmallValue>(state, 100);
}

BENCHMARK(BM_UnorderedMap_Iterate_100);

void
BM_UnorderedMap_Iterate_1K(benchmark::State & state)
{
    run_unordered_iterate<SmallValue>(state, 1000);
}

BENCHMARK(BM_UnorderedMap_Iterate_1K);

void
BM_UnorderedMap_Iterate_4K(benchmark::State & state)
{
    run_unordered_iterate<SmallValue>(state, 4096);
}

BENCHMARK(BM_UnorderedMap_Iterate_4K);

void
BM_UnorderedMap_Iterate_32K(benchmark::State & state)
{
    run_unordered_iterate<SmallValue>(state, 32768);
}

BENCHMARK(BM_UnorderedMap_Iterate_32K);

void
BM_UnorderedMap_Iterate_262K(benchmark::State & state)
{
    run_unordered_iterate<SmallValue>(state, 262144);
}

BENCHMARK(BM_UnorderedMap_Iterate_262K);

void
BM_UnorderedMap_Iterate_1M(benchmark::State & state)
{
    run_unordered_iterate<SmallValue>(state, 1048576);
}

BENCHMARK(BM_UnorderedMap_Iterate_1M);

// ============================================================================
// LARGE VALUE BENCHMARKS (128 bytes)
// ============================================================================

// Register LargeValue INSERT benchmarks for all configs
// clang-format off
BENCH_SLOTMAP_LARGE_ALL_CONFIGS(Insert, run_insert_benchmark, LargeKey1M, 1M, 1048576);

// clang-format on

void
BM_UnorderedMap_Insert_LargeValue_1M(benchmark::State & state)
{
    run_unordered_insert<LargeValue>(state, 1048576);
    set_bytes_processed(
        state,
        state.iterations() *
            static_cast<std::int64_t>(1048576 * sizeof(LargeValue)));
}

BENCHMARK(BM_UnorderedMap_Insert_LargeValue_1M);

// LargeValue iteration needs custom handling for bytes processed
template <typename SlotMapT>
void
run_iterate_large(benchmark::State & state, std::size_t n)
{
    reset_rng();
    SlotMapT sm;
    populate_slotmap(sm, n);

    std::uint64_t sum = 0;
    for (auto _ : state) {
        sm.for_each([&](auto const & v) {
            sum += v.id;
            sum += v.data[7];
        });
    }

    benchmark::DoNotOptimize(sum);
    set_items_processed(
        state,
        state.iterations() * static_cast<std::int64_t>(n));
    set_bytes_processed(
        state,
        state.iterations() * static_cast<std::int64_t>(n * sizeof(LargeValue)));
}

// Register LargeValue ITERATE benchmarks for all configs
// clang-format off
BENCH_SLOTMAP_LARGE_ALL_CONFIGS(Iterate, run_iterate_large, LargeKey1M, 1M, 1048576);

// clang-format on

void
BM_UnorderedMap_Iterate_LargeValue_1M(benchmark::State & state)
{
    reset_rng();
    std::unordered_map<std::uint64_t, LargeValue> um;
    populate_unordered_map<LargeValue>(um, 1048576);

    std::uint64_t sum = 0;
    for (auto _ : state) {
        for (auto const & [key, value] : um) {
            sum += value.id;
            sum += value.data[7];
        }
    }

    benchmark::DoNotOptimize(sum);
    set_items_processed(state, state.iterations() * 1048576);
    set_bytes_processed(
        state,
        state.iterations() *
            static_cast<std::int64_t>(1048576 * sizeof(LargeValue)));
}

BENCHMARK(BM_UnorderedMap_Iterate_LargeValue_1M);

} // anonymous namespace
