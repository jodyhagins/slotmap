// ----------------------------------------------------------------------
// Copyright 2025 Jody Hagins
// Distributed under the MIT Software License
// See accompanying file LICENSE or copy at
// https://opensource.org/licenses/MIT
// ----------------------------------------------------------------------

#include "wjh/slotmap/detail/Slab.hpp"

#include "wjh/slotmap/detail.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

#include "testing/doctest.hpp"
#include "testing/rapidcheck.hpp"

namespace {
using namespace wjh::slotmap::detail;

// Common test configurations
#define DefineStrongType(name, nbits) \
    class name \
    : public wjh::slotmap::detail::TypeBase<nbits, name> \
    { \
        using Base = wjh::slotmap::detail::TypeBase<nbits, name>; \
\
    public: \
        using Base::Base; \
        using value_type = typename Base::value_type; \
        constexpr name(auto v) \
        requires requires { value_type(v); } \
        : Base(value_type(v)) \
        { } \
    }

DefineStrongType(Version4, 4);

DefineStrongType(Index8, 8);

DefineStrongType(Index16, 16);
DefineStrongType(Version16, 16);

DefineStrongType(Index32, 32);
DefineStrongType(Version32, 32);
DefineStrongType(Size32, 32);

DefineStrongType(Index64, 64);
DefineStrongType(Version64, 64);
DefineStrongType(Size64, 64);

using TestSlab = Slab<int, Index32, Version32, Size64>;

// 2-bit version for testing version exhaustion
DefineStrongType(Version2, 2);
using SmallVersionSlab = Slab<int, Index32, Version2, Size64>;

// ============================================================================
// Basic Slab Tests
// ============================================================================

TEST_CASE("Slab: creation")
{
    SUBCASE("create with 1 slot") {
        auto slab = TestSlab::create(1u);

        REQUIRE(slab != nullptr);
        REQUIRE(slab->slots_per_slab() == 1);
        REQUIRE(slab->dead_count() == 0);
    }

    SUBCASE("create with power of 2 slots") {
        auto slab = TestSlab::create(1024u);

        REQUIRE(slab != nullptr);
        REQUIRE(slab->slots_per_slab() == 1024);
        REQUIRE(slab->dead_count() == 0);
    }

    SUBCASE("create with arbitrary slot count") {
        auto slab = TestSlab::create(100u);

        REQUIRE(slab != nullptr);
        REQUIRE(slab->slots_per_slab() == 100);
    }

    SUBCASE("all slots start not alive") {
        using Index = TestSlab::index_type;
        auto slab = TestSlab::create(8u);

        for (std::uint32_t i = 0; i < 8; ++i) {
            REQUIRE_FALSE(slab->is_alive(Index{i}));
        }
    }

    SUBCASE("all slots start with version 0") {
        using Index = TestSlab::index_type;
        auto slab = TestSlab::create(8u);

        for (std::uint32_t i = 0; i < 8; ++i) {
            REQUIRE(slab->slot(Index{i}).version() == 0u);
        }
    }
}

TEST_CASE("Slab: slot access")
{
    auto slab = TestSlab::create(8u);

    SUBCASE("slots are default initialized") {
        for (std::uint32_t i = 0; i < 8; ++i) {
            REQUIRE(slab->slot(i).version() == 0);
            REQUIRE(slab->slot(i).next() == 0);
        }
    }

    SUBCASE("slots can be accessed by index") {
        slab->slot(0).set_version(TestSlab::version_type{1});
        slab->slot(3).set_version(TestSlab::version_type{4});
        slab->slot(7).set_version(TestSlab::version_type{8});

        REQUIRE(slab->slot(0).version() == 1);
        REQUIRE(slab->slot(3).version() == 4);
        REQUIRE(slab->slot(7).version() == 8);
    }

    SUBCASE("const slot access") {
        slab->slot(0).set_version(TestSlab::version_type{42});

        auto const & const_slab = *slab;
        REQUIRE(const_slab.slot(0).version() == 42);
    }
}

// ============================================================================
// Slab emplace/destroy lifecycle tests
// ============================================================================

TEST_CASE("Slab: emplace")
{
    using Index = TestSlab::index_type;
    auto slab = TestSlab::create(4u);

    SUBCASE("emplace returns current version") {
        // Slot starts at version 0
        auto [ver, next] = slab->emplace(Index(0u), 42);
        REQUIRE(ver == 0);
        REQUIRE(slab->slot(0).value() == 42);
    }

    SUBCASE("emplace sets alive bit") {
        REQUIRE_FALSE(slab->is_alive(Index{0}));
        slab->emplace(Index{0}, 42);
        REQUIRE(slab->is_alive(Index{0}));
    }

    SUBCASE("emplace with non-zero version") {
        // Set version first (simulating slot that was used before)
        slab->slot(0).set_version(TestSlab::version_type{5});

        auto [ver, next] = slab->emplace(Index{0}, 100);
        REQUIRE(ver == 5);
        REQUIRE(slab->slot(0).value() == 100);
        REQUIRE(slab->is_alive(Index{0}));
    }

    SUBCASE("emplace multiple slots") {
        slab->emplace(Index(0), 100);
        slab->emplace(Index(1), 200);
        slab->emplace(Index(2), 300);
        slab->emplace(Index(3), 400);

        REQUIRE(slab->slot(0).value() == 100);
        REQUIRE(slab->slot(1).value() == 200);
        REQUIRE(slab->slot(2).value() == 300);
        REQUIRE(slab->slot(3).value() == 400);

        for (std::uint32_t i = 0; i < 4; ++i) {
            REQUIRE(slab->is_alive(Index{i}));
        }
    }
}

TEST_CASE("Slab: destroy")
{
    using Index = TestSlab::index_type;
    auto slab = TestSlab::create(4u);

    SUBCASE("destroy clears alive bit") {
        slab->emplace(Index{0}, 42);
        REQUIRE(slab->is_alive(Index{0}));

        slab->destroy(Index{0});
        REQUIRE_FALSE(slab->is_alive(Index{0}));
    }

    SUBCASE("destroy increments version") {
        slab->emplace(Index{0}, 42);
        REQUIRE(slab->slot(0).version() == 0);

        slab->destroy(Index{0});
        REQUIRE(slab->slot(0).version() == 1);
    }

    SUBCASE("destroy returns true when slot can be reused") {
        slab->emplace(Index{0}, 42);
        bool can_reuse = slab->destroy(Index{0});
        REQUIRE(can_reuse);
    }
}

TEST_CASE("Slab: version exhaustion with 2-bit version")
{
    using Index = SmallVersionSlab::index_type;
    auto slab = SmallVersionSlab::create(4u);

    // max_version for 2 bits is 0b11 = 3
    REQUIRE(SmallVersionSlab::max_version == 3);

    SUBCASE("slot becomes dead when version reaches max") {
        // Version 0 -> emplace -> destroy -> version 1
        slab->emplace(Index{0}, 1);
        REQUIRE(slab->destroy(Index{0}));
        REQUIRE(slab->slot(0).version() == 1);

        // Version 1 -> emplace -> destroy -> version 2
        slab->emplace(Index{0}, 2);
        REQUIRE(slab->destroy(Index{0}));
        REQUIRE(slab->slot(0).version() == 2);

        // Version 2 -> emplace -> destroy -> version 3
        slab->emplace(Index{0}, 3);
        REQUIRE(slab->destroy(Index{0}));
        REQUIRE(slab->slot(0).version() == 3);

        // Version 3 (max) -> emplace -> destroy -> slot is DEAD
        slab->emplace(Index{0}, 4);
        bool can_reuse = slab->destroy(Index{0});
        REQUIRE_FALSE(can_reuse);
        REQUIRE(slab->dead_count() == 1);
        // Version stays at max (not incremented further)
        REQUIRE(slab->slot(0).version() == 3);
    }

    SUBCASE("dead count tracks exhausted slots") {
        // Exhaust all 4 slots
        for (Index::value_type slot = 0; slot < 4; ++slot) {
            for (int use = 0; use < 4; ++use) {
                slab->emplace(Index{slot}, static_cast<int>(use));
                slab->destroy(Index{slot});
            }
        }

        REQUIRE(slab->dead_count() == 4);
        REQUIRE(slab->dead_count() == slab->slots_per_slab());
    }
}

TEST_CASE("Slab: reset_dead_count")
{
    using Index = SmallVersionSlab::index_type;
    auto slab = SmallVersionSlab::create(4u);

    // Exhaust a slot
    for (int use = 0; use < 4; ++use) {
        slab->emplace(Index{0}, use);
        slab->destroy(Index{0});
    }
    REQUIRE(slab->dead_count() == 1);
}

// ============================================================================
// Alive bitmap tests
// ============================================================================

TEST_CASE("Slab: is_alive")
{
    using Index = TestSlab::index_type;
    auto slab = TestSlab::create(16u);

    SUBCASE("initially all not alive") {
        for (std::uint32_t i = 0; i < 16; ++i) {
            REQUIRE_FALSE(slab->is_alive(Index{i}));
        }
    }

    SUBCASE("emplace makes slot alive") {
        slab->emplace(Index{5}, 42);
        REQUIRE(slab->is_alive(Index{5}));

        // Others still not alive
        for (std::uint32_t i = 0; i < 16; ++i) {
            if (i != 5) {
                REQUIRE_FALSE(slab->is_alive(Index{i}));
            }
        }
    }

    SUBCASE("destroy makes slot not alive") {
        slab->emplace(Index{5}, 42);
        slab->destroy(Index{5});
        REQUIRE_FALSE(slab->is_alive(Index{5}));
    }

    SUBCASE("bitmap handles slot indices across byte boundaries") {
        // Test slots at various bit positions
        slab->emplace(Index{0}, 0); // bit 0 of byte 0
        slab->emplace(Index{7}, 7); // bit 7 of byte 0
        slab->emplace(Index{8}, 8); // bit 0 of byte 1
        slab->emplace(Index{15}, 15); // bit 7 of byte 1

        REQUIRE(slab->is_alive(Index{0}));
        REQUIRE(slab->is_alive(Index{7}));
        REQUIRE(slab->is_alive(Index{8}));
        REQUIRE(slab->is_alive(Index{15}));

        REQUIRE_FALSE(slab->is_alive(Index{1}));
        REQUIRE_FALSE(slab->is_alive(Index{6}));
        REQUIRE_FALSE(slab->is_alive(Index{9}));
        REQUIRE_FALSE(slab->is_alive(Index{14}));
    }
}

// ============================================================================
// Destructor tests
// ============================================================================

TEST_CASE("Slab: destructor destroys alive slots")
{
    static int destructor_count = 0;

    struct Tracker
    {
        ~Tracker() { ++destructor_count; }
    };

    using Index = Index32;
    using Version = Version32;
    using Size = Size64;
    using TrackerSlab = Slab<Tracker, Index, Version, Size>;

    SUBCASE("destructor calls destroy on alive slots") {
        destructor_count = 0;
        {
            auto slab = TrackerSlab::create(4u);
            slab->emplace(Index{0});
            slab->emplace(Index{2});
            // Slots 0 and 2 are alive, 1 and 3 are not
        }
        // Slab destructor should have destroyed slots 0 and 2
        REQUIRE(destructor_count == 2);
    }

    SUBCASE("destructor handles all slots alive") {
        destructor_count = 0;
        {
            auto slab = TrackerSlab::create(4u);
            slab->emplace(Index{0});
            slab->emplace(Index{1});
            slab->emplace(Index{2});
            slab->emplace(Index{3});
        }
        REQUIRE(destructor_count == 4);
    }

    SUBCASE("destructor handles no slots alive") {
        destructor_count = 0;
        {
            auto slab = TrackerSlab::create(4u);
            // No emplace calls - all slots are free
        }
        REQUIRE(destructor_count == 0);
    }

    SUBCASE("destructor handles mixed alive and destroyed") {
        destructor_count = 0;
        {
            auto slab = TrackerSlab::create(4u);
            slab->emplace(Index{0});
            slab->emplace(Index{1});
            slab->destroy(Index{0}); // destructor_count = 1
            slab->emplace(Index{2});
            // Slot 1 and 2 are alive, 0 and 3 are not
        }
        // destroy(0) = 1, slab destructor destroys 1 and 2 = 2 more
        REQUIRE(destructor_count == 3);
    }
}

// ============================================================================
// Non-trivial value types
// ============================================================================

TEST_CASE("Slab: non-trivial value types")
{
    using Index = Index32;
    using Version = Version32;
    using Size = Size64;
    using StringSlab = Slab<std::string, Index, Version, Size>;

    auto slab = StringSlab::create(4u);

    SUBCASE("emplace and access strings") {
        slab->emplace(Index{0}, "hello");
        slab->emplace(Index{1}, "world");

        REQUIRE(slab->slot(0).value() == "hello");
        REQUIRE(slab->slot(1).value() == "world");
        REQUIRE(slab->is_alive(Index{0}));
        REQUIRE(slab->is_alive(Index{1}));
    }

    SUBCASE("strings are properly destroyed") {
        slab->emplace(Index{0}, std::string(1000, 'x'));
        slab->emplace(Index{1}, std::string(1000, 'y'));

        REQUIRE(slab->slot(0).value().size() == 1000);
        REQUIRE(slab->slot(1).value().size() == 1000);

        slab->destroy(Index{0});
        slab->destroy(Index{1});

        REQUIRE_FALSE(slab->is_alive(Index{0}));
        REQUIRE_FALSE(slab->is_alive(Index{1}));
    }
}

// ============================================================================
// Free-list setup pattern
// ============================================================================

TEST_CASE("Slab: free-list setup pattern")
{
    using Index = TestSlab::index_type;
    auto slab = TestSlab::create(8u);
    std::uint32_t const base_index = 100;
    std::uint32_t const null_index = 0xFFFF'FFFF;

    // Simulate the free-list initialization pattern from the design
    for (std::uint32_t i = 0; i < 7; ++i) {
        slab->slot(i).set_next(TestSlab::index_type{base_index + i + 1});
    }
    slab->slot(7).set_next(TestSlab::index_type{null_index});

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

    SUBCASE("none are alive") {
        for (std::uint32_t i = 0; i < 8; ++i) {
            REQUIRE_FALSE(slab->is_alive(Index{i}));
        }
    }
}

// ============================================================================
// Complete lifecycle simulation
// ============================================================================

TEST_CASE("Slab: lifecycle simulation")
{
    using Index = SmallVersionSlab::index_type;
    auto slab = SmallVersionSlab::create(4u);
    std::uint32_t const null_index = 0xFFFF'FFFF;

    // Initial state: all slots free, linked together
    slab->slot(0).set_next(SmallVersionSlab::index_type{1});
    slab->slot(1).set_next(SmallVersionSlab::index_type{2});
    slab->slot(2).set_next(SmallVersionSlab::index_type{3});
    slab->slot(3).set_next(SmallVersionSlab::index_type{null_index});

    // Allocate slot 0 (remove from free list head)
    auto free_head = slab->slot(0).next(); // = 1
    auto [ver0, next0] = slab->emplace(Index{0}, 100);

    REQUIRE(free_head == 1);
    REQUIRE(ver0 == 0);
    REQUIRE(slab->slot(0).value() == 100);
    REQUIRE(slab->is_alive(Index{0}));

    // Allocate slot 1
    free_head = slab->slot(1).next(); // = 2
    auto [ver1, next1] = slab->emplace(Index{1}, 200);

    REQUIRE(free_head == 2);
    REQUIRE(ver1 == 0);
    REQUIRE(slab->slot(1).value() == 200);
    REQUIRE(slab->is_alive(Index{1}));

    // Free slot 0 (return to free list)
    bool can_reuse = slab->destroy(Index{0});
    REQUIRE(can_reuse);
    REQUIRE_FALSE(slab->is_alive(Index{0}));
    REQUIRE(slab->slot(0).version() == 1);

    // Set slot 0 to point to old head (slot 2)
    slab->slot(0).set_next(SmallVersionSlab::index_type{free_head}); // 0 -> 2

    // Re-allocate slot 0
    free_head = slab->slot(0).next(); // = 2
    auto [ver0_reuse, next0_reuse] = slab->emplace(Index{0}, 101);

    REQUIRE(free_head == 2);
    REQUIRE(ver0_reuse == 1); // version incremented from previous use
    REQUIRE(slab->slot(0).value() == 101);
    REQUIRE(slab->is_alive(Index{0}));
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
            slab->slot(i).set_version(TestSlab::version_type{i * 2});
            slab->slot(i).set_next(TestSlab::index_type{i * 3});
        }

