// ----------------------------------------------------------------------
// Copyright 2025 Jody Hagins
// Distributed under the MIT Software License
// See accompanying file LICENSE or copy at
// https://opensource.org/licenses/MIT
// ----------------------------------------------------------------------
//
// Comprehensive tests for SlotMap::use() with return value support.
//
// This test suite validates the modified use() method that returns:
// - std::optional<R> for non-void callbacks (where R is callback's return type)
// - bool for void callbacks (backward compatibility)
//
// SAFETY CRITICAL: These tests verify memory safety, exception safety,
// type correctness, and edge case handling for the new return semantics.

#include "slotmap_test_utils.hpp"

#include "../SlotMap.hpp"

#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

#include "testing/doctest.hpp"
#include "testing/rapidcheck.hpp"

namespace {
using namespace wjh::slotmap::literals;

using TestKey = wjh::slotmap::Key<int, 20_ib, 12_vb>;
using TestMap = wjh::SlotMap<TestKey>;

// Helper types for comprehensive testing

// Move-only type to test move semantics
struct MoveOnly
{
    explicit MoveOnly(int v)
    : value(v)
    { }

    MoveOnly(MoveOnly const &) = delete;
    MoveOnly & operator = (MoveOnly const &) = delete;

    MoveOnly(MoveOnly && other) noexcept
    : value(other.value)
    {
        other.value = -1;
    }

    [[maybe_unused]]
    MoveOnly &
    operator = (MoveOnly && other) noexcept
    {
        if (this != &other) {
            value = other.value;
            other.value = -1;
        }
        return *this;
    }

    int value;
};

// Type that throws on copy but not on move
struct ThrowOnCopy
{
    explicit ThrowOnCopy(int v)
    : value(v)
    { }

    [[noreturn]] ThrowOnCopy(ThrowOnCopy const &)
    {
        throw std::runtime_error("copy failed");
    }

    [[noreturn]]
    ThrowOnCopy &
    operator = (ThrowOnCopy const &)
    {
        throw std::runtime_error("copy failed");
    }

    ThrowOnCopy(ThrowOnCopy && other) noexcept
    : value(other.value)
    {
        other.value = -1;
    }

    [[maybe_unused]]
    ThrowOnCopy &
    operator = (ThrowOnCopy && other) noexcept
    {
        if (this != &other) {
            value = other.value;
            other.value = -1;
        }
        return *this;
    }

    int value;
};

// Custom struct for testing struct returns
struct Point
{
    int x;
    int y;
};

} // anonymous namespace

// ============================================================================
// TEST SUITE: Non-void Return Values
// ============================================================================

