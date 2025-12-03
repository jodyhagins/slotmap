// ----------------------------------------------------------------------
// Copyright 2025 Jody Hagins
// Distributed under the MIT Software License
// See accompanying file LICENSE or copy at
// https://opensource.org/licenses/MIT
// ----------------------------------------------------------------------
#ifndef WJH_SLOTMAP_BENCH_COMMON_A3E8F92D4B6C47E1
#define WJH_SLOTMAP_BENCH_COMMON_A3E8F92D4B6C47E1

#include <wjh/slotmap/SlotMap.hpp>

#include "benchmarking/benchmark.hpp"

#include <algorithm>
#include <cstdint>
#include <random>
#include <unordered_map>
#include <vector>

namespace bench {

using namespace wjh::slotmap_literals;

// ============================================================================
// Test Value Types
// ============================================================================

/// Simple POD for lightweight benchmarks (16 bytes)
struct SmallValue
{
    std::uint64_t id;
    std::uint64_t data;
};

/// Larger struct to test cache behavior (128 bytes)
struct LargeValue
{
    std::uint64_t id;
    std::array<std::uint64_t, 15> data;
};

// ============================================================================
// Sized Key Types
//
// Key bit totals must be 16, 32, 64, or 128. We size index bits to the
// expected maximum concurrent element count, with remaining bits for version.
//
// Smaller index bits don't limit total objects created over time—only
// concurrent live objects. Version bits handle object reuse.
// ============================================================================

// For ~100 elements: 8 index bits = 256 max slots, 24 version bits
using SmallKey100 = wjh::slotmap::Key<SmallValue, 7_ib, 25_vb>;
using LargeKey100 = wjh::slotmap::Key<LargeValue, 7_ib, 25_vb>;

// For ~1000 elements: 10 index bits = 1024 max slots, 22 version bits
using SmallKey1K = wjh::slotmap::Key<SmallValue, 10_ib, 22_vb>;
using LargeKey1K = wjh::slotmap::Key<LargeValue, 10_ib, 22_vb>;

// For ~4096 elements: 13 index bits = 8192 max slots, 19 version bits
using SmallKey4K = wjh::slotmap::Key<SmallValue, 13_ib, 19_vb>;
using LargeKey4K = wjh::slotmap::Key<LargeValue, 13_ib, 19_vb>;

// For ~32K elements: 16 index bits = 65536 max slots, 16 version bits
using SmallKey32K = wjh::slotmap::Key<SmallValue, 15_ib, 17_vb>;
using LargeKey32K = wjh::slotmap::Key<LargeValue, 15_ib, 17_vb>;

// For ~262K elements: 18 index bits = 262144 max slots, 14 version bits
// Using 64-bit key to fit 18+14=32... wait, that's 32. Let's use 32-bit.
// Actually 18+14=32, so we can use 32-bit key.
using SmallKey262K = wjh::slotmap::Key<SmallValue, 18_ib, 14_vb>;
using LargeKey262K = wjh::slotmap::Key<LargeValue, 18_ib, 14_vb>;

// For ~1M elements: 20 index bits = 1048576 max slots, 12 version bits
using SmallKey1M = wjh::slotmap::Key<SmallValue, 20_ib, 12_vb>;
using LargeKey1M = wjh::slotmap::Key<LargeValue, 20_ib, 12_vb>;

// Standard 32/32 configuration for comparison (64-bit key)
using SmallKey32_32 = wjh::slotmap::Key<SmallValue, 32_ib, 32_vb>;
using LargeKey32_32 = wjh::slotmap::Key<LargeValue, 32_ib, 32_vb>;

// ============================================================================
// Configuration Aliases
// ============================================================================

// Shorthand aliases for configuration enums
using SPS = wjh::slotmap::SlotsPerSlab;
using UAB = wjh::slotmap::UseAliveBitForLookup;

// Generic configurable SlotMap template
template <typename KeyT, SPS sps, UAB alive>
using ConfiguredSlotMap = wjh::SlotMap<wjh::slotmap::Traits<KeyT, sps, alive>>;

// ============================================================================
// 4 Configuration-Specific SlotMap Templates
// ============================================================================

// All slots in single slab, alive bit enabled (fastest lookups)
template <typename KeyT>
using SlotMap_All_Alive = ConfiguredSlotMap<KeyT, SPS::All, UAB::Yes>;

// All slots in single slab, alive bit disabled
template <typename KeyT>
using SlotMap_All_NoAlive = ConfiguredSlotMap<KeyT, SPS::All, UAB::No>;

// Dynamic multi-slab (4096 slots/slab), alive bit enabled
template <typename KeyT>
using SlotMap_Dyn_Alive = ConfiguredSlotMap<KeyT, SPS::Dynamic, UAB::Yes>;

// Dynamic multi-slab (4096 slots/slab), alive bit disabled
template <typename KeyT>
using SlotMap_Dyn_NoAlive = ConfiguredSlotMap<KeyT, SPS::Dynamic, UAB::No>;

// Legacy aliases for backward compatibility
template <typename KeyT>
using DynSlotMap = SlotMap_Dyn_Alive<KeyT>;
template <typename KeyT>
using AllSlotMap = SlotMap_All_Alive<KeyT>;

// SlotMap type aliases
// For small index spaces (32-bit keys with <= ~64K slots), use
// SlotsPerSlab::All for single-slab storage optimization (eliminates vector
// indirection)
using SmallSlotMap100 = AllSlotMap<SmallKey100>;
using SmallSlotMap1K = AllSlotMap<SmallKey1K>;
using SmallSlotMap4K = AllSlotMap<SmallKey4K>;
using SmallSlotMap32K = AllSlotMap<SmallKey32K>;
using SmallSlotMap262K = AllSlotMap<SmallKey262K>;
using SmallSlotMap1M = AllSlotMap<SmallKey1M>;

// For 64-bit keys (large index space), use default multi-slab storage
using SmallSlotMap32_32 = DynSlotMap<SmallKey32_32>;

using LargeSlotMap100 = AllSlotMap<LargeKey100>;
using LargeSlotMap1K = AllSlotMap<LargeKey1K>;
using LargeSlotMap4K = AllSlotMap<LargeKey4K>;
using LargeSlotMap32K = AllSlotMap<LargeKey32K>;
using LargeSlotMap262K = AllSlotMap<LargeKey262K>;
using LargeSlotMap1M = AllSlotMap<LargeKey1M>;

// For 64-bit keys (large index space), use default multi-slab storage
using LargeSlotMap32_32 = DynSlotMap<LargeKey32_32>;

// ============================================================================
// Random Number Generation
// ============================================================================

inline std::mt19937_64 &
get_rng()
{
    static std::mt19937_64 rng{42}; // Fixed seed for reproducibility
    return rng;
}

inline void
reset_rng()
{
    get_rng().seed(42);
}

/// Generate a Zipfian distributed index (80/20 hot/cold pattern)
/// @param n Upper bound (exclusive)
/// @param skew Zipfian skew parameter (1.0 = standard Zipf, higher = more skew)
inline std::size_t
zipfian_index(std::size_t n, double skew = 1.0)
{
    static std::uniform_real_distribution<double> dist(0.0, 1.0);
    double u = dist(get_rng());
    // Approximation: power-law distribution
    return static_cast<std::size_t>(n * std::pow(u, 1.0 / (1.0 + skew))) % n;
}

// ============================================================================
// Container Setup Helpers
// ============================================================================

/// Populate a SlotMap with n elements, returning the keys
template <typename SlotMapT>
std::vector<typename SlotMapT::key_type>
populate_slotmap(SlotMapT & sm, std::size_t n)
{
    using value_type = typename SlotMapT::mapped_type;
    std::vector<typename SlotMapT::key_type> keys;
    keys.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        value_type v{};
        v.id = i;
        keys.push_back(sm.emplace(std::move(v)));
    }
    return keys;
}

