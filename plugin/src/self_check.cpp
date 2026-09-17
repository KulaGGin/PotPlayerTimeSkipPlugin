#include "plugin/self_check.hpp"

#include <windows.h>

#include <mutex>
#include <utility>

#include "diagnostics/log.hpp"
#include "plugin/player_window.hpp"
#include "plugin/skip_setup.hpp"

namespace plugin {

std::optional<SelfCheckFailure> RunSelfCheckSequence(
    const std::function<std::optional<std::string>()>& checkMainWindow,
    const std::function<std::optional<std::string>()>& checkSkipSetupDialog,
    const std::function<std::optional<std::string>()>& checkSkipIntervalDialog) {
    if (auto detail = checkMainWindow()) {
        return SelfCheckFailure{SelfCheckStage::kMainWindow, std::move(*detail)};
    }
    if (auto detail = checkSkipSetupDialog()) {
        return SelfCheckFailure{SelfCheckStage::kSkipSetupDialog, std::move(*detail)};
    }
    if (auto detail = checkSkipIntervalDialog()) {
        return SelfCheckFailure{SelfCheckStage::kSkipIntervalDialog, std::move(*detail)};
    }
    return std::nullopt;
}

std::optional<SelfCheckFailure> RunLiveSelfCheck() {
    // Kept open across the Skip-Setup and Skip-Interval stages (the third
    // stage needs it to click Add...) and closed exactly once at the end,
    // regardless of outcome — RunSelfCheckSequence itself holds no state.
    std::optional<SkipSetupDialog> dialog;

    const auto result = RunSelfCheckSequence(
        [] {
            return GetMainWindowHandle() == 0
                       ? std::make_optional<std::string>("main window (class PotPlayer64) not found")
                       : std::nullopt;
        },
        [&dialog] {
            dialog = OpenSkipSetup();
            if (!dialog) {
                return std::make_optional<std::string>(
                    "Skip Setup dialog or one of its controls not found");
            }
            return std::optional<std::string>{};
        },
        [&dialog] { return CheckSkipIntervalControls(*dialog); });

    if (dialog) {
        CloseSkipSetupCancel(*dialog);
    }
    return result;
}

namespace {

std::string DescribeStage(SelfCheckStage stage) {
    switch (stage) {
    case SelfCheckStage::kMainWindow:
        return "main window";
    case SelfCheckStage::kSkipSetupDialog:
        return "Skip Setup dialog";
    case SelfCheckStage::kSkipIntervalDialog:
        return "Skip Interval Setup dialog";
    }
    return "unknown stage";
}

// Same %LOCALAPPDATA%\PotPlayerTimeSkip directory diagnostics/log.cpp's
// LogFilePath and plugin/config.cpp's ConfigFilePath already create, just a
// different file in it — the last self-check outcome, for tools/install's
// status command (PTS-007) to read.
std::wstring StatusFilePath() {
    wchar_t localAppData[MAX_PATH];
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return {};
    }

    std::wstring dir = localAppData;
    dir += L"\\PotPlayerTimeSkip";
    CreateDirectoryW(dir.c_str(), nullptr);  // ignore ERROR_ALREADY_EXISTS and any failure

    return dir + L"\\selfcheck.ini";
}

void WriteStatusFile(bool passed, const std::string& version, const std::string& detail) {
    const std::wstring path = StatusFilePath();
    if (path.empty()) {
        return;
    }

    const std::string contents = "[SelfCheck]\r\nResult=" + std::string(passed ? "Pass" : "Fail") +
                                  "\r\nPotPlayerVersion=" + version + "\r\nDetail=" + detail + "\r\n";

    const HANDLE file =
        CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    DWORD written = 0;
    WriteFile(file, contents.data(), static_cast<DWORD>(contents.size()), &written, nullptr);
    CloseHandle(file);
}

std::optional<std::string> g_cachedFailureDetail;

}  // namespace

std::optional<std::string> EnsureSelfCheckPassed() {
    static std::once_flag onceFlag;
    std::call_once(onceFlag, [] {
        const std::string version = GetPotPlayerVersion().value_or("unknown");
        const auto failure = RunLiveSelfCheck();
        if (!failure) {
            LOG_INFO("self-check: passed, PotPlayer version {}", version);
            WriteStatusFile(true, version, "");
            return;
        }

        const std::string detail = DescribeStage(failure->stage) + ": " + failure->detail;
        LOG_ERROR("self-check: failed — {}", detail);
        WriteStatusFile(false, version, detail);
        g_cachedFailureDetail =
            "This PotPlayer version isn't supported (" + detail + ") — see docs/REVERIFICATION_CHECKLIST.md";
    });
    return g_cachedFailureDetail;
}

}  // namespace plugin