TEST_SUITE("SlotMap::use - Non-void Return Values")
{
    TEST_CASE("use with int-returning callback returns optional<int>")
    {
        TestMap map;
        auto key = map.emplace(42);

        SUBCASE("valid key returns optional with value") {
            auto result = map.use(key, [](int const & val) { return val * 2; });

            // Verify return type
            static_assert(
                std::is_same_v<decltype(result), std::optional<int>>,
                "use() should return std::optional<int>");

            REQUIRE(result.has_value());
            CHECK(*result == 84);
            CHECK(map.contains(key)); // Element still exists
        }

        SUBCASE("invalid key returns nullopt") {
            map.erase(key);
            auto result = map.use(key, [](int const & val) { return val * 2; });

            REQUIRE_FALSE(result.has_value());
        }

        SUBCASE("callback can use mutable reference") {
            auto result = map.use(key, [](int & val) {
                val += 10;
                return val;
            });

            REQUIRE(result.has_value());
            CHECK(*result == 52);

            // Verify the modification persisted
            result = map.use(key, [](int const & val) { CHECK(val == 52); });
            REQUIRE(result.has_value());
        }
    }

    TEST_CASE("use with string-returning callback returns optional<string>")
    {
        using StringKey = wjh::slotmap::Key<std::string, 20_ib, 12_vb>;
        using StringMap = wjh::SlotMap<StringKey>;

        StringMap map;
        auto key = map.emplace("hello");

        SUBCASE("callback returns transformed string") {
            auto result = map.use(key, [](std::string const & s) {
                return s + " world";
            });

            static_assert(
                std::is_same_v<decltype(result), std::optional<std::string>>,
                "use() should return std::optional<std::string>");

            REQUIRE(result.has_value());
            CHECK(*result == "hello world");
            CHECK(map.contains(key));
        }

        SUBCASE("callback returns copy of value") {
            auto result = map.use(key, [](std::string const & s) { return s; });

            REQUIRE(result.has_value());
            CHECK(*result == "hello");
        }

        SUBCASE("invalid key returns nullopt") {
            StringKey invalid_key = StringKey::null();
            auto result = map.use(invalid_key, [](std::string const & s) {
                return s + "!";
            });

            REQUIRE_FALSE(result.has_value());
        }
    }

    TEST_CASE("use with custom struct return")
    {
        TestMap map;
        auto key = map.emplace(10);

        auto result = map.use(key, [](int val) {
            Point p;
            p.x = val;
            p.y = val * 2;
            return p;
        });

        static_assert(
            std::is_same_v<decltype(result), std::optional<Point>>,
            "use() should return std::optional<Point>");

        REQUIRE(result.has_value());
        CHECK(result->x == 10);
        CHECK(result->y == 20);
    }

    TEST_CASE("use with bool-returning callback returns optional<bool>")
    {
        // CRITICAL: bool return should NOT be confused with the old
        // found/not-found bool return type!

        TestMap map;
        auto key = map.emplace(42);

        SUBCASE("callback returns true") {
            auto result = map.use(key, [](int val) { return val > 0; });

            static_assert(
                std::is_same_v<decltype(result), std::optional<bool>>,
                "use() should return std::optional<bool>, not bool");

            REQUIRE(result.has_value());
            CHECK(*result == true);
        }

        SUBCASE("callback returns false") {
            auto result = map.use(key, [](int val) { return val < 0; });

            REQUIRE(result.has_value());
            CHECK(*result == false); // Returned false, not "not found"
        }

        SUBCASE("invalid key returns nullopt, not false") {
            map.erase(key);
            auto result = map.use(key, [](int val) { return val > 0; });

            REQUIRE_FALSE(result.has_value());
            // NOT: CHECK(*result == false)  <-- would be wrong!
        }
    }

    TEST_CASE("use with all callback signature variations")
    {
        TestMap map;
        auto key = map.emplace(100);

        SUBCASE("R(T&)") {
            auto result = map.use(key, [](int & val) { return val + 1; });
            REQUIRE(result.has_value());
            CHECK(*result == 101);
        }

        SUBCASE("R(key_type, T&)") {
            auto result = map.use(key, [key](TestKey k, int & val) {
                CHECK(k == key);
                return val + 2;
            });
            REQUIRE(result.has_value());
            CHECK(*result == 102);
        }

        SUBCASE("R(T&, Options&) - without erase") {
            auto result = map.use(
                key,
                [](int & val, wjh::slotmap::Options & opts) {
                    CHECK_FALSE(opts.erase);
                    return val + 3;
                });
            REQUIRE(result.has_value());
            CHECK(*result == 103);
            CHECK(map.contains(key)); // Not erased
        }

        SUBCASE("R(key_type, T&, Options&)") {
            auto result = map.use(
                key,
                [key](TestKey k, int & val, wjh::slotmap::Options & opts) {
                    CHECK(k == key);
                    CHECK_FALSE(opts.erase);
                    return val + 4;
                });
            REQUIRE(result.has_value());
            CHECK(*result == 104);
        }
    }

    TEST_CASE("const use with non-void callback")
    {
        TestMap map;
        auto key = map.emplace(42);
        TestMap const & const_map = map;

        auto result = const_map.use(key, [](int const & val) {
            return val * 3;
        });

        static_assert(
            std::is_same_v<decltype(result), std::optional<int>>,
            "const use() should return std::optional<int>");

        REQUIRE(result.has_value());
        CHECK(*result == 126);
        CHECK(map.contains(key));
    }

} // TEST_SUITE

// ============================================================================
// TEST SUITE: Void Return Values (Backward Compatibility)
// ============================================================================