/// Populate an unordered_map with n elements, returning the keys
template <typename ValueT>
std::vector<std::uint64_t>
populate_unordered_map(
    std::unordered_map<std::uint64_t, ValueT> & um,
    std::size_t n)
{
    std::vector<std::uint64_t> keys;
    keys.reserve(n);
    um.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        ValueT v{};
        v.id = i;
        um.emplace(i, std::move(v));
        keys.push_back(i);
    }
    return keys;
}

/// Shuffle a vector in-place
template <typename T>
void
shuffle(std::vector<T> & v)
{
    std::shuffle(v.begin(), v.end(), get_rng());
}

/// Create a shuffled copy of indices [0, n)
inline std::vector<std::size_t>
shuffled_indices(std::size_t n)
{
    std::vector<std::size_t> indices(n);
    std::iota(indices.begin(), indices.end(), 0);
    shuffle(indices);
    return indices;
}

/// Create Zipfian-distributed access pattern
inline std::vector<std::size_t>
zipfian_indices(std::size_t n, std::size_t count, double skew = 1.0)
{
    std::vector<std::size_t> indices;
    indices.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        indices.push_back(zipfian_index(n, skew));
    }
    return indices;
}

// ============================================================================
// Benchmark Counters
// ============================================================================

inline void
set_items_processed(benchmark::State & state, std::int64_t items)
{
    state.SetItemsProcessed(items);
}

inline void
set_bytes_processed(benchmark::State & state, std::int64_t bytes)
{
    state.SetBytesProcessed(bytes);
}

