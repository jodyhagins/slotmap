// ----------------------------------------------------------------------
// Copyright 2025 Jody Hagins
// Distributed under the MIT Software License
// See accompanying file LICENSE or copy at
// https://opensource.org/licenses/MIT
// ----------------------------------------------------------------------

// Test that umbrella header and version header compile correctly
// with the ABI namespace macros.

#include "wjh/slotmap.hpp"
#include "wjh/slotmap/version.hpp"

#include "testing/doctest.hpp"

TEST_CASE("umbrella header provides Key")
{
    using Key = wjh::slotmap::
        Key<int, wjh::slotmap::IndexBits(16), wjh::slotmap::VersionBits(16)>;
    auto k = Key::null();
    CHECK(k.is_null());
}

TEST_CASE("umbrella header provides SlotMap")
{
    wjh::SlotMap<int, wjh::slotmap::IndexBits(8), wjh::slotmap::VersionBits(8)>
        map;
    auto key = map.emplace(42);
    CHECK(map.contains(key));
}

TEST_CASE("version header provides version constants")
{
    CHECK(wjh::slotmap::version_major >= 0);
    CHECK(wjh::slotmap::version_minor >= 0);
    CHECK(wjh::slotmap::version_patch >= 0);
    CHECK(wjh::slotmap::version_string != nullptr);
    CHECK(
        wjh::slotmap::version ==
        wjh::slotmap::version_major * 10000 +
            wjh::slotmap::version_minor * 100 + wjh::slotmap::version_patch);
}

TEST_CASE("version check macro works")
{
    CHECK(WJH_SLOTMAP_VERSION_AT_LEAST(0, 0, 0));
    CHECK(WJH_SLOTMAP_VERSION_AT_LEAST(0, 1, 0));
    CHECK_FALSE(WJH_SLOTMAP_VERSION_AT_LEAST(99, 99, 99));
}
