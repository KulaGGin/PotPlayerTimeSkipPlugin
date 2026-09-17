#include <catch_amalgamated.hpp>

#include <optional>
#include <string>

#include "plugin/self_check.hpp"

using plugin::RunSelfCheckSequence;
using plugin::SelfCheckStage;

TEST_CASE("RunSelfCheckSequence passes when every stage passes", "[plugin][self_check]") {
    int calls = 0;
    const auto result = RunSelfCheckSequence(
        [&calls] {
            ++calls;
            return std::optional<std::string>{};
        },
        [&calls] {
            ++calls;
            return std::optional<std::string>{};
        },
        [&calls] {
            ++calls;
            return std::optional<std::string>{};
        });

    REQUIRE_FALSE(result.has_value());
    REQUIRE(calls == 3);
}

TEST_CASE("RunSelfCheckSequence reports the main window stage and never checks the dialogs",
          "[plugin][self_check]") {
    int dialogChecks = 0;
    const auto result = RunSelfCheckSequence(
        [] { return std::make_optional<std::string>("main window (class PotPlayer64) not found"); },
        [&dialogChecks] {
            ++dialogChecks;
            return std::optional<std::string>{};
        },
        [&dialogChecks] {
            ++dialogChecks;
            return std::optional<std::string>{};
        });

    REQUIRE(result.has_value());
    REQUIRE(result->stage == SelfCheckStage::kMainWindow);
    REQUIRE(result->detail == "main window (class PotPlayer64) not found");
    REQUIRE(dialogChecks == 0);
}

TEST_CASE("RunSelfCheckSequence reports the Skip Setup stage and never checks the interval dialog",
          "[plugin][self_check]") {
    int intervalChecks = 0;
    const auto result = RunSelfCheckSequence(
        [] { return std::optional<std::string>{}; },
        [] { return std::make_optional<std::string>("Skip Setup dialog or one of its controls not found"); },
        [&intervalChecks] {
            ++intervalChecks;
            return std::optional<std::string>{};
        });

    REQUIRE(result.has_value());
    REQUIRE(result->stage == SelfCheckStage::kSkipSetupDialog);
    REQUIRE(intervalChecks == 0);
}

TEST_CASE("RunSelfCheckSequence reports the Skip Interval Setup stage when only it fails",
          "[plugin][self_check]") {
    const auto result = RunSelfCheckSequence(
        [] { return std::optional<std::string>{}; },
        [] { return std::optional<std::string>{}; },
        [] { return std::make_optional<std::string>("Skip Interval Setup control id 3088 not found"); });

    REQUIRE(result.has_value());
    REQUIRE(result->stage == SelfCheckStage::kSkipIntervalDialog);
    REQUIRE(result->detail == "Skip Interval Setup control id 3088 not found");
}
