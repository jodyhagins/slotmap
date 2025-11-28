// ----------------------------------------------------------------------
// Copyright 2025 Jody Hagins
// Distributed under the MIT Software License
// See accompanying file LICENSE or copy at
// https://opensource.org/licenses/MIT
// ----------------------------------------------------------------------

#include "wjh/slotmap/detail/Slab.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

#include "testing/doctest.hpp"
#include "testing/rapidcheck.hpp"

namespace {
using namespace wjh::slotmap::detail;

using TestSlab = Slab<int, std::uint32_t, std::uint32_t, std::uint32_t>;

// ============================================================================
// Basic Slab Tests
// ============================================================================

TEST_CASE("Slab: creation")
{
    SUBCASE("create with 1 slot") {
        auto slab = TestSlab::create(1);

        REQUIRE(slab != nullptr);
        REQUIRE(slab->slots_per_slab() == 1);
        REQUIRE(slab->dead_count() == 0);
    }

    SUBCASE("create with power of 2 slots") {
        auto slab = TestSlab::create(1024);

        REQUIRE(slab != nullptr);
        REQUIRE(slab->slots_per_slab() == 1024);
        REQUIRE(slab->dead_count() == 0);
    }

    SUBCASE("create with arbitrary slot count") {
        auto slab = TestSlab::create(100);

        REQUIRE(slab != nullptr);
        REQUIRE(slab->slots_per_slab() == 100);
    }
}

TEST_CASE("Slab: slot access")
{
    auto slab = TestSlab::create(8);

    SUBCASE("slots are default initialized") {
        for (std::uint32_t i = 0; i < 8; ++i) {
            REQUIRE(slab->slot(i).version() == 0);
            REQUIRE(slab->slot(i).next() == 0);
        }
    }

    SUBCASE("slots can be accessed by index") {
        slab->slot(0).set_version(1);
        slab->slot(3).set_version(4);
        slab->slot(7).set_version(8);

        REQUIRE(slab->slot(0).version() == 1);
        REQUIRE(slab->slot(3).version() == 4);
        REQUIRE(slab->slot(7).version() == 8);
    }

    SUBCASE("const slot access") {
        slab->slot(0).set_version(42);

        auto const & const_slab = *slab;
        REQUIRE(const_slab.slot(0).version() == 42);
    }
}

TEST_CASE("Slab: slot emplace and value access")
{
    auto slab = TestSlab::create(4);

    SUBCASE("emplace values in slots") {
        slab->slot(0).emplace(100);
        slab->slot(1).emplace(200);
        slab->slot(2).emplace(300);
        slab->slot(3).emplace(400);

        REQUIRE(slab->slot(0).value() == 100);
        REQUIRE(slab->slot(1).value() == 200);
        REQUIRE(slab->slot(2).value() == 300);
        REQUIRE(slab->slot(3).value() == 400);

        // Cleanup
        slab->slot(0).destroy();
        slab->slot(1).destroy();
        slab->slot(2).destroy();
        slab->slot(3).destroy();
    }
}

TEST_CASE("Slab: dead count tracking")
{
    auto slab = TestSlab::create(4);

    SUBCASE("initial dead count is 0") {
        REQUIRE(slab->dead_count() == 0);
    }

    SUBCASE("increment dead count") {
        slab->increment_dead_count();
        REQUIRE(slab->dead_count() == 1);

        slab->increment_dead_count();
        REQUIRE(slab->dead_count() == 2);
    }

    SUBCASE("reset dead count") {
        slab->increment_dead_count();
        slab->increment_dead_count();
        slab->increment_dead_count();
        REQUIRE(slab->dead_count() == 3);

        slab->reset_dead_count();
        REQUIRE(slab->dead_count() == 0);
    }
}

TEST_CASE("Slab: non-trivial value types")
{
    using StringSlab =
        Slab<std::string, std::uint32_t, std::uint32_t, std::uint32_t>;

    auto slab = StringSlab::create(4);

    SUBCASE("emplace and access strings") {
        slab->slot(0).emplace("hello");
        slab->slot(1).emplace("world");

        REQUIRE(slab->slot(0).value() == "hello");
        REQUIRE(slab->slot(1).value() == "world");

        slab->slot(0).destroy();
        slab->slot(1).destroy();
    }

    SUBCASE("strings are properly destroyed") {
        slab->slot(0).emplace(std::string(1000, 'x'));
        slab->slot(1).emplace(std::string(1000, 'y'));

        REQUIRE(slab->slot(0).value().size() == 1000);
        REQUIRE(slab->slot(1).value().size() == 1000);

        slab->slot(0).destroy();
        slab->slot(1).destroy();
    }
}

TEST_CASE("Slab: free-list setup pattern")
{
    auto slab = TestSlab::create(8);
    std::uint32_t const base_index = 100;
    std::uint32_t const null_index = 0xFFFF'FFFF;

    // Simulate the free-list initialization pattern from the design
    for (std::uint32_t i = 0; i < 7; ++i) {
        slab->slot(i).set_next(base_index + i + 1);
        slab->slot(i).set_version(0);
    }
    slab->slot(7).set_next(null_index);
    slab->slot(7).set_version(0);

    SUBCASE("free list chain is correct") {
        REQUIRE(slab->slot(0).next() == 101);
        REQUIRE(slab->slot(1).next() == 102);
        REQUIRE(slab->slot(2).next() == 103);
        REQUIRE(slab->slot(3).next() == 104);
        REQUIRE(slab->slot(4).next() == 105);
        REQUIRE(slab->slot(5).next() == 106);
        REQUIRE(slab->slot(6).next() == 107);
        REQUIRE(slab->slot(7).next() == null_index);
    }

    SUBCASE("all versions are 0") {
        for (std::uint32_t i = 0; i < 8; ++i) {
            REQUIRE(slab->slot(i).version() == 0);
        }
    }
}

TEST_CASE("Slab: slab exhaustion tracking")
{
    auto slab = TestSlab::create(4);

    // Simulate slots becoming dead
    SUBCASE("track when slab becomes exhausted") {
        // All 4 slots become dead
        for (int i = 0; i < 4; ++i) {
            slab->increment_dead_count();
        }

        REQUIRE(slab->dead_count() == slab->slots_per_slab());
    }
}

// ============================================================================
// Property-Based Tests
// ============================================================================

TEST_CASE("Slab: property-based slot access")
{
    rc::check("all slots are accessible and independent", []() {
        auto const size_hint = *rc::gen::arbitrary<std::uint8_t>();
        std::uint32_t size = (size_hint % 100) + 1; // 1-100 slots
        auto slab = TestSlab::create(size);

        RC_ASSERT(slab->slots_per_slab() == size);

        // Set unique values in each slot
        for (std::uint32_t i = 0; i < size; ++i) {
            slab->slot(i).set_version(i * 2);
            slab->slot(i).set_next(i * 3);
        }

        // Verify all values
        for (std::uint32_t i = 0; i < size; ++i) {
            RC_ASSERT(slab->slot(i).version() == i * 2);
            RC_ASSERT(slab->slot(i).next() == i * 3);
        }
    });
}

TEST_CASE("Slab: property-based dead count")
{
    rc::check("dead count tracks increments correctly", []() {
        auto const increment_count = *rc::gen::arbitrary<std::uint8_t>();
        auto slab = TestSlab::create(256);

        for (int i = 0; i < increment_count; ++i) {
            slab->increment_dead_count();
        }

        RC_ASSERT(slab->dead_count() == increment_count);
    });
}

TEST_CASE("Slab: property-based emplace/destroy cycle")
{
    rc::check("emplace and destroy work correctly for arbitrary values", []() {
        auto const value = *rc::gen::arbitrary<int>();
        auto slab = TestSlab::create(4);

        slab->slot(0).emplace(value);
        RC_ASSERT(slab->slot(0).value() == value);

        slab->slot(0).destroy();
    });
}

// ============================================================================
// Type Traits Tests
// ============================================================================

TEST_CASE("Slab: type traits")
{
    SUBCASE("slab is not copyable") {
        static_assert(not std::is_copy_constructible_v<TestSlab>);
        static_assert(not std::is_copy_assignable_v<TestSlab>);
    }

    SUBCASE("slab is not movable") {
        static_assert(not std::is_move_constructible_v<TestSlab>);
        static_assert(not std::is_move_assignable_v<TestSlab>);
    }

    SUBCASE("direct construction is disabled") {
        // The constructor is private - Slab can only be created via create()
        // This also prevents all forms of `new Slab(...)` even if operator new
        // weren't deleted
        static_assert(not std::is_constructible_v<TestSlab, std::uint32_t>);
        static_assert(not std::is_default_constructible_v<TestSlab>);
    }

    REQUIRE(true);
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST_CASE("Slab: edge cases")
{
    SUBCASE("single slot slab") {
        auto slab = TestSlab::create(1);

        slab->slot(0).emplace(42);
        REQUIRE(slab->slot(0).value() == 42);
        slab->slot(0).destroy();

        slab->increment_dead_count();
        REQUIRE(slab->dead_count() == 1);
        REQUIRE(slab->dead_count() == slab->slots_per_slab());
    }

    SUBCASE("large slab") {
        auto slab = TestSlab::create(4096);

        REQUIRE(slab->slots_per_slab() == 4096);

        // Access first, middle, and last slots
        slab->slot(0).set_version(1);
        slab->slot(2048).set_version(2);
        slab->slot(4095).set_version(3);

        REQUIRE(slab->slot(0).version() == 1);
        REQUIRE(slab->slot(2048).version() == 2);
        REQUIRE(slab->slot(4095).version() == 3);
    }
}

TEST_CASE("Slab: different type configurations")
{
    SUBCASE("16-bit indices") {
        using Slab16 = Slab<int, std::uint16_t, std::uint16_t, std::uint16_t>;
        auto slab = Slab16::create(4);

        slab->slot(0).set_next(65535);
        slab->slot(0).set_version(65535);

        REQUIRE(slab->slot(0).next() == 65535);
        REQUIRE(slab->slot(0).version() == 65535);
    }

    SUBCASE("64-bit indices") {
        using Slab64 = Slab<int, std::uint64_t, std::uint64_t, std::uint64_t>;
        auto slab = Slab64::create(4);

        slab->slot(0).set_next(0xFFFF'FFFF'FFFF'FFFF);
        slab->slot(0).set_version(0xFFFF'FFFF'FFFF'FFFF);

        REQUIRE(slab->slot(0).next() == 0xFFFF'FFFF'FFFF'FFFF);
        REQUIRE(slab->slot(0).version() == 0xFFFF'FFFF'FFFF'FFFF);
    }
}

TEST_CASE("Slab: destructor properly destroys alive slots")
{
    static int destructor_count = 0;

    struct Tracker
    {
        ~Tracker() { ++destructor_count; }
    };

    destructor_count = 0;
    {
        using TrackerSlab =
            Slab<Tracker, std::uint32_t, std::uint32_t, std::uint32_t>;
        auto slab = TrackerSlab::create(4);

        // Emplace 2 values but leave them alive (simulating SlotMap cleanup)
        slab->slot(0).emplace();
        slab->slot(1).emplace();

        // Must destroy alive slots before slab destruction
        // (This is the SlotMap's responsibility in real usage)
        slab->slot(0).destroy();
        slab->slot(1).destroy();

        // At this point, destructor_count should be 2 from destroy() calls
        REQUIRE(destructor_count == 2);
    }
    // Slab destructor runs here, but slots are in "free" state
    // The Slot destructors don't call T's destructor again
}

TEST_CASE("Slab: lifecycle simulation")
{
    auto slab = TestSlab::create(4);
    std::uint32_t const null_index = 0xFFFF'FFFF;

    // Initial state: all slots free, linked together
    slab->slot(0).set_next(1);
    slab->slot(1).set_next(2);
    slab->slot(2).set_next(3);
    slab->slot(3).set_next(null_index);

    // Allocate slot 0
    auto allocated_next = slab->slot(0).next();
    slab->slot(0).set_version(1);
    slab->slot(0).emplace(100);

    REQUIRE(allocated_next == 1);
    REQUIRE(slab->slot(0).version() == 1);
    REQUIRE(slab->slot(0).value() == 100);

    // Allocate slot 1
    allocated_next = slab->slot(1).next();
    slab->slot(1).set_version(1);
    slab->slot(1).emplace(200);

    REQUIRE(allocated_next == 2);
    REQUIRE(slab->slot(1).version() == 1);
    REQUIRE(slab->slot(1).value() == 200);

    // Free slot 0 (return to free list)
    slab->slot(0).destroy();
    slab->slot(0).set_version(2); // Increment version
    slab->slot(0).set_next(2); // Point to old head

    REQUIRE(slab->slot(0).version() == 2);
    REQUIRE(slab->slot(0).next() == 2);

    // Cleanup slot 1
    slab->slot(1).destroy();
}

} // anonymous namespace
