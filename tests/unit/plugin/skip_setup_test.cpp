#include <catch_amalgamated.hpp>

#include <functional>
#include <unordered_map>

#include "core/core.hpp"
#include "plugin/skip_setup.hpp"

using plugin::kAddButtonId;
using plugin::kCancelButtonId;
using plugin::kDeleteButtonId;
using plugin::kEditButtonId;
using plugin::kEnableCheckboxId;
using plugin::kFileSpecificComboIndex;
using plugin::kIntervalCancelButtonId;
using plugin::kIntervalEndEditId;
using plugin::kIntervalLengthEditId;
using plugin::kIntervalOkButtonId;
using plugin::kIntervalStartEditId;
using plugin::kIntervalTypeComboId;
using plugin::kOkButtonId;
using plugin::kRangeListId;
using plugin::MissingSkipIntervalControl;
using plugin::MissingSkipSetupControl;
using plugin::ResolveSkipIntervalControls;
using plugin::ResolveSkipSetupControls;
using plugin::SkipIntervalControls;
using plugin::SkipIntervalReadback;
using plugin::SkipSetupControls;
using plugin::VerifySkipIntervalReadback;

namespace {

// Stands in for GetDlgItem: a synthetic child-window set keyed by control
// id, missing ids simply absent (mirroring GetDlgItem's 0-on-not-found).
std::function<std::uintptr_t(int)> LookupOver(const std::unordered_map<int, std::uintptr_t>& handles) {
    return [&handles](int id) {
        const auto it = handles.find(id);
        return it == handles.end() ? std::uintptr_t{0} : it->second;
    };
}

}  // namespace

TEST_CASE("ResolveSkipSetupControls resolves every control by id", "[plugin][skip_setup]") {
    const std::unordered_map<int, std::uintptr_t> handles{
        {kEnableCheckboxId, 1}, {kRangeListId, 2}, {kAddButtonId, 3},
        {kEditButtonId, 4},     {kDeleteButtonId, 5}, {kOkButtonId, 6},
        {kCancelButtonId, 7},
    };

    const auto result = ResolveSkipSetupControls(LookupOver(handles));
    REQUIRE(std::holds_alternative<SkipSetupControls>(result));

    const auto& controls = std::get<SkipSetupControls>(result);
    REQUIRE(controls.enableCheckbox == 1);
    REQUIRE(controls.rangeList == 2);
    REQUIRE(controls.addButton == 3);
    REQUIRE(controls.editButton == 4);
    REQUIRE(controls.deleteButton == 5);
    REQUIRE(controls.okButton == 6);
    REQUIRE(controls.cancelButton == 7);
}

TEST_CASE("ResolveSkipSetupControls reports the first missing control", "[plugin][skip_setup]") {
    const std::unordered_map<int, std::uintptr_t> handles{
        {kEnableCheckboxId, 1}, {kRangeListId, 2}, {kAddButtonId, 3},
        // kEditButtonId deliberately absent.
        {kDeleteButtonId, 5}, {kOkButtonId, 6}, {kCancelButtonId, 7},
    };

    const auto result = ResolveSkipSetupControls(LookupOver(handles));
    REQUIRE(std::holds_alternative<MissingSkipSetupControl>(result));
    REQUIRE(std::get<MissingSkipSetupControl>(result).id == kEditButtonId);
}

TEST_CASE("ResolveSkipSetupControls reports the first control missing from an empty set", "[plugin][skip_setup]") {
    const auto result = ResolveSkipSetupControls(LookupOver({}));
    REQUIRE(std::holds_alternative<MissingSkipSetupControl>(result));
    REQUIRE(std::get<MissingSkipSetupControl>(result).id == kEnableCheckboxId);
}

