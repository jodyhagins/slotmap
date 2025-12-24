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
// using namespace wjh::slotmap::detail;
// namespace dtl = wjh::slotmap::detail;

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
    using Slot = wjh::slotmap::detail::Slot<
        wjh::slotmap::detail::
            SlotTraits<T, Type<IndexT, adj>, Type<VersionT, adj>, true>>;
};

template <typename T, typename IndexT, typename VersionT>
using Slot = dtl::Slot<T, IndexT, VersionT, 1>;

template <typename T, typename IndexT, typename VersionT>
using FullSlot = dtl::Slot<T, IndexT, VersionT, 0>;

// ============================================================================
// Basic Slot Tests
// ============================================================================

TEST_CASE("Slot: basic types and construction")
{
    SUBCASE("default construction does nothing") {
        using Slot = dtl::Slot<int, std::uint32_t, std::uint32_t>;
        CHECK(std::is_trivially_default_constructible_v<Slot>);
        alignas(alignof(Slot)) std::array<std::byte, sizeof(Slot)> buf;
        buf.fill(std::byte(0xa7));
        auto & slot = *::new (static_cast<void *>(buf.data())) Slot;

        std::array<std::byte, sizeof(Slot)> buf1, buf2;
        buf1.fill(std::byte(0xa7));
        std::memcpy(buf2.data(), &slot, sizeof(Slot));
        CHECK(buf1 == buf2);
    }

    SUBCASE("value-initialized construction initializes to free state") {
        using Slot = dtl::Slot<int, std::uint32_t, std::uint32_t>;
        CHECK(std::is_trivially_default_constructible_v<Slot>);
        alignas(alignof(Slot)) std::array<std::byte, sizeof(Slot)> buf;
        buf.fill(std::byte(0xa7));
        auto & slot = *::new (static_cast<void *>(buf.data())) Slot();

        REQUIRE(slot.version() == 0);
        REQUIRE(slot.next() == 0);
    }

    SUBCASE("different type configurations compile") {
        Slot<int, std::uint16_t, std::uint16_t> slot16{};
        Slot<int, std::uint32_t, std::uint32_t> slot32{};
        Slot<int, std::uint64_t, std::uint64_t> slot64{};

        REQUIRE(slot16.version() == 0);
        REQUIRE(slot32.version() == 0);
        REQUIRE(slot64.version() == 0);
    }

    SUBCASE("slot with non-trivial type") {
        Slot<std::string, std::uint32_t, std::uint32_t> slot{};

        REQUIRE(slot.version() == 0);
    }
}

TEST_CASE("Slot: version access")
{
    FullSlot<int, std::uint32_t, std::uint32_t> slot{};

    SUBCASE("set and get version") {
        slot.set_version(42);
        REQUIRE(slot.version() == 42);
    }

    SUBCASE("version can be set to max value") {
        slot.set_version(std::numeric_limits<std::uint32_t>::max());
        REQUIRE(slot.version() == std::numeric_limits<std::uint32_t>::max());
    }

    SUBCASE("version changes are independent of next") {
        slot.set_next(100);
        slot.set_version(42);
        REQUIRE(slot.next() == 100);
        REQUIRE(slot.version() == 42);
    }
}

TEST_CASE("Slot: free-list access")
{
    Slot<int, std::uint32_t, std::uint32_t> slot{};

    SUBCASE("set and get next") {
        slot.set_next(12345);
        REQUIRE(slot.next() == 12345);
    }

    SUBCASE("next can be set to max value") {
        slot.set_next(std::numeric_limits<std::uint32_t>::max());
        REQUIRE(slot.next() == std::numeric_limits<std::uint32_t>::max());
    }
}

