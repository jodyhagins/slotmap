// ----------------------------------------------------------------------
// Copyright 2025 Jody Hagins
// Distributed under the MIT Software License
// See accompanying file LICENSE or copy at
// https://opensource.org/licenses/MIT
// ----------------------------------------------------------------------

#include <wjh/slotmap/Key.hpp>

#include "benchmarking/benchmark.hpp"

#include <vector>

namespace {

struct Entity
{ };

using EntityKey = wjh::slotmap::Key<Entity, 20, 12>;

template <typename KeyT>
KeyT
make_key(auto index, auto version)
{
    using IndexT = typename KeyT::index_type;
    using VersionT = typename KeyT::version_type;
    using UserT = typename KeyT::user_type;
    using I = typename IndexT::value_type;
    using V = typename VersionT::value_type;
    using U = typename UserT::value_type;
    return KeyT{IndexT{I(index)}, VersionT{V(version)}, UserT{U(0)}};
}

void
BM_KeyCreation(benchmark::State & state)
{
    for (auto _ : state) {
        auto key = make_key<EntityKey>(42, 1);
        benchmark::DoNotOptimize(key);
    }
}

BENCHMARK(BM_KeyCreation);

void
BM_KeyComparison(benchmark::State & state)
{
    auto key1 = make_key<EntityKey>(42, 1);
    auto key2 = make_key<EntityKey>(42, 1);
    for (auto _ : state) {
        bool result = key1 == key2;
        benchmark::DoNotOptimize(result);
    }
}

BENCHMARK(BM_KeyComparison);

void
BM_KeyIndexExtraction(benchmark::State & state)
{
    auto key = make_key<EntityKey>(12345, 99);
    for (auto _ : state) {
        auto index = key.index();
        benchmark::DoNotOptimize(index);
    }
}

BENCHMARK(BM_KeyIndexExtraction);

void
BM_KeyVersionExtraction(benchmark::State & state)
{
    auto key = make_key<EntityKey>(12345, 99);
    for (auto _ : state) {
        auto version = key.version();
        benchmark::DoNotOptimize(version);
    }
}

BENCHMARK(BM_KeyVersionExtraction);

} // anonymous namespace