TEST_SUITE("SlotMap::use - Void Return (Backward Compatibility)")
{
    TEST_CASE("void callback returns bool")
    {
        TestMap map;
        auto key = map.emplace(42);

        SUBCASE("valid key returns true") {
            bool called = false;
            auto result = map.use(key, [&called](int & val) {
                called = true;
                val = 99;
            });

            static_assert(
                std::is_same_v<decltype(result), bool>,
                "void callback should return bool");

            CHECK(result == true);
            CHECK(called);

            result = map.use(key, [](int val) { CHECK(val == 99); });
            CHECK(result == true);
        }

        SUBCASE("invalid key returns false") {
            map.erase(key);
            bool called = false;

            auto result = map.use(key, [&called](int &) { called = true; });

            CHECK(result == false);
            CHECK_FALSE(called);
        }
    }

    TEST_CASE("void callback with all signatures")
    {
        TestMap map;
        auto key = map.emplace(100);

        SUBCASE("void(T&)") {
            auto result = map.use(key, [](int & val) { val = 1; });
            static_assert(std::is_same_v<decltype(result), bool>);
            CHECK(result == true);
        }

        SUBCASE("void(key_type, T&)") {
            auto result = map.use(key, [](TestKey, int & val) { val = 2; });
            static_assert(std::is_same_v<decltype(result), bool>);
            CHECK(result == true);
        }

        SUBCASE("void(T&, Options&)") {
            auto result = map.use(key, [](int & val, wjh::slotmap::Options &) {
                val = 3;
            });
            static_assert(std::is_same_v<decltype(result), bool>);
            CHECK(result == true);
        }

        SUBCASE("void(key_type, T&, Options&)") {
            auto result = map.use(
                key,
                [](TestKey, int & val, wjh::slotmap::Options &) { val = 4; });
            static_assert(std::is_same_v<decltype(result), bool>);
            CHECK(result == true);
        }
    }

    TEST_CASE("const use with void callback")
    {
        TestMap map;
        auto key = map.emplace(42);
        TestMap const & const_map = map;

        bool called = false;
        auto result = const_map.use(key, [&called](int const &) {
            called = true;
        });

        static_assert(
            std::is_same_v<decltype(result), bool>,
            "const use() with void callback should return bool");

        CHECK(result == true);
        CHECK(called);
    }

} // TEST_SUITE

// ============================================================================
// TEST SUITE: Options::erase with Return Values
// ============================================================================

TEST_SUITE("SlotMap::use - Options::erase with Return Values")
{
    TEST_CASE("non-void callback with Options::erase")
    {
        TestMap map;
        auto key = map.emplace(42);

        SUBCASE("value is returned BEFORE erasure") {
            auto result = map.use(
                key,
                [](int & val, wjh::slotmap::Options & opts) {
                    opts.erase = true;
                    return val * 2;
                });

            REQUIRE(result.has_value());
            CHECK(*result == 84);
            CHECK_FALSE(map.contains(key)); // Element WAS erased
            CHECK(map.size().value == 0);
        }

        SUBCASE("callback can read value before setting erase") {
            auto result = map.use(
                key,
                [](int & val, wjh::slotmap::Options & opts) {
                    int copy = val;
                    val = 999; // Modify (will be erased anyway)
                    opts.erase = true;
                    return copy;
                });

            REQUIRE(result.has_value());
            CHECK(*result == 42); // Original value returned
            CHECK_FALSE(map.contains(key));
        }
    }

    TEST_CASE("void callback with Options::erase")
    {
        TestMap map;
        auto key = map.emplace(42);

        SUBCASE("returns true and erases") {
            int value_seen = 0;
            auto result = map.use(
                key,
                [&value_seen](int & val, wjh::slotmap::Options & opts) {
                    value_seen = val;
                    opts.erase = true;
                });

            static_assert(std::is_same_v<decltype(result), bool>);
            CHECK(result == true);
            CHECK(value_seen == 42);
            CHECK_FALSE(map.contains(key));
        }
    }

    TEST_CASE("invalid key with Options callback")
    {
        TestMap map;
        auto key = map.emplace(42);
        map.erase(key);

        SUBCASE("non-void callback returns nullopt") {
            bool called = false;
            auto result = map.use(
                key,
                [&called](int & val, wjh::slotmap::Options & opts) {
                    called = true;
                    opts.erase = true;
                    return val;
                });

            CHECK_FALSE(result.has_value());
            CHECK_FALSE(called);
        }

        SUBCASE("void callback returns false") {
            bool called = false;
            auto result = map.use(
                key,
                [&called](int &, wjh::slotmap::Options & opts) {
                    called = true;
                    opts.erase = true;
                });

            static_assert(std::is_same_v<decltype(result), bool>);
            CHECK(result == false);
            CHECK_FALSE(called);
        }
    }

} // TEST_SUITE

// ============================================================================
// TEST SUITE: Move-Only and Move Semantics
// ============================================================================