// ============================================================================
// Benchmark Registration Macros
//
// These macros generate benchmark functions for all 4 configuration
// combinations. Use them to avoid repeating benchmark logic 4x.
//
// Usage:
//   // First, define a templated run function:
//   template <typename SlotMapT>
//   void run_insert(benchmark::State& state, std::size_t n) { ... }
//
//   // Then register for all configs at each size:
//   BENCH_SLOTMAP_ALL_CONFIGS(Insert, run_insert, SmallKey100, 100, 100)
//   BENCH_SLOTMAP_ALL_CONFIGS(Insert, run_insert, SmallKey1K, 1K, 1000)
//
//   // For 32/32 keys (Dynamic only - can't use SlotsPerSlab::All):
//   BENCH_SLOTMAP_32_32(Insert, run_insert, 1M, 1048576)
// ============================================================================

// Register benchmark for all 4 configs at a given size
// Args: OpName, RunFunc, KeyType, SizeName, SizeVal
#define BENCH_SLOTMAP_ALL_CONFIGS(OpName, RunFunc, KeyType, SizeName, SizeVal) \
    void BM_SlotMap_##OpName##_##SizeName##_All_Alive( \
        benchmark::State & state) \
    { \
        RunFunc<SlotMap_All_Alive<KeyType>>(state, SizeVal); \
    } \
    BENCHMARK(BM_SlotMap_##OpName##_##SizeName##_All_Alive); \
\
    void BM_SlotMap_##OpName##_##SizeName##_All_NoAlive( \
        benchmark::State & state) \
    { \
        RunFunc<SlotMap_All_NoAlive<KeyType>>(state, SizeVal); \
    } \
    BENCHMARK(BM_SlotMap_##OpName##_##SizeName##_All_NoAlive); \
\
    void BM_SlotMap_##OpName##_##SizeName##_Dyn_Alive( \
        benchmark::State & state) \
    { \
        RunFunc<SlotMap_Dyn_Alive<KeyType>>(state, SizeVal); \
    } \
    BENCHMARK(BM_SlotMap_##OpName##_##SizeName##_Dyn_Alive); \
\
    void BM_SlotMap_##OpName##_##SizeName##_Dyn_NoAlive( \
        benchmark::State & state) \
    { \
        RunFunc<SlotMap_Dyn_NoAlive<KeyType>>(state, SizeVal); \
    } \
    BENCHMARK(BM_SlotMap_##OpName##_##SizeName##_Dyn_NoAlive)

// Register benchmark for 32/32 keys (Dynamic only - All not supported)
// Args: OpName, RunFunc, SizeName, SizeVal
#define BENCH_SLOTMAP_32_32(OpName, RunFunc, SizeName, SizeVal) \
    void BM_SlotMap_##OpName##_##SizeName##_32_32_Alive( \
        benchmark::State & state) \
    { \
        RunFunc<SlotMap_Dyn_Alive<SmallKey32_32>>(state, SizeVal); \
    } \
    BENCHMARK(BM_SlotMap_##OpName##_##SizeName##_32_32_Alive); \
\
    void BM_SlotMap_##OpName##_##SizeName##_32_32_NoAlive( \
        benchmark::State & state) \
    { \
        RunFunc<SlotMap_Dyn_NoAlive<SmallKey32_32>>(state, SizeVal); \
    } \
    BENCHMARK(BM_SlotMap_##OpName##_##SizeName##_32_32_NoAlive)

// Register benchmark for LargeValue with all 4 configs
// Args: OpName, RunFunc, KeyType, SizeName, SizeVal
#define BENCH_SLOTMAP_LARGE_ALL_CONFIGS( \
    OpName, \
    RunFunc, \
    KeyType, \
    SizeName, \
    SizeVal) \
    void BM_SlotMap_##OpName##_Large_##SizeName##_All_Alive( \
        benchmark::State & state) \
    { \
        RunFunc<SlotMap_All_Alive<KeyType>>(state, SizeVal); \
    } \
    BENCHMARK(BM_SlotMap_##OpName##_Large_##SizeName##_All_Alive); \
\
    void BM_SlotMap_##OpName##_Large_##SizeName##_All_NoAlive( \
        benchmark::State & state) \
    { \
        RunFunc<SlotMap_All_NoAlive<KeyType>>(state, SizeVal); \
    } \
    BENCHMARK(BM_SlotMap_##OpName##_Large_##SizeName##_All_NoAlive); \
\
    void BM_SlotMap_##OpName##_Large_##SizeName##_Dyn_Alive( \
        benchmark::State & state) \
    { \
        RunFunc<SlotMap_Dyn_Alive<KeyType>>(state, SizeVal); \
    } \
    BENCHMARK(BM_SlotMap_##OpName##_Large_##SizeName##_Dyn_Alive); \
\
    void BM_SlotMap_##OpName##_Large_##SizeName##_Dyn_NoAlive( \
        benchmark::State & state) \
    { \
        RunFunc<SlotMap_Dyn_NoAlive<KeyType>>(state, SizeVal); \
    } \
    BENCHMARK(BM_SlotMap_##OpName##_Large_##SizeName##_Dyn_NoAlive)

} // namespace bench

#endif // WJH_SLOTMAP_BENCH_COMMON_A3E8F92D4B6C47E1