TEST_CASE("ResolveSkipIntervalControls resolves every control by id", "[plugin][skip_setup][skip_interval]") {
    const std::unordered_map<int, std::uintptr_t> handles{
        {kIntervalStartEditId, 1}, {kIntervalEndEditId, 2}, {kIntervalLengthEditId, 3},
        {kIntervalTypeComboId, 4}, {kIntervalOkButtonId, 5}, {kIntervalCancelButtonId, 6},
    };

    const auto result = ResolveSkipIntervalControls(LookupOver(handles));
    REQUIRE(std::holds_alternative<SkipIntervalControls>(result));

    const auto& controls = std::get<SkipIntervalControls>(result);
    REQUIRE(controls.startEdit == 1);
    REQUIRE(controls.endEdit == 2);
    REQUIRE(controls.lengthEdit == 3);
    REQUIRE(controls.typeCombo == 4);
    REQUIRE(controls.okButton == 5);
    REQUIRE(controls.cancelButton == 6);
}

TEST_CASE("ResolveSkipIntervalControls reports the first missing control", "[plugin][skip_setup][skip_interval]") {
    const std::unordered_map<int, std::uintptr_t> handles{
        {kIntervalStartEditId, 1}, {kIntervalEndEditId, 2},
        // kIntervalLengthEditId deliberately absent.
        {kIntervalTypeComboId, 4}, {kIntervalOkButtonId, 5}, {kIntervalCancelButtonId, 6},
    };

    const auto result = ResolveSkipIntervalControls(LookupOver(handles));
    REQUIRE(std::holds_alternative<MissingSkipIntervalControl>(result));
    REQUIRE(std::get<MissingSkipIntervalControl>(result).id == kIntervalLengthEditId);
}

TEST_CASE("VerifySkipIntervalReadback accepts a read-back that matches the request",
          "[plugin][skip_setup][skip_interval]") {
    const auto range = core::SkipRange::Create(754567, 754567 + 671111);
    const SkipIntervalReadback readback{"00:12:34.567", "00:23:45.678", kFileSpecificComboIndex};

    REQUIRE_FALSE(VerifySkipIntervalReadback(range, readback).has_value());
}

TEST_CASE("VerifySkipIntervalReadback rejects a start time that doesn't match",
          "[plugin][skip_setup][skip_interval]") {
    const auto range = core::SkipRange::Create(754567, 754567 + 671111);
    const SkipIntervalReadback readback{"00:12:35.567", "00:23:45.678", kFileSpecificComboIndex};

    const auto mismatch = VerifySkipIntervalReadback(range, readback);
    REQUIRE(mismatch.has_value());
    REQUIRE(mismatch->field == "start");
    REQUIRE(mismatch->expected == "00:12:34.567");
    REQUIRE(mismatch->actual == "00:12:35.567");
}

TEST_CASE("VerifySkipIntervalReadback rejects an end time that doesn't match",
          "[plugin][skip_setup][skip_interval]") {
    const auto range = core::SkipRange::Create(754567, 754567 + 671111);
    const SkipIntervalReadback readback{"00:12:34.567", "00:23:46.678", kFileSpecificComboIndex};

    const auto mismatch = VerifySkipIntervalReadback(range, readback);
    REQUIRE(mismatch.has_value());
    REQUIRE(mismatch->field == "end");
    REQUIRE(mismatch->expected == "00:23:45.678");
    REQUIRE(mismatch->actual == "00:23:46.678");
}

TEST_CASE("VerifySkipIntervalReadback rejects a type that isn't File-specific",
          "[plugin][skip_setup][skip_interval]") {
    const auto range = core::SkipRange::Create(754567, 754567 + 671111);
    const SkipIntervalReadback readback{"00:12:34.567", "00:23:45.678", 0};

    const auto mismatch = VerifySkipIntervalReadback(range, readback);
    REQUIRE(mismatch.has_value());
    REQUIRE(mismatch->field == "type");
    REQUIRE(mismatch->expected == "1");
    REQUIRE(mismatch->actual == "0");
}

TEST_CASE("VerifySkipIntervalReadback rejects start/end text that doesn't even parse",
          "[plugin][skip_setup][skip_interval]") {
    const auto range = core::SkipRange::Create(754567, 754567 + 671111);
    const SkipIntervalReadback readback{"garbage", "00:23:45.678", kFileSpecificComboIndex};

    const auto mismatch = VerifySkipIntervalReadback(range, readback);
    REQUIRE(mismatch.has_value());
    REQUIRE(mismatch->field == "start");
    REQUIRE(mismatch->actual == "garbage");
}
