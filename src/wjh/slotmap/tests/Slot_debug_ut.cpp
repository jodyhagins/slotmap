// ----------------------------------------------------------------------
// Copyright 2025 Jody Hagins
// Distributed under the MIT Software License
// See accompanying file LICENSE or copy at
// https://opensource.org/licenses/MIT
// ----------------------------------------------------------------------

#include "wjh/slotmap/detail/Slot.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>

#include "testing/doctest.hpp"
#include "testing/rapidcheck.hpp"

namespace {

struct dtl
{
    template <typename T, int adj>
    struct Type
    {
        static constexpr unsigned num_bits = std::numeric_limits<T>::digits -
            adj;
        using value_type = T;
        static constexpr value_type mask = [] {
            if constexpr (num_bits >= std::numeric_limits<value_type>::digits) {
                return value_type(~value_type{0});
            } else {
                return value_type((value_type{1} << num_bits) - 1);
            }
        };
        value_type value;

        constexpr Type(value_type v)
        : value(v)
        { }

        constexpr operator value_type () const { return value; }
    };

    template <typename T, typename IndexT, typename VersionT, int adj = 1>
    using Slot =
        wjh::slotmap::detail::Slot<T, Type<IndexT, adj>, Type<VersionT, adj>>;
};

template <typename T, typename IndexT, typename VersionT>
using Slot = dtl::Slot<T, IndexT, VersionT, 1>;

template <typename T, typename IndexT, typename VersionT>
using FullSlot = dtl::Slot<T, IndexT, VersionT, 0>;

// ============================================================================
// Debug Mode Version Masking Tests
// ============================================================================

TEST_CASE("Slot Debug: version masking with spare bit")
{
    // This slot has room for an alive bit (adj=1 means we have 1 spare bit)
    Slot<int, std::uint32_t, std::uint32_t> slot{};

#ifdef WJH_SLOTMAP_DEBUG_MODE
    SUBCASE("version retrieval masks out alive bit") {
        // Set version to a known value
        slot.set_version(42);
        REQUIRE(slot.version() == 42);

        // Emplace changes alive bit but not version
        slot.emplace(100);
        REQUIRE(slot.version() == 42);

        // Destroy changes alive bit but not version
        slot.destroy();
        REQUIRE(slot.version() == 42);
    }

    SUBCASE("set_version preserves alive bit during lifecycle") {
        // Set version while free
        slot.set_version(10);
        REQUIRE(slot.version() == 10);

        // Emplace makes it alive
        slot.emplace(123);
        REQUIRE(slot.version() == 10);

        // Setting version while alive preserves alive state
        // (we can verify this by checking value() still works)
        slot.set_version(20);
        REQUIRE(slot.version() == 20);
        REQUIRE(slot.value() == 123); // Still alive and accessible

        slot.destroy();
    }

    SUBCASE("version survives multiple emplace/destroy cycles") {
        slot.set_version(100);

        for (int i = 0; i < 5; ++i) {
            slot.emplace(i * 10);
            REQUIRE(slot.version() == 100);
            REQUIRE(slot.value() == i * 10);
            slot.destroy();
            REQUIRE(slot.version() == 100);
        }
    }
#else
    SUBCASE("version access in non-debug mode") {
        // In non-debug mode, no masking needed
        slot.set_version(42);
        REQUIRE(slot.version() == 42);

        slot.emplace(100);
        REQUIRE(slot.version() == 42);

        slot.destroy();
        REQUIRE(slot.version() == 42);
    }
#endif
}

TEST_CASE("Slot Debug: full version bits have no masking")
{
    // This slot uses ALL version bits (adj=0 means no spare bit)
    FullSlot<int, std::uint32_t, std::uint32_t> slot{};

    SUBCASE("version access with full bits") {
        // Even in debug mode, when there's no spare bit, no masking occurs
        slot.set_version(std::numeric_limits<std::uint32_t>::max());
        REQUIRE(slot.version() == std::numeric_limits<std::uint32_t>::max());

        slot.emplace(42);
        REQUIRE(slot.version() == std::numeric_limits<std::uint32_t>::max());

        slot.destroy();
        REQUIRE(slot.version() == std::numeric_limits<std::uint32_t>::max());
    }
}

TEST_CASE("Slot Debug: version independence across bit sizes"){
#ifdef WJH_SLOTMAP_DEBUG_MODE
    SUBCASE("8-bit version"){Slot<int, std::uint8_t, std::uint8_t> slot{};

// Max version without high bit (high bit reserved for alive flag)
constexpr std::uint8_t max_version = (std::uint8_t(1) << 7) - 1;
slot.set_version(max_version);
REQUIRE(slot.version() == max_version);

slot.emplace(42);
REQUIRE(slot.version() == max_version);
REQUIRE(slot.value() == 42);

slot.destroy();
REQUIRE(slot.version() == max_version);
}

SUBCASE("16-bit version") {
    Slot<int, std::uint16_t, std::uint16_t> slot{};

    constexpr std::uint16_t max_version = (std::uint16_t(1) << 15) - 1;
    slot.set_version(max_version);
    REQUIRE(slot.version() == max_version);

    slot.emplace(42);
    REQUIRE(slot.version() == max_version);
    REQUIRE(slot.value() == 42);

    slot.destroy();
    REQUIRE(slot.version() == max_version);
}

SUBCASE("64-bit version") {
    Slot<int, std::uint64_t, std::uint64_t> slot{};

    constexpr std::uint64_t max_version = (std::uint64_t(1) << 63) - 1;
    slot.set_version(max_version);
    REQUIRE(slot.version() == max_version);

    slot.emplace(42);
    REQUIRE(slot.version() == max_version);
    REQUIRE(slot.value() == 42);

    slot.destroy();
    REQUIRE(slot.version() == max_version);
}
#endif
}

// ============================================================================
// Debug Mode Lifecycle Tests
// ============================================================================

TEST_CASE("Slot Debug: proper lifecycle transitions")
{
    Slot<std::string, std::uint32_t, std::uint32_t> slot{};

    SUBCASE("normal lifecycle doesn't assert") {
        // These operations should work without triggering assertions
        slot.set_version(1);
        slot.set_next(100);
        REQUIRE(slot.version() == 1);
        REQUIRE(slot.next() == 100);

        // Transition to alive
        slot.set_version(2);
        slot.emplace("test value");
        REQUIRE(slot.version() == 2);
        REQUIRE(slot.value() == "test value");

        // Transition back to free
        slot.destroy();
        slot.set_version(3);
        slot.set_next(200);
        REQUIRE(slot.version() == 3);
        REQUIRE(slot.next() == 200);
    }

    SUBCASE("multiple cycles maintain correctness") {
        for (std::uint32_t i = 0; i < 10; ++i) {
            slot.set_version(i);
            slot.emplace("cycle " + std::to_string(i));
            REQUIRE(slot.version() == i);
            REQUIRE(slot.value() == "cycle " + std::to_string(i));
            slot.destroy();
        }
    }
}

TEST_CASE("Slot Debug: version and next independence")
{
    Slot<int, std::uint32_t, std::uint32_t> slot{};

    SUBCASE("setting next doesn't affect version") {
        slot.set_version(42);
        slot.set_next(100);
        REQUIRE(slot.version() == 42);
        REQUIRE(slot.next() == 100);

        slot.set_next(200);
        REQUIRE(slot.version() == 42);
        REQUIRE(slot.next() == 200);
    }

    SUBCASE("setting version doesn't affect next") {
        slot.set_next(100);
        slot.set_version(42);
        REQUIRE(slot.next() == 100);
        REQUIRE(slot.version() == 42);

        slot.set_version(84);
        REQUIRE(slot.next() == 100);
        REQUIRE(slot.version() == 84);
    }
}

// ============================================================================
// Property-Based Tests for Debug Mode
// ============================================================================

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wexit-time-destructors"
#pragma clang diagnostic ignored "-Wglobal-constructors"
template <std::unsigned_integral IntT>
auto const gen_uint_no_high_bit =
    rc::gen::suchThat(rc::gen::arbitrary<IntT>(), [](IntT x) {
        static constexpr IntT hibit = IntT(
            IntT(1) << (std::numeric_limits<IntT>::digits - 1));
        return not (x & hibit);
    });
#pragma clang diagnostic pop

#ifdef WJH_SLOTMAP_DEBUG_MODE
TEST_CASE("Slot Debug: property-based version masking")
{
    rc::check("version correctly masked regardless of alive state", []() {
        auto const version = *gen_uint_no_high_bit<std::uint32_t>;

        Slot<int, std::uint32_t, std::uint32_t> slot{};

        // Set version while free
        slot.set_version(version);
        auto const v1 = slot.version();
        RC_ASSERT(v1 == version);

        // Version unchanged after emplace
        slot.emplace(123);
        auto const v2 = slot.version();
        RC_ASSERT(v2 == version);

        // Version unchanged after destroy
        slot.destroy();
        auto const v3 = slot.version();
        RC_ASSERT(v3 == version);
    });
}

TEST_CASE("Slot Debug: property-based lifecycle correctness")
{
    rc::check("multiple emplace/destroy cycles maintain correctness", []() {
        auto const num_cycles = *rc::gen::inRange(1, 10);
        auto const version = *gen_uint_no_high_bit<std::uint32_t>;

        Slot<int, std::uint32_t, std::uint32_t> slot{};
        slot.set_version(version);

        for (int i = 0; i < num_cycles; ++i) {
            // Emplace
            slot.emplace(i * 100);
            RC_ASSERT(slot.value() == i * 100);
            RC_ASSERT(slot.version() == version);

            // Destroy
            slot.destroy();
            RC_ASSERT(slot.version() == version);
        }
    });
}
#endif // WJH_SLOTMAP_DEBUG_MODE

TEST_CASE("Slot Debug: property-based version and next independence")
{
    rc::check("version and next are independent", []() {
        auto const version = *gen_uint_no_high_bit<std::uint32_t>;
        auto const next_val = *gen_uint_no_high_bit<std::uint32_t>;

        Slot<int, std::uint32_t, std::uint32_t> slot{};

        // Set both
        slot.set_version(version);
        slot.set_next(next_val);

        RC_ASSERT(slot.version() == version);
        RC_ASSERT(slot.next() == next_val);

        // Changing one doesn't affect the other
        slot.set_version(version + 1);
        RC_ASSERT(slot.next() == next_val);

        slot.set_next(next_val + 1);
        RC_ASSERT(slot.version() == version + 1);
    });
}

} // anonymous namespace
