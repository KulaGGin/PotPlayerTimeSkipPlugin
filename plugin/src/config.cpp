#include "plugin/config.hpp"

#include <windows.h>

#include <optional>
#include <string>

#include "diagnostics/log.hpp"

namespace plugin {

namespace {

// Same %LOCALAPPDATA%\PotPlayerTimeSkip directory diagnostics/log.cpp's
// LogFilePath already creates for plugin.log, just a different file in it.
std::wstring ConfigFilePath() {
    wchar_t localAppData[MAX_PATH];
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return {};
    }

    std::wstring dir = localAppData;
    dir += L"\\PotPlayerTimeSkip";
    CreateDirectoryW(dir.c_str(), nullptr);  // ignore ERROR_ALREADY_EXISTS and any failure

    return dir + L"\\config.ini";
}

// config.ini is plain ASCII/UTF-8 text (every key and value core/config.hpp
// recognizes is ASCII), so this reads/writes raw bytes with no UTF-16
// conversion — only the path itself needs to be wide for the Win32 file
// APIs.
std::optional<std::string> ReadFileUtf8(const std::wstring& path) {
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                     FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return std::nullopt;
    }

    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0) {
        CloseHandle(file);
        return std::nullopt;
    }

    std::string contents(static_cast<std::size_t>(size.QuadPart), '\0');
    DWORD bytesRead = 0;
    const bool ok = contents.empty() ||
                    (ReadFile(file, contents.data(), static_cast<DWORD>(contents.size()), &bytesRead, nullptr) &&
                     bytesRead == contents.size());
    CloseHandle(file);
    if (!ok) {
        return std::nullopt;
    }
    return contents;
}

bool WriteFileUtf8(const std::wstring& path, const std::string& contents) {
    const HANDLE file =
        CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }

    DWORD written = 0;
    const bool ok = WriteFile(file, contents.data(), static_cast<DWORD>(contents.size()), &written, nullptr) &&
                    written == contents.size();
    CloseHandle(file);
    return ok;
}

}  // namespace

core::Config LoadConfig() {
    const std::wstring path = ConfigFilePath();
    if (path.empty()) {
        LOG_WARN("config: could not resolve %LOCALAPPDATA%, using built-in defaults");
        return core::DefaultConfig();
    }

    const std::optional<std::string> contents = ReadFileUtf8(path);
    if (!contents) {
        LOG_INFO("config: no existing config.ini, writing defaults");
        if (!WriteFileUtf8(path, core::SerializeConfig(core::DefaultConfig()))) {
            LOG_WARN("config: could not write default config.ini, gle={}", GetLastError());
        }
        return core::DefaultConfig();
    }

    core::ParsedConfig parsed = core::ParseConfig(*contents);
    for (const std::string& warning : parsed.warnings) {
        LOG_WARN("config: {}", warning);
    }
    return parsed.config;
}

}  // namespace plugin
