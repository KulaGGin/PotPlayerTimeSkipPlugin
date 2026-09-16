#include "plugin/window_enum.hpp"

#include <iterator>
#include <utility>

#include "diagnostics/log.hpp"

namespace plugin {

namespace {

BOOL CALLBACK EnumWindowsCallback(HWND hwnd, LPARAM lParam) {
    auto& result = *reinterpret_cast<EnumeratedWindows*>(lParam);

    DWORD ownerProcessId = 0;
    GetWindowThreadProcessId(hwnd, &ownerProcessId);
    if (ownerProcessId != GetCurrentProcessId()) {
        return TRUE;
    }

    wchar_t className[256];
    const int classLength = GetClassNameW(hwnd, className, static_cast<int>(std::size(className)));
    if (classLength <= 0) {
        return TRUE;
    }

    // Same-process window, so unlike docs/FINDINGS.md section 6's
    // cross-process gotcha, GetWindowText marshals nothing and just works.
    wchar_t title[256];
    const int titleLength = GetWindowTextW(hwnd, title, static_cast<int>(std::size(title)));

    const auto handleValue = reinterpret_cast<std::uintptr_t>(hwnd);
    result.candidates.push_back(WindowCandidate{
        handleValue, std::wstring(className, static_cast<std::size_t>(classLength)), IsWindowVisible(hwnd) != FALSE,
        titleLength > 0 ? std::wstring(title, static_cast<std::size_t>(titleLength)) : std::wstring{}});
    result.handlesByValue.emplace(handleValue, hwnd);

    return TRUE;
}

}  // namespace

EnumeratedWindows EnumerateOwnProcessWindows() {
    EnumeratedWindows result;
    if (!EnumWindows(&EnumWindowsCallback, reinterpret_cast<LPARAM>(&result))) {
        LOG_WARN("plugin: EnumWindows failed, gle={}", GetLastError());
    }
    return result;
}

}  // namespace plugin