TEST_SUITE("SlotMap::use - Move-Only and Move Semantics")
{
    TEST_CASE("callback returns std::unique_ptr")
    {
        using UniquePtrKey =
            wjh::slotmap::Key<std::unique_ptr<int>, 20_ib, 12_vb>;
        using UniquePtrMap = wjh::SlotMap<UniquePtrKey>;

        UniquePtrMap map;
        auto key = map.emplace(std::make_unique<int>(42));

        SUBCASE("callback returns new unique_ptr") {
            auto result = map.use(key, [](std::unique_ptr<int> & ptr) {
                return std::make_unique<int>(*ptr * 2);
            });

            static_assert(
                std::is_same_v<
                    decltype(result),
                    std::optional<std::unique_ptr<int>>>,
                "should return optional<unique_ptr<int>>");

            REQUIRE(result.has_value());
            REQUIRE(*result != nullptr);
            CHECK(**result == 84);

            // Original still exists
            CHECK(map.contains(key));
        }

        SUBCASE("callback returns by value from value") {
            auto result = map.use(key, [](std::unique_ptr<int> & ptr) {
                return *ptr;
            });

            REQUIRE(result.has_value());
            CHECK(*result == 42);
        }
    }

    TEST_CASE("callback returns move-only type")
    {
        using MoveOnlyKey = wjh::slotmap::Key<int, 20_ib, 12_vb>;
        using MoveOnlyMap = wjh::SlotMap<MoveOnlyKey>;

        MoveOnlyMap map;
        auto key = map.emplace(42);

        auto result = map.use(key, [](int val) { return MoveOnly(val); });

        static_assert(
            std::is_same_v<decltype(result), std::optional<MoveOnly>>,
            "should return optional<MoveOnly>");

        REQUIRE(result.has_value());
        CHECK(result->value == 42);
    }

    TEST_CASE("callback returns move-only with erase")
    {
        TestMap map;
        auto key = map.emplace(100);

        auto result = map.use(key, [](int val, wjh::slotmap::Options & opts) {
            opts.erase = true;
            return MoveOnly(val);
        });

        REQUIRE(result.has_value());
        CHECK(result->value == 100);
        CHECK_FALSE(map.contains(key)); // Erased after callback
    }

} // TEST_SUITE

// ============================================================================
// TEST SUITE: Exception Safety
// ============================================================================

TEST_SUITE("SlotMap::use - Exception Safety")
{
    TEST_CASE("callback throws exception")
    {
        TestMap map;
        auto key = map.emplace(42);

        SUBCASE("exception propagates, map unchanged") {
            CHECK_THROWS_AS(
                (void)map.use(
                    key,
                    [](int &) -> int {
                        throw std::runtime_error("test exception");
                    }),
                std::runtime_error);

            // Map should be unchanged
            CHECK(map.contains(key));
            CHECK(map.size().value == 1);

            CHECK(map.use(key, [](int val) { CHECK(val == 42); }));
        }

        SUBCASE("exception after modifying value") {
            CHECK_THROWS_AS(
                (void)map.use(
                    key,
                    [](int & val) -> int {
                        val = 999;
                        throw std::runtime_error("test exception");
                    }),
                std::runtime_error);

            // Value modification should persist (no rollback)
            CHECK(map.use(key, [](int val) { CHECK(val == 999); }));
        }
    }

    TEST_CASE("void callback throws exception")
    {
        TestMap map;
        auto key = map.emplace(42);

        SUBCASE("exception propagates") {
            CHECK_THROWS_AS(
                (void)map.use(
                    key,
                    [](int &) { throw std::runtime_error("test exception"); }),
                std::runtime_error);

            CHECK(map.contains(key));
        }

        SUBCASE("exception with Options::erase set") {
            CHECK_THROWS_AS(
                (void)map.use(
                    key,
                    [](int &, wjh::slotmap::Options & opts) {
                        opts.erase = true;
                        throw std::runtime_error("test exception");
                    }),
                std::runtime_error);

            CHECK(not map.contains(key));
        }

        SUBCASE("exception before Options::erase set") {
            CHECK_THROWS_AS(
                (void)map.use(
                    key,
                    [](int &, wjh::slotmap::Options & opts) {
                        throw std::runtime_error("test exception");
                        opts.erase = true;
                    }),
                std::runtime_error);

            // Should NOT be erased
            CHECK(map.contains(key));
        }
    }

    TEST_CASE("callback returns type that throws on copy")
    {
        // This tests that returning ThrowOnCopy should use move, not copy
        TestMap map;
        auto key = map.emplace(42);

        SUBCASE("move construction succeeds") {
            auto result = map.use(key, [](int val) {
                return ThrowOnCopy(val);
            });

            // Should succeed via move, not throw via copy
            REQUIRE(result.has_value());
            CHECK(result->value == 42);
        }
    }

} // TEST_SUITE