TEST_CASE("Slot: emplace and value access")
{
    SUBCASE("emplace trivial type") {
        Slot<int, std::uint32_t, std::uint32_t> slot{};

        auto & ref = slot.emplace(42);
        REQUIRE(ref == 42);
        REQUIRE(slot.value() == 42);

        slot.destroy();
    }

    SUBCASE("emplace non-trivial type") {
        Slot<std::string, std::uint32_t, std::uint32_t> slot{};

        std::string expected;
        for (int i = 0; i < 1000; ++i) {
            expected.push_back('*');
        }
        auto & ref = slot.emplace(1000u, '*');
        CHECK(ref == expected);
        CHECK(std::addressof(slot.value()) == std::addressof(ref));
        CHECK(slot.value() == expected);

        slot.destroy();
    }

    SUBCASE("emplace with multiple arguments") {
        Slot<std::string, std::uint32_t, std::uint32_t> slot{};

        auto & ref = slot.emplace(std::size_t{5}, 'x');
        REQUIRE(ref == "xxxxx");
        REQUIRE(slot.value() == "xxxxx");

        slot.destroy();
    }

    SUBCASE("value is modifiable") {
        Slot<int, std::uint32_t, std::uint32_t> slot{};

        slot.emplace(10);
        slot.value() = 20;
        REQUIRE(slot.value() == 20);

        slot.destroy();
    }

    SUBCASE("const value access") {
        Slot<int, std::uint32_t, std::uint32_t> slot{};
        slot.emplace(42);

        auto const & const_slot = slot;
        REQUIRE(const_slot.value() == 42);

        slot.destroy();
    }
}

TEST_CASE("Slot: destroy")
{
    SUBCASE("destroy calls destructor") {
        static int destructor_count = 0;

        struct Tracker
        {
            ~Tracker() { ++destructor_count; }
        };

        destructor_count = 0;
        {
            Slot<Tracker, std::uint32_t, std::uint32_t> slot{};
            slot.emplace();
            REQUIRE(destructor_count == 0);
            slot.destroy();
            REQUIRE(destructor_count == 1);
        }
    }

    SUBCASE("destroy is noexcept for nothrow destructible types") {
        Slot<int, std::uint32_t, std::uint32_t> slot{};
        slot.emplace(42);

        static_assert(noexcept(slot.destroy()));

        slot.destroy();
    }
}

TEST_CASE("Slot: lifecycle - free to alive to free")
{
    Slot<std::string, std::uint32_t, std::uint32_t> slot{};

    // Initially free
    slot.set_version(0);
    slot.set_next(100);
    REQUIRE(slot.version() == 0);
    REQUIRE(slot.next() == 100);

    // Emplace - now alive
    slot.set_version(1);
    slot.emplace("test value");
    REQUIRE(slot.version() == 1);
    REQUIRE(slot.value() == "test value");

    // Destroy and return to free list
    slot.destroy();
    slot.set_version(2);
    slot.set_next(200);
    REQUIRE(slot.version() == 2);
    REQUIRE(slot.next() == 200);
}

TEST_CASE("Slot: version survives emplace/destroy cycle")
{
    Slot<int, std::uint32_t, std::uint32_t> slot{};

    // Set version before emplace
    slot.set_version(42);
    slot.emplace(100);

    // Version should still be accessible (it's stored separately)
    REQUIRE(slot.version() == 42);

    slot.destroy();

    // Version should survive destroy
    REQUIRE(slot.version() == 42);
}

// ============================================================================
// Property-Based Tests
// ============================================================================
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wexit-time-destructors"
#pragma clang diagnostic ignored "-Wglobal-constructors"
#endif
template <std::unsigned_integral IntT>
auto const gen_uint_no_high_bit =
    rc::gen::suchThat(rc::gen::arbitrary<IntT>(), [](IntT x) {
        static constexpr IntT hibit = IntT(
            IntT(1) << (std::numeric_limits<IntT>::digits - 1));
        return not (x & hibit);
    });
#ifdef __clang__
#pragma clang diagnostic pop
#endif

TEST_CASE("Slot: property-based version round-trip")
{
    rc::check("version round-trips correctly", []() {
        auto const version = *gen_uint_no_high_bit<std::uint32_t>;
        Slot<int, std::uint32_t, std::uint32_t> slot{};
        slot.set_version(version);
        RC_ASSERT(slot.version() == version);
    });
}

TEST_CASE("Slot: property-based next round-trip")
{
    rc::check("next round-trips correctly", []() {
        auto const next = *gen_uint_no_high_bit<std::uint32_t>;
        Slot<int, std::uint32_t, std::uint32_t> slot{};
        slot.set_next(next);
        RC_ASSERT(slot.next() == next);
    });
}

