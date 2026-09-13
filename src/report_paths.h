#pragma once

#include <windows.h>

#include <cstdint>
#include <cwctype>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

inline std::wstring utf8ToWide(std::string_view value) {
    if (value.empty())
        return {};
    const int required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                              static_cast<int>(value.size()), nullptr, 0);
    if (required <= 0)
        return {};
    std::wstring result(static_cast<size_t>(required), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
                            result.data(), required) != required)
        return {};
    return result;
}

namespace ReportPaths {

namespace fs = std::filesystem;

inline fs::path environmentPath(const wchar_t* name) {
    std::vector<wchar_t> buffer(32768);
    const DWORD length = GetEnvironmentVariableW(name, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length > 0 && length < buffer.size())
        return fs::path(std::wstring(buffer.data(), length));
    return {};
}

inline fs::path root() {
    const fs::path overridePath = environmentPath(L"125A_PLUGIN_TESTER_REPORT_DIR");
    if (!overridePath.empty())
        return overridePath;

    const fs::path localAppData = environmentPath(L"LOCALAPPDATA");
    if (!localAppData.empty())
        return localAppData / L"125A Plugin Tester" / L"Reports";

    std::error_code ec;
    const fs::path temp = fs::temp_directory_path(ec);
    if (!ec && !temp.empty())
        return temp / L"125A Plugin Tester" / L"Reports";

    return fs::path(L".") / L"125A Plugin Tester" / L"Reports";
}

inline bool ensureRoot(std::error_code& ec) {
    ec.clear();
    fs::create_directories(root(), ec);
    return !ec;
}

inline std::wstring normalizedIdentity(const fs::path& pluginPath) {
    std::error_code ec;
    fs::path normalized = fs::absolute(pluginPath, ec);
    if (ec)
        normalized = pluginPath;
    normalized = normalized.lexically_normal();

    std::wstring value = normalized.wstring();
    for (auto& ch : value)
        ch = static_cast<wchar_t>(std::towlower(ch));
    return value;
}

inline std::wstring stablePathHash(const fs::path& pluginPath) {
    std::uint64_t hash = 14695981039346656037ull;
    for (const wchar_t ch : normalizedIdentity(pluginPath)) {
        hash ^= static_cast<std::uint32_t>(ch);
        hash *= 1099511628211ull;
    }

    std::wostringstream stream;
    stream << std::hex << std::setfill(L'0') << std::setw(16) << hash;
    return stream.str();
}

inline std::wstring baseName(const fs::path& pluginPath) {
    return pluginPath.stem().wstring() + L"_" + stablePathHash(pluginPath);
}

inline fs::path qa(const fs::path& pluginPath) { return root() / (baseName(pluginPath) + L"_125A_QA_Report.txt"); }
inline fs::path guard(const fs::path& pluginPath) { return root() / (baseName(pluginPath) + L"_125A_QA_Guard_Report.txt"); }
inline fs::path validator(const fs::path& pluginPath) { return root() / (baseName(pluginPath) + L"_125A_Steinberg_Validator.txt"); }
inline fs::path ioEvent(const fs::path& pluginPath) { return root() / (baseName(pluginPath) + L"_125A_IO_Event_Probe.txt"); }
inline fs::path json(const fs::path& pluginPath) { return root() / (baseName(pluginPath) + L"_125A_QA_Report.json"); }
inline fs::path folderSummaryTxt() { return root() / L"125A_Folder_Scan_Summary.txt"; }
inline fs::path folderSummaryJson() { return root() / L"125A_Folder_Scan_Summary.json"; }
inline fs::path folderProgressTxt() { return root() / L"125A_Folder_Scan_Progress.txt"; }

} // namespace ReportPaths
