#include <catch_amalgamated.hpp>

#include <functional>
#include <unordered_map>

#include "plugin/skip_setup.hpp"

using plugin::kAddButtonId;
using plugin::kCancelButtonId;
using plugin::kDeleteButtonId;
using plugin::kEditButtonId;
using plugin::kEnableCheckboxId;
using plugin::kOkButtonId;
using plugin::kRangeListId;
using plugin::MissingSkipSetupControl;
using plugin::ResolveSkipSetupControls;
using plugin::SkipSetupControls;

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
