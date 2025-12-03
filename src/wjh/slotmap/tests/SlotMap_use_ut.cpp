// ----------------------------------------------------------------------
// Copyright 2025 Jody Hagins
// Distributed under the MIT Software License
// See accompanying file LICENSE or copy at
// https://opensource.org/licenses/MIT
// ----------------------------------------------------------------------

#include "../SlotMap.hpp"

#include <string>
#include <vector>

#include "testing/doctest.hpp"
#include "testing/rapidcheck.hpp"

namespace {
using namespace wjh::slotmap::literals;

using TestKey = wjh::slotmap::Key<int, 20_ib, 12_vb>;
using TestMap = wjh::SlotMap<TestKey>;

} // anonymous namespace

TEST_SUITE("SlotMap::use with Options")
{
    TEST_CASE("use with default Options behaves like regular use")
    {
        TestMap map;
        auto key = map.emplace(42);

        SUBCASE("without erase flag") {
            bool called = false;
            int value_seen = 0;

            auto result = map.use(
                key,
                [&](int & val, wjh::slotmap::Options & opts) {
                    called = true;
                    value_seen = val;
                    CHECK(opts.erase == false);
                });

            CHECK(result == true);
            CHECK(called == true);
            CHECK(value_seen == 42);
            CHECK(map.contains(key)); // Element still exists
            CHECK(map.size().value == 1);
        }

        SUBCASE("Options default constructed has erase = false") {
            wjh::slotmap::Options opts;
            CHECK(opts.erase == false);
        }
    }

    TEST_CASE("use with Options::erase = true erases after callback")
    {
        TestMap map;
        auto key = map.emplace(42);

        SUBCASE("erase single element") {
            int value_before_erase = 0;

            auto result = map.use(
                key,
                [&](int & val, wjh::slotmap::Options & opts) {
                    value_before_erase = val;
                    opts.erase = true;
                });

            CHECK(result == true);
            CHECK(value_before_erase == 42);
            CHECK(not map.contains(key)); // Element erased
            CHECK(map.size().value == 0);
        }

        SUBCASE("callback can read value before erase") {
            std::vector<int> values_seen;

            auto k1 = map.emplace(100);
            auto k2 = map.emplace(200);

            (void)map.use(k1, [&](int & val, wjh::slotmap::Options & opts) {
                values_seen.push_back(val);
                opts.erase = true;
            });

            CHECK(values_seen.size() == 1);
            CHECK(values_seen[0] == 100);
            CHECK(not map.contains(k1));
            CHECK(map.contains(k2)); // k2 still exists
            CHECK(map.size().value == 2); // Original key + k2
        }

        SUBCASE("callback can modify value before erase") {
            (void)map.use(key, [&](int & val, wjh::slotmap::Options & opts) {
                val = 999; // Modify before erase
                opts.erase = true;
            });

            CHECK(not map.contains(key));
            // Value was modified but then erased - can't verify the
            // modification but the important part is the erase happened after
            // the callback
        }
    }

    TEST_CASE("use with invalid key returns false")
    {
        TestMap map;
        auto key = map.emplace(42);
        map.erase(key); // Make key invalid

        bool called = false;
        auto result = map.use(key, [&](int &, wjh::slotmap::Options & opts) {
            called = true;
            opts.erase = true;
        });

        CHECK(result == false);
        CHECK(called == false);
    }

    TEST_CASE("use supports all callback signatures")
    {
        TestMap map;
        auto key = map.emplace(42);

        SUBCASE("void(key_type, T&, Options&)") {
            bool called = false;
            auto result = map.use(
                key,
                [&](TestKey k, int & val, wjh::slotmap::Options & opts) {
                    called = true;
                    CHECK(k == key);
                    CHECK(val == 42);
                    CHECK(opts.erase == false);
                });
            CHECK(result == true);
            CHECK(called == true);
        }

        SUBCASE("void(key_type, T&)") {
            bool called = false;
            auto result = map.use(key, [&](TestKey k, int & val) {
                called = true;
                CHECK(k == key);
                CHECK(val == 42);
            });
            CHECK(result == true);
            CHECK(called == true);
        }

        SUBCASE("void(T&, Options&)") {
            bool called = false;
            auto result = map.use(
                key,
                [&](int & val, wjh::slotmap::Options & opts) {
                    called = true;
                    CHECK(val == 42);
                    CHECK(opts.erase == false);
                });
            CHECK(result == true);
            CHECK(called == true);
        }

        SUBCASE("void(T&)") {
            bool called = false;
            auto result = map.use(key, [&](int & val) {
                called = true;
                CHECK(val == 42);
            });
            CHECK(result == true);
            CHECK(called == true);
        }
    }

    TEST_CASE("use with key_type parameter and erase")
    {
        TestMap map;
        auto key = map.emplace(100);

        SUBCASE("void(key_type, T&, Options&) with erase") {
            TestKey key_seen = TestKey::null();
            int value_seen = 0;

            auto result = map.use(
                key,
                [&](TestKey k, int & val, wjh::slotmap::Options & opts) {
                    key_seen = k;
                    value_seen = val;
                    opts.erase = true;
                });

            CHECK(result == true);
            CHECK(key_seen == key);
            CHECK(value_seen == 100);
            CHECK(not map.contains(key));
        }

        SUBCASE("void(key_type, T&) without erase") {
            TestKey key_seen = TestKey::null();

            auto result = map.use(key, [&](TestKey k, int & val) {
                key_seen = k;
                val = 200;
            });

            CHECK(result == true);
            CHECK(key_seen == key);
            CHECK(map.contains(key));

            (void)map.use(key, [](int & val) { CHECK(val == 200); });
        }
    }

    TEST_CASE("const use works with simple callbacks")
    {
        TestMap map;
        auto key = map.emplace(42);
        TestMap const & const_map = map;

        SUBCASE("const use with void(T const&)") {
            bool called = false;
            int value_seen = 0;

            auto result = const_map.use(key, [&](int const & val) {
                called = true;
                value_seen = val;
            });

            CHECK(result == true);
            CHECK(called == true);
            CHECK(value_seen == 42);
            CHECK(map.contains(key));
        }

        SUBCASE("const use with void(key_type, T const&)") {
            bool called = false;
            TestKey key_seen = TestKey::null();

            auto result = const_map.use(key, [&](TestKey k, int const & val) {
                called = true;
                key_seen = k;
                CHECK(val == 42);
            });

            CHECK(result == true);
            CHECK(called == true);
            CHECK(key_seen == key);
            CHECK(map.contains(key));
        }
    }

    TEST_CASE("use with erase on multiple elements")
    {
        TestMap map;
        std::vector<TestKey> keys;

        for (int i = 0; i < 10; ++i) {
            keys.push_back(map.emplace(i * 10));
        }

        CHECK(map.size().value == 10);

        SUBCASE("erase every other element") {
            for (std::size_t i = 0; i < keys.size(); i += 2) {
                (void)map.use(keys[i], [](int &, wjh::slotmap::Options & opts) {
                    opts.erase = true;
                });
            }

            CHECK(map.size().value == 5);

            for (std::size_t i = 0; i < keys.size(); ++i) {
                if (i % 2 == 0) {
                    CHECK(not map.contains(keys[i]));
                } else {
                    CHECK(map.contains(keys[i]));
                }
            }
        }

        SUBCASE("conditional erase based on value") {
            for (auto k : keys) {
                (void)map.use(k, [](int & val, wjh::slotmap::Options & opts) {
                    if (val >= 50) {
                        opts.erase = true;
                    }
                });
            }

            CHECK(map.size().value == 5);

            // Verify remaining elements are < 50
            map.for_each([](int const & val) { CHECK(val < 50); });
        }
    }

    TEST_CASE("use with Options and string values")
    {
        using StringKey = wjh::slotmap::Key<std::string, 20_ib, 12_vb>;
        using StringMap = wjh::SlotMap<StringKey>;

        StringMap map;
        auto key = map.emplace("hello");

        SUBCASE("read and erase string") {
            std::string value_copy;

            auto result = map.use(
                key,
                [&](std::string & val, wjh::slotmap::Options & opts) {
                    value_copy = val;
                    opts.erase = true;
                });

            CHECK(result == true);
            CHECK(value_copy == "hello");
            CHECK(not map.contains(key));
            CHECK(map.size().value == 0);
        }

        SUBCASE("modify then erase") {
            (void)map.use(
                key,
                [](std::string & val, wjh::slotmap::Options & opts) {
                    val += " world";
                    opts.erase = true;
                });

            CHECK(not map.contains(key));
        }
    }

} // TEST_SUITE