        // Verify all values
        for (std::uint32_t i = 0; i < size; ++i) {
            RC_ASSERT(slab->slot(i).version() == i * 2);
            RC_ASSERT(slab->slot(i).next() == i * 3);
        }
    });
}

TEST_CASE("Slab: property-based emplace/destroy cycle")
{
    using Index = TestSlab::index_type;
    rc::check("emplace and destroy work correctly for arbitrary values", []() {
        auto const value = *rc::gen::arbitrary<int>();
        auto slab = TestSlab::create(4u);

        auto ver = slab->emplace(Index{0}, value).version;
        RC_ASSERT(ver == 0);
        RC_ASSERT(slab->slot(0).value() == value);
        RC_ASSERT(slab->is_alive(Index{0}));

        bool can_reuse = slab->destroy(Index{0});
        RC_ASSERT(can_reuse);
        RC_ASSERT(not slab->is_alive(Index{0}));
    });
}

TEST_CASE("Slab: property-based alive bitmap")
{
    rc::check("alive bitmap correctly tracks emplaced slots", []() {
        using Index = TestSlab::index_type;
        auto slab = TestSlab::create(64u);

        // Generate a random set of slots to emplace
        auto const slot_mask = *rc::gen::arbitrary<std::uint64_t>();

        for (Index::value_type i = 0; i < 64; ++i) {
            if ((slot_mask >> i) & 1u) {
                slab->emplace(Index{i}, static_cast<int>(i));
            }
        }

        // Verify alive bitmap matches
        for (std::uint32_t i = 0; i < 64; ++i) {
            bool expected_alive = (slot_mask >> i) & 1u;
            RC_ASSERT(slab->is_alive(Index{i}) == expected_alive);
        }
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
        static_assert(not std::is_constructible_v<TestSlab, std::uint32_t>);
        static_assert(not std::is_default_constructible_v<TestSlab>);
    }

    SUBCASE("max_version is correct") {
        REQUIRE(TestSlab::max_version == 0xFFFF'FFFF);
        REQUIRE(SmallVersionSlab::max_version == 3);
    }

    REQUIRE(true);
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST_CASE("Slab: edge cases")
{
    SUBCASE("single slot slab") {
        using Index = SmallVersionSlab::index_type;
        auto slab = SmallVersionSlab::create(1u);

        // Use up all versions
        for (int use = 0; use < 4; ++use) {
            slab->emplace(Index{0}, use);
            slab->destroy(Index{0});
        }

        REQUIRE(slab->dead_count() == 1);
        REQUIRE(slab->dead_count() == slab->slots_per_slab());
    }

    SUBCASE("large slab") {
        using Index = TestSlab::index_type;
        auto slab = TestSlab::create(4096u);

        REQUIRE(slab->slots_per_slab() == 4096);

        // Emplace in first, middle, and last slots
        slab->emplace(Index{0}, 1);
        slab->emplace(Index{2048}, 2);
        slab->emplace(Index{4095}, 3);

        REQUIRE(slab->is_alive(Index{0}));
        REQUIRE(slab->is_alive(Index{2048}));
        REQUIRE(slab->is_alive(Index{4095}));
        REQUIRE_FALSE(slab->is_alive(Index{1}));
        REQUIRE_FALSE(slab->is_alive(Index{2047}));
        REQUIRE_FALSE(slab->is_alive(Index{4094}));
    }

    SUBCASE("bitmap boundary - 8 slots (1 byte)") {
        using Index = TestSlab::index_type;
        auto slab = TestSlab::create(8u);

        slab->emplace(Index{0}, 0);
        slab->emplace(Index{7}, 7);

        REQUIRE(slab->is_alive(Index{0}));
        REQUIRE(slab->is_alive(Index{7}));
        for (std::uint32_t i = 1; i < 7; ++i) {
            REQUIRE_FALSE(slab->is_alive(Index{i}));
        }
    }

    SUBCASE("bitmap boundary - 9 slots (2 bytes)") {
        using Index = TestSlab::index_type;
        auto slab = TestSlab::create(9u);

        slab->emplace(Index{0}, 0);
        slab->emplace(Index{7}, 7);
        slab->emplace(Index{8}, 8);

        REQUIRE(slab->is_alive(Index{0}));
        REQUIRE(slab->is_alive(Index{7}));
        REQUIRE(slab->is_alive(Index{8}));
        for (std::uint32_t i = 1; i < 7; ++i) {
            REQUIRE_FALSE(slab->is_alive(Index{i}));
        }
    }
}

// ============================================================================
// Different type configurations
// ============================================================================

TEST_CASE("Slab: different type configurations")
{
    SUBCASE("16-bit indices") {
        using Size = Size32;
        using Slab16 = Slab<int, Index16, Version16, Size>;
        auto slab = Slab16::create(4u);

        slab->slot(0).set_next(Slab16::index_type{65535});
        slab->slot(0).set_version(Slab16::version_type{65535});

        REQUIRE(slab->slot(0).next() == 65535);
        REQUIRE(slab->slot(0).version() == 65535);
        REQUIRE(Slab16::max_version == 65535);
    }

    SUBCASE("64-bit indices") {
        using Size = Size64;
        using Slab64 = Slab<int, Index64, Version64, Size>;
        auto slab = Slab64::create(4u);

        slab->slot(0).set_next(Slab64::index_type{0xFFFF'FFFF'FFFF'FFFF});
        slab->slot(0).set_version(Slab64::version_type{0xFFFF'FFFF'FFFF'FFFF});

        REQUIRE(slab->slot(0).next() == 0xFFFF'FFFF'FFFF'FFFF);
        REQUIRE(slab->slot(0).version() == 0xFFFF'FFFF'FFFF'FFFF);
    }

    SUBCASE("mixed sizes") {
        using Size = Size32;
        using MixedSlab = Slab<int, Index8, Version4, Size>;
        auto slab = MixedSlab::create(4u);

        REQUIRE(MixedSlab::max_version == 15); // 4 bits = 0xF

        // Use slot through 16 versions (0-15), then it's dead
        for (int use = 0; use < 16; ++use) {
            slab->emplace(Index8{0}, use);
            slab->destroy(Index8{0});
        }
        REQUIRE(slab->dead_count() == 1);
    }
}

} // anonymous namespace