TEST_CASE("Slot: property-based version and next independence")
{
    rc::check("version and next are independent", []() {
        auto const version = *gen_uint_no_high_bit<std::uint32_t>;
        auto const next = *gen_uint_no_high_bit<std::uint32_t>;
        Slot<int, std::uint32_t, std::uint32_t> slot{};

        slot.set_version(version);
        slot.set_next(next);

        RC_ASSERT(slot.version() == version);
        RC_ASSERT(slot.next() == next);

        // Changing one doesn't affect the other
        slot.set_version(version + 1);
        RC_ASSERT(slot.next() == next);

        slot.set_next(next + 1);
        RC_ASSERT(slot.version() == version + 1);
    });
}

TEST_CASE("Slot: property-based emplace value round-trip")
{
    rc::check("emplace value round-trips correctly", []() {
        auto const value = *rc::gen::arbitrary<int>();
        Slot<int, std::uint32_t, std::uint32_t> slot{};

        slot.emplace(value);
        RC_ASSERT(slot.value() == value);

        slot.destroy();
    });
}

TEST_CASE("Slot: property-based string emplace")
{
    rc::check("string emplace round-trips correctly", []() {
        auto const value = *rc::gen::arbitrary<std::string>();
        Slot<std::string, std::uint32_t, std::uint32_t> slot{};

        slot.emplace(value);
        RC_ASSERT(slot.value() == value);

        slot.destroy();
    });
}

// ============================================================================
// Type Traits Tests
// ============================================================================

TEST_CASE("Slot: type traits")
{
    using SlotT = Slot<int, std::uint32_t, std::uint32_t>;

    SUBCASE("slot is not copyable") {
        static_assert(not std::is_copy_constructible_v<SlotT>);
        static_assert(not std::is_copy_assignable_v<SlotT>);
    }

    SUBCASE("slot is not movable") {
        static_assert(not std::is_move_constructible_v<SlotT>);
        static_assert(not std::is_move_assignable_v<SlotT>);
    }

    SUBCASE("slot is default constructible") {
        static_assert(std::is_default_constructible_v<SlotT>);
    }

    REQUIRE(true);
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST_CASE("Slot: edge cases with different index/version types")
{
    SUBCASE("8-bit index and version") {
        FullSlot<int, std::uint8_t, std::uint8_t> slot{};

        slot.set_version(255);
        slot.set_next(255);

        REQUIRE(slot.version() == 255);
        REQUIRE(slot.next() == 255);
    }

    SUBCASE("16-bit index and version") {
        FullSlot<int, std::uint16_t, std::uint16_t> slot{};

        slot.set_version(65535);
        slot.set_next(65535);

        REQUIRE(slot.version() == 65535);
        REQUIRE(slot.next() == 65535);
    }

    SUBCASE("64-bit index and version") {
        FullSlot<int, std::uint64_t, std::uint64_t> slot{};

        slot.set_version(std::numeric_limits<std::uint64_t>::max());
        slot.set_next(std::numeric_limits<std::uint64_t>::max());

        REQUIRE(slot.version() == std::numeric_limits<std::uint64_t>::max());
        REQUIRE(slot.next() == std::numeric_limits<std::uint64_t>::max());
    }
}

TEST_CASE("Slot: value type larger than index type")
{
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunsafe-buffer-usage"
#endif
    struct LargeValue
    {
        std::uint64_t data[16];

        bool operator == (LargeValue const & other) const
        {
            for (int i = 0; i < 16; ++i) {
                if (data[i] != other.data[i]) {
                    return false;
                }
            }
            return true;
        }
    };

    Slot<LargeValue, std::uint16_t, std::uint16_t> slot{};

    LargeValue val{};
    for (int i = 0; i < 16; ++i) {
        val.data[i] = static_cast<std::uint64_t>(i) * 1000;
    }

    slot.emplace(val);
    REQUIRE(slot.value() == val);

    slot.destroy();
#ifdef __clang__
#pragma clang diagnostic pop
#endif
}

TEST_CASE("Slot: value type smaller than index type")
{
    Slot<char, std::uint64_t, std::uint64_t> slot{};

    slot.emplace('X');
    REQUIRE(slot.value() == 'X');

    slot.destroy();

    // After destroy, we can use as free-list node
    slot.set_next(12345678901234ULL);
    REQUIRE(slot.next() == 12345678901234ULL);
}

} // anonymous namespace