// Property-based tests
TEST_SUITE("SlotMap::use Options - Property tests")
{
    TEST_CASE("use with erase maintains map consistency")
    {
        rc::check(
            "erasing via use reduces size correctly",
            [](std::vector<int> const & values) {
                RC_PRE(not values.empty());

                TestMap map;
                std::vector<TestKey> keys;

                for (auto val : values) {
                    keys.push_back(map.emplace(val));
                }

                auto initial_size = map.size();
                RC_ASSERT(initial_size.value == values.size());

                // Erase first element via use
                auto erased = map.use(
                    keys[0],
                    [](int &, wjh::slotmap::Options & opts) {
                        opts.erase = true;
                    });

                RC_ASSERT(erased == true);
                RC_ASSERT(not map.contains(keys[0]));
                RC_ASSERT(map.size().value == values.size() - 1);

                // Verify all other keys still exist
                for (std::size_t i = 1; i < keys.size(); ++i) {
                    RC_ASSERT(map.contains(keys[i]));
                }
            });
    }

    TEST_CASE("use without erase never removes elements")
    {
        rc::check(
            "use without erase maintains all elements",
            [](std::vector<int> const & values) {
                RC_PRE(not values.empty());

                TestMap map;
                std::vector<TestKey> keys;

                for (auto val : values) {
                    keys.push_back(map.emplace(val));
                }

                // Use all elements without erase
                for (auto key : keys) {
                    (void)map.use(
                        key,
                        [](int & val, wjh::slotmap::Options & opts) {
                            val += 1;
                            CHECK(opts.erase == false); // Default is false
                        });
                }

                // All elements should still exist
                RC_ASSERT(map.size().value == values.size());
                for (auto key : keys) {
                    RC_ASSERT(map.contains(key));
                }
            });
    }

    TEST_CASE("conditional erase via Options")
    {
        rc::check(
            "conditional erase based on predicate",
            [](std::vector<int> const & values) {
                RC_PRE(not values.empty());

                TestMap map;
                std::vector<TestKey> keys;

                for (auto val : values) {
                    keys.push_back(map.emplace(val));
                }

                // Erase even values
                std::size_t expected_erased = 0;
                for (auto key : keys) {
                    (void)map.use(
                        key,
                        [](int & val, wjh::slotmap::Options & opts) {
                            if (val % 2 == 0) {
                                opts.erase = true;
                            }
                        });
                }

                for (auto val : values) {
                    if (val % 2 == 0) {
                        ++expected_erased;
                    }
                }

                auto expected_remaining = values.size() - expected_erased;
                RC_ASSERT(map.size().value == expected_remaining);

                // Verify only odd values remain
                map.for_each([](int const & val) { RC_ASSERT(val % 2 != 0); });
            });
    }

    TEST_CASE("use with erase is idempotent")
    {
        rc::check("using erased key returns false", []() {
            TestMap map;
            auto key = map.emplace(*rc::gen::inRange(0, 1000));

            // First use with erase
            auto result1 = map.use(
                key,
                [](int &, wjh::slotmap::Options & opts) { opts.erase = true; });

            RC_ASSERT(result1 == true);
            RC_ASSERT(not map.contains(key));

            // Second use should fail (key already erased)
            bool called = false;
            auto result2 = map.use(key, [&](int &, wjh::slotmap::Options &) {
                called = true;
            });

            RC_ASSERT(result2 == false);
            RC_ASSERT(called == false);
        });
    }

    TEST_CASE("all callback signatures work with Options")
    {
        rc::check("all signatures callable and erase works", []() {
            auto value = *rc::gen::inRange(0, 1000);

            SUBCASE("(T&, Options&)") {
                TestMap map;
                auto key = map.emplace(value);

                (void)map.use(key, [](int &, wjh::slotmap::Options & opts) {
                    opts.erase = true;
                });

                RC_ASSERT(not map.contains(key));
            }

            SUBCASE("(key_type, T&, Options&)") {
                TestMap map;
                auto key = map.emplace(value);

                (void)map.use(
                    key,
                    [key](TestKey k, int &, wjh::slotmap::Options & opts) {
                        RC_ASSERT(k == key);
                        opts.erase = true;
                    });

                RC_ASSERT(not map.contains(key));
            }

            SUBCASE("(key_type, T&) - no Options") {
                TestMap map;
                auto key = map.emplace(value);

                (void)map.use(key, [key](TestKey k, int &) {
                    RC_ASSERT(k == key);
                });

                RC_ASSERT(map.contains(key)); // Not erased
            }

            SUBCASE("(T&) - no Options") {
                TestMap map;
                auto key = map.emplace(value);

                (void)map.use(key, [](int &) {
                    // Do nothing
                });

                RC_ASSERT(map.contains(key)); // Not erased
            }
        });
    }

} // TEST_SUITE