// ============================================================================
// TEST SUITE: Edge Cases
// ============================================================================

TEST_SUITE("SlotMap::use - Edge Cases")
{
    TEST_CASE("callback returns reference type")
    {
        // WARNING: Returning references from use() is dangerous!
        // The reference becomes invalid if Options::erase is used.

        TestMap map;
        auto key = map.emplace(42);

        SUBCASE("callback returns value from reference") {
            // Safe: callback returns T&, but use() should return optional<T>
            auto result = map.use(key, [](int & val) -> int & { return val; });

            // The return type should be optional<int> (by-value copy)
            // NOT optional<int&> (reference wrapper)
            REQUIRE(result.has_value());
            CHECK(*result == 42);
        }
    }

    TEST_CASE("callback with complex return type")
    {
        TestMap map;
        auto key = map.emplace(10);

        SUBCASE("returns vector") {
            auto result = map.use(key, [](int val) {
                return std::vector<int>{val, val * 2, val * 3};
            });

            static_assert(std::is_same_v<
                          decltype(result),
                          std::optional<std::vector<int>>>);

            REQUIRE(result.has_value());
            REQUIRE(result->size() == 3);
            CHECK((*result)[0] == 10);
            CHECK((*result)[1] == 20);
            CHECK((*result)[2] == 30);
        }

        SUBCASE("returns pair") {
            auto result = map.use(key, [](int val) {
                return std::make_pair(val, val * 2);
            });

            REQUIRE(result.has_value());
            CHECK(result->first == 10);
            CHECK(result->second == 20);
        }
    }

    TEST_CASE("null key with non-void callback")
    {
        TestMap map;
        TestKey null_key = TestKey::null();

        auto result = map.use(null_key, [](int val) { return val * 2; });

        REQUIRE_FALSE(result.has_value());
    }

    TEST_CASE("callback returns optional")
    {
        // Nested optional: callback returns optional<int>
        TestMap map;
        auto key = map.emplace(42);

        auto result = map.use(key, [](int val) -> std::optional<int> {
            if (val > 0) {
                return val * 2;
            }
            return std::nullopt;
        });

        static_assert(
            std::is_same_v<decltype(result), std::optional<std::optional<int>>>,
            "should be optional<optional<int>>");

        REQUIRE(result.has_value()); // Outer optional
        REQUIRE(result->has_value()); // Inner optional
        CHECK(**result == 84);
    }

} // TEST_SUITE

// ============================================================================
// TEST SUITE: Property-Based Tests
// ============================================================================

