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
using SmallKey100 = wjh::slotmap::Key<SmallValue, 7, 25>;
using LargeKey100 = wjh::slotmap::Key<LargeValue, 7, 25>;

// For ~1000 elements: 10 index bits = 1024 max slots, 22 version bits
using SmallKey1K = wjh::slotmap::Key<SmallValue, 10, 22>;
using LargeKey1K = wjh::slotmap::Key<LargeValue, 10, 22>;

// For ~4096 elements: 13 index bits = 8192 max slots, 19 version bits
using SmallKey4K = wjh::slotmap::Key<SmallValue, 13, 19>;
using LargeKey4K = wjh::slotmap::Key<LargeValue, 13, 19>;

// For ~32K elements: 16 index bits = 65536 max slots, 16 version bits
using SmallKey32K = wjh::slotmap::Key<SmallValue, 15, 17>;
using LargeKey32K = wjh::slotmap::Key<LargeValue, 15, 17>;

// For ~262K elements: 18 index bits = 262144 max slots, 14 version bits
// Using 64-bit key to fit 18+14=32... wait, that's 32. Let's use 32-bit.
// Actually 18+14=32, so we can use 32-bit key.
using SmallKey262K = wjh::slotmap::Key<SmallValue, 18, 14>;
using LargeKey262K = wjh::slotmap::Key<LargeValue, 18, 14>;

// For ~1M elements: 20 index bits = 1048576 max slots, 12 version bits
using SmallKey1M = wjh::slotmap::Key<SmallValue, 20, 12>;
using LargeKey1M = wjh::slotmap::Key<LargeValue, 20, 12>;

// Standard 32/32 configuration for comparison (64-bit key)
using SmallKey32_32 = wjh::slotmap::Key<SmallValue, 32, 32>;
using LargeKey32_32 = wjh::slotmap::Key<LargeValue, 32, 32>;

// SlotMap type aliases
using SmallSlotMap100 = wjh::SlotMap<SmallKey100>;
using SmallSlotMap1K = wjh::SlotMap<SmallKey1K>;
using SmallSlotMap4K = wjh::SlotMap<SmallKey4K>;
using SmallSlotMap32K = wjh::SlotMap<SmallKey32K>;
using SmallSlotMap262K = wjh::SlotMap<SmallKey262K>;
using SmallSlotMap1M = wjh::SlotMap<SmallKey1M>;
using SmallSlotMap32_32 = wjh::SlotMap<SmallKey32_32>;

using LargeSlotMap100 = wjh::SlotMap<LargeKey100>;
using LargeSlotMap1K = wjh::SlotMap<LargeKey1K>;
using LargeSlotMap4K = wjh::SlotMap<LargeKey4K>;
using LargeSlotMap32K = wjh::SlotMap<LargeKey32K>;
using LargeSlotMap262K = wjh::SlotMap<LargeKey262K>;
using LargeSlotMap1M = wjh::SlotMap<LargeKey1M>;
using LargeSlotMap32_32 = wjh::SlotMap<LargeKey32_32>;

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

} // namespace bench

#endif // WJH_SLOTMAP_BENCH_COMMON_A3E8F92D4B6C47E1
