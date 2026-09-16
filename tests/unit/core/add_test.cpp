#include <catch_amalgamated.hpp>

#include "core/core.hpp"

// Table-driven: every row is an independent Catch2 case (visible individually
// in `-r console` / `--list-tests` output and in CTest), not just one pass/fail.
TEST_CASE("core::add sums two integers", "[core][add]") {
    struct Case {
        int lhs;
        int rhs;
        int expected;
    };

    const auto [lhs, rhs, expected] = GENERATE(
        Case{0, 0, 0},
        Case{1, 1, 2},
        Case{2, 3, 5},
        Case{-2, 3, 1},
        Case{-5, -7, -12},
        Case{100000, 1, 100001}
    );

    CAPTURE(lhs, rhs);
    REQUIRE(core::add(lhs, rhs) == expected);
}

TEST_CASE("core::add is commutative", "[core][add]") {
    const auto [a, b] = GENERATE(std::pair{3, 4}, std::pair{-8, 15}, std::pair{0, -1});

    REQUIRE(core::add(a, b) == core::add(b, a));
}

// Negative case: pins down a result add() must NOT produce, so a broken
// implementation (e.g. one that returns lhs - rhs) fails loudly here even
// though the "sums two integers" cases above might accidentally pass for
// some inputs.
TEST_CASE("core::add does not subtract", "[core][add]") {
    REQUIRE(core::add(5, 3) != 5 - 3);
    REQUIRE_FALSE(core::add(5, 3) == 2);
}