TEST_SUITE("SlotMap::use - Property-Based Tests")
{
    TEST_CASE("returned value matches callback computation")
    {
        rc::check("use() returns exact callback result", [](int input_value) {
            TestMap map;
            auto key = map.emplace(input_value);

            SUBCASE("identity") {
                auto result = map.use(key, [](int val) { return val; });
                RC_ASSERT(result.has_value());
                RC_ASSERT(*result == input_value);
            }

            SUBCASE("multiply by 2") {
                auto result = map.use(key, [](int val) { return val * 2; });
                RC_ASSERT(result.has_value());
                RC_ASSERT(*result == input_value * 2);
            }

            SUBCASE("add 100") {
                auto result = map.use(key, [](int val) { return val + 100; });
                RC_ASSERT(result.has_value());
                RC_ASSERT(*result == input_value + 100);
            }

            SUBCASE("comparison") {
                auto result = map.use(key, [](int val) { return val > 0; });
                RC_ASSERT(result.has_value());
                RC_ASSERT(*result == (input_value > 0));
            }
        });
    }

    TEST_CASE("void callback always returns bool")
    {
        rc::check("void callback returns true for valid key", []() {
            auto value = *rc::gen::inRange(-1000, 1000);
            TestMap map;
            auto key = map.emplace(value);

            bool result = map.use(key, [](int & val) { val += 1; });
            RC_ASSERT(result == true);
            RC_ASSERT(map.contains(key));
        });
    }

    TEST_CASE("invalid key always returns nullopt or false")
    {
        rc::check("invalid key handling", []() {
            TestMap map;
            auto key = map.emplace(*rc::gen::inRange(0, 100));
            map.erase(key);

            SUBCASE("non-void returns nullopt") {
                auto result = map.use(key, [](int val) { return val; });
                RC_ASSERT_FALSE(result.has_value());
            }

            SUBCASE("void returns false") {
                bool result = map.use(key, [](int &) {});
                RC_ASSERT(result == false);
            }
        });
    }

    TEST_CASE("erase with return value maintains consistency")
    {
        rc::check(
            "erase via Options returns value before erasing",
            [](std::vector<int> values) {
                RC_PRE(not values.empty());

                TestMap map;
                std::vector<TestKey> keys;
                for (auto v : values) {
                    keys.push_back(map.emplace(v));
                }

                auto const initial_size = map.size().value;
                RC_ASSERT(initial_size == values.size());

                // Use first key with erase
                auto result = map.use(
                    keys[0],
                    [](int val, wjh::slotmap::Options & opts) {
                        opts.erase = true;
                        return val;
                    });

                RC_ASSERT(result.has_value());
                RC_ASSERT(*result == values[0]);
                RC_ASSERT_FALSE(map.contains(keys[0]));
                RC_ASSERT(map.size().value == initial_size - 1);

                // All other keys still valid
                for (std::size_t i = 1; i < keys.size(); ++i) {
                    RC_ASSERT(map.contains(keys[i]));
                }
            });
    }

    TEST_CASE("multiple uses return consistent values")
    {
        rc::check("multiple uses on same key", [](int value) {
            TestMap map;
            auto key = map.emplace(value);

            // Call use multiple times, should get same result
            auto result1 = map.use(key, [](int val) { return val * 2; });
            auto result2 = map.use(key, [](int val) { return val * 2; });
            auto result3 = map.use(key, [](int val) { return val * 2; });

            RC_ASSERT(result1.has_value());
            RC_ASSERT(result2.has_value());
            RC_ASSERT(result3.has_value());
            RC_ASSERT(*result1 == *result2);
            RC_ASSERT(*result2 == *result3);
        });
    }

    TEST_CASE("value modification visible in subsequent return")
    {
        rc::check("modifications persist", [](int initial, int delta) {
            TestMap map;
            auto key = map.emplace(initial);

            // First use: modify and return old value
            auto result1 = map.use(key, [delta](int & val) {
                int old = val;
                val += delta;
                return old;
            });

            RC_ASSERT(result1.has_value());
            RC_ASSERT(*result1 == initial);

            // Second use: return new value
            auto result2 = map.use(key, [](int val) { return val; });

            RC_ASSERT(result2.has_value());
            RC_ASSERT(*result2 == initial + delta);
        });
    }

} // TEST_SUITE

// ============================================================================
// TEST SUITE: Type Trait Verification
// ============================================================================

TEST_SUITE("SlotMap::use - Type Trait Verification")
{
    TEST_CASE("return type verification at compile time")
    {
        TestMap map;
        auto key = map.emplace(42);

        // These should all compile and have correct types
        static_assert(std::is_same_v<
                      decltype(map.use(key, [](int) { return 1; })),
                      std::optional<int>>);

        static_assert(std::is_same_v<
                      decltype(map.use(key, [](int) { return 1.5; })),
                      std::optional<double>>);

        static_assert(std::is_same_v<
                      decltype(map.use(key, [](int) { return true; })),
                      std::optional<bool>>);

        static_assert(
            std::is_same_v<
                decltype(map.use(key, [](int) { return std::string("hi"); })),
                std::optional<std::string>>);

        static_assert(std::is_same_v<decltype(map.use(key, [](int) {})), bool>);

        static_assert(
            std::is_same_v<decltype(map.use(key, [](int &) {})), bool>);

        static_assert(
            std::is_same_v<
                decltype(map.use(key, [](int &, wjh::slotmap::Options &) {})),
                bool>);

        static_assert(std::is_same_v<
                      decltype(map.use(
                          key,
                          [](int, wjh::slotmap::Options &) { return 1; })),
                      std::optional<int>>);
    }

    TEST_CASE("const use return types")
    {
        TestMap map;
        auto key = map.emplace(42);
        TestMap const & const_map = map;

        static_assert(
            std::is_same_v<
                decltype(const_map.use(key, [](int const &) { return 1; })),
                std::optional<int>>);

        static_assert(std::is_same_v<
                      decltype(const_map.use(key, [](int const &) {})),
                      bool>);
    }

} // TEST_SUITE
