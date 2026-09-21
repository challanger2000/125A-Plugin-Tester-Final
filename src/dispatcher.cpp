#include <windows.h>

#include "report_paths.h"

#include <algorithm>
#include <cstdio>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

std::wstring quoteArg(const std::wstring& value) {
    std::wstring result = L"\"";
    size_t backslashes = 0;
    for (const wchar_t ch : value) {
        if (ch == L'\\') {
            ++backslashes;
            continue;
        }
        if (ch == L'\"') {
            result.append(backslashes * 2 + 1, L'\\');
            result.push_back(L'\"');
            backslashes = 0;
            continue;
        }
        result.append(backslashes, L'\\');
        backslashes = 0;
        result.push_back(ch);
    }
    result.append(backslashes * 2, L'\\');
    result.push_back(L'\"');
    return result;
}

fs::path executablePath() {
    std::vector<wchar_t> buffer(32768);
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size())
        return {};
    return fs::path(std::wstring(buffer.data(), length));
}

void stripOuterQuotes(std::wstring& value) {
    if (value.size() >= 2 && value.front() == L'\"' && value.back() == L'\"')
        value = value.substr(1, value.size() - 2);
}

bool isVst3Path(const fs::path& path) {
    std::wstring extension = path.extension().wstring();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
    return extension == L".vst3";
}

std::string utf8(const std::wstring& value) {
    if (value.empty())
        return {};
    const int required = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                                             nullptr, 0, nullptr, nullptr);
    if (required <= 0)
        return {};
    std::string result(static_cast<size_t>(required), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                        result.data(), required, nullptr, nullptr);
    return result;
}

std::string pathUtf8(const fs::path& path) {
    return utf8(path.wstring());
}

std::string jsonEscape(const std::string& value) {
    std::ostringstream out;
    for (const unsigned char ch : value) {
        switch (ch) {
            case '\"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\b': out << "\\b"; break;
            case '\f': out << "\\f"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (ch < 0x20) {
                    static constexpr char hex[] = "0123456789abcdef";
                    out << "\\u00" << hex[(ch >> 4) & 0x0f] << hex[ch & 0x0f];
                } else {
                    out << static_cast<char>(ch);
                }
                break;
        }
    }
    return out.str();
}

std::string utcTimestamp() {
    SYSTEMTIME time{};
    GetSystemTime(&time);
    char buffer[32]{};
    std::snprintf(buffer, sizeof(buffer), "%04u-%02u-%02uT%02u:%02u:%02uZ",
                  static_cast<unsigned>(time.wYear), static_cast<unsigned>(time.wMonth),
                  static_cast<unsigned>(time.wDay), static_cast<unsigned>(time.wHour),
                  static_cast<unsigned>(time.wMinute), static_cast<unsigned>(time.wSecond));
    return buffer;
}

int launchSingle(const fs::path& singleExe,
                 const std::vector<std::wstring>& arguments,
                 DWORD timeoutMs = 25u * 60u * 1000u) {
    std::wstring command = quoteArg(singleExe.wstring());
    for (const auto& argument : arguments) {
        command.push_back(L' ');
        command += quoteArg(argument);
    }

    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};

    const BOOL created = CreateProcessW(singleExe.c_str(),
                                        mutableCommand.data(),
                                        nullptr,
                                        nullptr,
                                        FALSE,
                                        CREATE_NO_WINDOW,
                                        nullptr,
                                        singleExe.parent_path().c_str(),
                                        &startup,
                                        &process);
    if (!created) {
        std::wcerr << L"[FAIL] Dispatcher - Could not launch internal tester. Windows error "
                   << GetLastError() << L'\n';
        return 2;
    }

    const DWORD waitResult = WaitForSingleObject(process.hProcess, timeoutMs);
    if (waitResult == WAIT_TIMEOUT) {
        TerminateProcess(process.hProcess, 124);
        WaitForSingleObject(process.hProcess, 5000);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        std::wcerr << L"[FAIL] Dispatcher - Complete plug-in test exceeded 25 minutes\n";
        return 124;
    }

    if (waitResult != WAIT_OBJECT_0) {
        const DWORD error = GetLastError();
        TerminateProcess(process.hProcess, 125);
        WaitForSingleObject(process.hProcess, 5000);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        std::wcerr << L"[FAIL] Dispatcher - Wait failed with Windows error " << error << L'\n';
        return 125;
    }

    DWORD exitCode = 125;
    if (!GetExitCodeProcess(process.hProcess, &exitCode)) {
        const DWORD error = GetLastError();
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        std::wcerr << L"[FAIL] Dispatcher - Could not query tester exit code. Windows error " << error << L'\n';
        return 125;
    }

    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);

    if (exitCode >= 0xC0000000u) {
        std::wcerr << L"[FAIL] Dispatcher - Internal tester terminated abnormally\n";
        return 126;
    }
    return static_cast<int>(exitCode);
}

struct DiscoveryResult {
    std::vector<fs::path> plugins;
    std::vector<std::wstring> errors;
};

DiscoveryResult discoverPlugins(const fs::path& root) {
    DiscoveryResult result;
    std::error_code ec;
    fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
    const fs::recursive_directory_iterator end;
    if (ec) {
        result.errors.push_back(L"Could not start directory enumeration: " + utf8ToWide(ec.message()));
        return result;
    }

    while (it != end) {
        const fs::directory_entry entry = *it;
        if (isVst3Path(entry.path())) {
            result.plugins.push_back(entry.path());
            std::error_code typeError;
            if (entry.is_directory(typeError))
                it.disable_recursion_pending();
            if (typeError)
                result.errors.push_back(L"Could not inspect " + entry.path().wstring() + L": " +
                                        utf8ToWide(typeError.message()));
        }

        it.increment(ec);
        if (ec) {
            if (ec != std::errc::permission_denied)
                result.errors.push_back(L"Directory enumeration error: " + utf8ToWide(ec.message()));
            ec.clear();
        }
    }

    std::sort(result.plugins.begin(), result.plugins.end(), [](const fs::path& a, const fs::path& b) {
        std::wstring left = a.wstring();
        std::wstring right = b.wstring();
        std::transform(left.begin(), left.end(), left.begin(),
                       [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
        std::transform(right.begin(), right.end(), right.begin(),
                       [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
        return left < right;
    });
    result.plugins.erase(std::unique(result.plugins.begin(), result.plugins.end()), result.plugins.end());
    return result;
}

enum class ScanStatus { Pass, Warning, Fail, Inconclusive };

const wchar_t* statusLabel(ScanStatus status) {
    switch (status) {
        case ScanStatus::Pass: return L"PASS";
        case ScanStatus::Warning: return L"WARNING";
        case ScanStatus::Fail: return L"FAIL";
        case ScanStatus::Inconclusive: return L"INCONCLUSIVE";
    }
    return L"INCONCLUSIVE";
}

const char* statusLabelAscii(ScanStatus status) {
    switch (status) {
        case ScanStatus::Pass: return "PASS";
        case ScanStatus::Warning: return "WARNING";
        case ScanStatus::Fail: return "FAIL";
        case ScanStatus::Inconclusive: return "INCONCLUSIVE";
    }
    return "INCONCLUSIVE";
}

bool readTextFile(const fs::path& path, std::string& text) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;
    std::ostringstream buffer;
    buffer << in.rdbuf();
    text = buffer.str();
    return true;
}

struct ResultCounts {
    int pass = -1;
    int warning = -1;
    int fail = -1;
};

ResultCounts parseCounts(const std::string& text) {
    ResultCounts counts;
    const std::string marker = "RESULT: ";
    const size_t pos = text.rfind(marker);
    if (pos == std::string::npos)
        return counts;

    if (std::sscanf(text.c_str() + pos, "RESULT: %d PASS / %d WARNING / %d FAIL",
                    &counts.pass, &counts.warning, &counts.fail) != 3) {
        counts = {};
    }
    return counts;
}

ScanStatus classifyResult(const fs::path& pluginPath, int exitCode) {
    std::string text;
    if (readTextFile(ReportPaths::guard(pluginPath), text)) {
        if (text.find("TEST INCONCLUSIVE") != std::string::npos || text.find("INCONCLUSIVE") != std::string::npos)
            return ScanStatus::Inconclusive;
        return ScanStatus::Fail;
    }

    if (readTextFile(ReportPaths::qa(pluginPath), text)) {
        const ResultCounts counts = parseCounts(text);
        if (counts.fail >= 0) {
            if (counts.fail > 0)
                return ScanStatus::Fail;
            if (counts.warning > 0)
                return ScanStatus::Warning;
            return ScanStatus::Pass;
        }
    }

    if (exitCode == 1)
        return ScanStatus::Fail;
    return ScanStatus::Inconclusive;
}

std::vector<std::string> extractFindings(const std::string& text) {
    std::vector<std::string> findings;
    std::istringstream input(text);
    std::string line;
    while (std::getline(input, line)) {
        if (line.rfind("[PASS] ", 0) == 0 || line.rfind("[WARN] ", 0) == 0 ||
            line.rfind("[FAIL] ", 0) == 0 || line.rfind("[INFO] ", 0) == 0) {
            findings.push_back(line);
        }
    }
    return findings;
}

bool writePluginJson(const fs::path& pluginPath, ScanStatus status, int exitCode) {
    std::error_code ec;
    if (!ReportPaths::ensureRoot(ec))
        return false;

    std::string qaText;
    std::string guardText;
    readTextFile(ReportPaths::qa(pluginPath), qaText);
    readTextFile(ReportPaths::guard(pluginPath), guardText);
    const ResultCounts counts = parseCounts(qaText);

    std::vector<std::string> findings = extractFindings(qaText);
    const auto guardFindings = extractFindings(guardText);
    findings.insert(findings.end(), guardFindings.begin(), guardFindings.end());

    std::ofstream out(ReportPaths::json(pluginPath), std::ios::binary | std::ios::trunc);
    if (!out)
        return false;

    out << "{\n";
    out << "  \"schemaVersion\": 1,\n";
    out << "  \"testerVersion\": \"0.2.6\",\n";
    out << "  \"generatedUtc\": \"" << utcTimestamp() << "\",\n";
    out << "  \"pluginPath\": \"" << jsonEscape(pathUtf8(pluginPath)) << "\",\n";
    out << "  \"pluginId\": \"" << jsonEscape(utf8(ReportPaths::stablePathHash(pluginPath))) << "\",\n";
    out << "  \"status\": \"" << statusLabelAscii(status) << "\",\n";
    out << "  \"exitCode\": " << exitCode << ",\n";
    out << "  \"counts\": {\"pass\": " << counts.pass << ", \"warning\": " << counts.warning
        << ", \"fail\": " << counts.fail << "},\n";
    out << "  \"reports\": {\n";
    out << "    \"qa\": \"" << jsonEscape(pathUtf8(ReportPaths::qa(pluginPath))) << "\",\n";
    out << "    \"guard\": \"" << jsonEscape(pathUtf8(ReportPaths::guard(pluginPath))) << "\",\n";
    out << "    \"validator\": \"" << jsonEscape(pathUtf8(ReportPaths::validator(pluginPath))) << "\"\n";
    out << "  },\n";
    out << "  \"findings\": [\n";
    for (size_t i = 0; i < findings.size(); ++i) {
        out << "    \"" << jsonEscape(findings[i]) << "\"";
        if (i + 1 < findings.size())
            out << ',';
        out << '\n';
    }
    out << "  ]\n";
    out << "}\n";
    return static_cast<bool>(out);
}

struct ScanEntry {
    fs::path plugin;
    ScanStatus status = ScanStatus::Inconclusive;
    int exitCode = 0;
};

struct AggregateCounts {
    int pass = 0;
    int warning = 0;
    int fail = 0;
    int inconclusive = 0;
};

AggregateCounts aggregateCounts(const std::vector<ScanEntry>& entries) {
    AggregateCounts counts;
    for (const auto& entry : entries) {
        switch (entry.status) {
            case ScanStatus::Pass: ++counts.pass; break;
            case ScanStatus::Warning: ++counts.warning; break;
            case ScanStatus::Fail: ++counts.fail; break;
            case ScanStatus::Inconclusive: ++counts.inconclusive; break;
        }
    }
    return counts;
}

bool writeFolderProgress(size_t total,
                         size_t current,
                         const std::vector<ScanEntry>& entries,
                         const fs::path& currentPlugin,
                         const char* state) {
    std::error_code ec;
    if (!ReportPaths::ensureRoot(ec))
        return false;

    const AggregateCounts counts = aggregateCounts(entries);
    const fs::path target = ReportPaths::folderProgressTxt();
    const fs::path temporary = target.wstring() + L".tmp";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out)
            return false;
        out << "state=" << state << '\n';
        out << "total=" << total << '\n';
        out << "current=" << current << '\n';
        out << "pass=" << counts.pass << '\n';
        out << "warning=" << counts.warning << '\n';
        out << "fail=" << counts.fail << '\n';
        out << "inconclusive=" << counts.inconclusive << '\n';
        out << "plugin=" << pathUtf8(currentPlugin) << '\n';
        out.flush();
        if (!out)
            return false;
    }

    if (!MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        fs::remove(temporary, ec);
        return false;
    }
    return true;
}

bool writeFolderSummary(const fs::path& root,
                        const std::vector<ScanEntry>& entries,
                        const std::vector<std::wstring>& discoveryErrors) {
    std::error_code ec;
    if (!ReportPaths::ensureRoot(ec))
        return false;

    const AggregateCounts counts = aggregateCounts(entries);
    std::ofstream out(ReportPaths::folderSummaryTxt(), std::ios::binary | std::ios::trunc);
    if (!out)
        return false;

    out << "125A Plugin Tester / VST3 Folder Scan\n";
    out << "Version: 0.2.6\n";
    out << "Folder: " << pathUtf8(root) << "\n\n";
    out << "FOUND: " << entries.size() << "\n";
    out << "PASS: " << counts.pass << "\n";
    out << "WARNING: " << counts.warning << "\n";
    out << "FAIL: " << counts.fail << "\n";
    out << "INCONCLUSIVE: " << counts.inconclusive << "\n";
    out << "DISCOVERY_ERRORS: " << discoveryErrors.size() << "\n\n";

    for (const auto& error : discoveryErrors)
        out << "DISCOVERY ERROR: " << utf8(error) << "\n";
    if (!discoveryErrors.empty())
        out << '\n';

    for (const auto& entry : entries)
        out << "[" << statusLabelAscii(entry.status) << "] " << pathUtf8(entry.plugin)
            << " | exit=" << entry.exitCode << "\n";

    out.flush();
    if (!out)
        return false;

    std::ofstream json(ReportPaths::folderSummaryJson(), std::ios::binary | std::ios::trunc);
    if (!json)
        return false;

    json << "{\n";
    json << "  \"schemaVersion\": 1,\n";
    json << "  \"testerVersion\": \"0.2.6\",\n";
    json << "  \"generatedUtc\": \"" << utcTimestamp() << "\",\n";
    json << "  \"folder\": \"" << jsonEscape(pathUtf8(root)) << "\",\n";
    json << "  \"counts\": {\"found\": " << entries.size() << ", \"pass\": " << counts.pass
         << ", \"warning\": " << counts.warning << ", \"fail\": " << counts.fail
         << ", \"inconclusive\": " << counts.inconclusive
         << ", \"discoveryErrors\": " << discoveryErrors.size() << "},\n";
    json << "  \"discoveryErrors\": [\n";
    for (size_t i = 0; i < discoveryErrors.size(); ++i) {
        json << "    \"" << jsonEscape(utf8(discoveryErrors[i])) << "\"";
        if (i + 1 < discoveryErrors.size())
            json << ',';
        json << '\n';
    }
    json << "  ],\n";
    json << "  \"plugins\": [\n";
    for (size_t i = 0; i < entries.size(); ++i) {
        const auto& entry = entries[i];
        json << "    {\"path\": \"" << jsonEscape(pathUtf8(entry.plugin))
             << "\", \"id\": \"" << jsonEscape(utf8(ReportPaths::stablePathHash(entry.plugin)))
             << "\", \"status\": \"" << statusLabelAscii(entry.status)
             << "\", \"exitCode\": " << entry.exitCode
             << ", \"jsonReport\": \"" << jsonEscape(pathUtf8(ReportPaths::json(entry.plugin))) << "\"}";
        if (i + 1 < entries.size())
            json << ',';
        json << '\n';
    }
    json << "  ]\n";
    json << "}\n";
    return static_cast<bool>(json);
}

int runFolderScan(const fs::path& singleExe, const fs::path& root) {
    std::error_code ec;
    if (!ReportPaths::ensureRoot(ec)) {
        std::wcerr << L"[FAIL] Report storage - Could not create " << ReportPaths::root().wstring() << L'\n';
        return 2;
    }

    fs::remove(ReportPaths::folderSummaryTxt(), ec);
    ec.clear();
    fs::remove(ReportPaths::folderSummaryJson(), ec);
    ec.clear();
    fs::remove(ReportPaths::folderProgressTxt(), ec);
    ec.clear();

    const DiscoveryResult discovery = discoverPlugins(root);
    const auto& plugins = discovery.plugins;
    if (plugins.empty()) {
        std::wcerr << L"[FAIL] Folder scan - No .vst3 plug-ins found under " << root.wstring() << L'\n';
        for (const auto& error : discovery.errors)
            std::wcerr << L"[WARN] Folder discovery - " << error << L'\n';
        return 2;
    }

    std::wcout << L"\nFolder scan: " << root.wstring() << L'\n';
    std::wcout << L"Found " << plugins.size() << L" VST3 plug-in(s). Tests run serially.\n\n";
    if (!discovery.errors.empty())
        std::wcerr << L"[WARN] Folder discovery - " << discovery.errors.size()
                   << L" enumeration error(s); final scan will be marked incomplete.\n";

    std::vector<ScanEntry> entries;
    entries.reserve(plugins.size());
    writeFolderProgress(plugins.size(), 0, entries, {}, "running");

    for (size_t i = 0; i < plugins.size(); ++i) {
        const auto& plugin = plugins[i];
        writeFolderProgress(plugins.size(), i, entries, plugin, "running");
        std::wcout << L"============================================================\n";
        std::wcout << L"[" << (i + 1) << L" / " << plugins.size() << L"] " << plugin.wstring() << L'\n';
        std::wcout << L"============================================================\n";

        const int exitCode = launchSingle(singleExe, {plugin.wstring()});
        const ScanStatus status = classifyResult(plugin, exitCode);
        entries.push_back({plugin, status, exitCode});
        if (!writePluginJson(plugin, status, exitCode))
            std::wcerr << L"[WARN] JSON report - Could not write structured report for " << plugin.wstring() << L'\n';
        writeFolderProgress(plugins.size(), i + 1, entries, plugin, "running");
        std::wcout << L"Folder scan result: " << statusLabel(status) << L"\n\n";
    }

    const AggregateCounts counts = aggregateCounts(entries);
    if (!writeFolderSummary(root, entries, discovery.errors))
        std::wcerr << L"[WARN] Folder scan - Could not write aggregate TXT/JSON summary\n";
    writeFolderProgress(plugins.size(), plugins.size(), entries, {}, "complete");

    std::wcout << L"============================================================\n";
    std::wcout << L"  VST3 FOLDER SCAN COMPLETE\n";
    std::wcout << L"  Found:        " << entries.size() << L'\n';
    std::wcout << L"  PASS:         " << counts.pass << L'\n';
    std::wcout << L"  WARNING:      " << counts.warning << L'\n';
    std::wcout << L"  FAIL:         " << counts.fail << L'\n';
    std::wcout << L"  INCONCLUSIVE: " << counts.inconclusive << L'\n';
    std::wcout << L"  Discovery errors: " << discovery.errors.size() << L'\n';
    std::wcout << L"  Summary TXT:  " << ReportPaths::folderSummaryTxt().wstring() << L'\n';
    std::wcout << L"  Summary JSON: " << ReportPaths::folderSummaryJson().wstring() << L'\n';
    std::wcout << L"============================================================\n";

    return (counts.fail > 0 || counts.inconclusive > 0 || !discovery.errors.empty()) ? 1 : 0;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    const fs::path self = executablePath();
    if (self.empty()) {
        std::wcerr << L"[FAIL] Dispatcher - Could not determine executable path\n";
        return 2;
    }

    const fs::path singleExe = self.parent_path() / L"125A_Plugin_Tester_Single.exe";
    if (!fs::exists(singleExe)) {
        std::wcerr << L"[FAIL] Package - Missing " << singleExe.wstring() << L'\n';
        return 2;
    }

    if (argc == 3 && std::wstring(argv[1]) == L"--guard-selftest")
        return launchSingle(singleExe, {argv[1], argv[2]}, 60000);

    const bool interactive = argc < 2;
    std::wstring pathText;
    if (interactive) {
        std::wcout << L"============================================================\n";
        std::wcout << L"  125A Plugin Tester / Quality Checker v0.2.6\n";
        std::wcout << L"  Single VST3 or complete VST3 folder\n";
        std::wcout << L"============================================================\n\n";
        std::wcout << L"VST3 plug-in or folder path eingeben:\n> ";
        std::getline(std::wcin, pathText);
    } else {
        pathText = argv[1];
    }

    stripOuterQuotes(pathText);
    if (pathText.empty()) {
        std::wcerr << L"[FAIL] Input - No path supplied\n";
        return 2;
    }

    const fs::path input(pathText);
    std::error_code ec;
    if (!fs::exists(input, ec) || ec) {
        std::wcerr << L"[FAIL] Input - Path does not exist: " << input.wstring() << L'\n';
        return 2;
    }

    int result = 2;
    if (isVst3Path(input)) {
        result = launchSingle(singleExe, {input.wstring()});
        const ScanStatus status = classifyResult(input, result);
        if (!writePluginJson(input, status, result))
            std::wcerr << L"[WARN] JSON report - Could not write structured report\n";
    } else if (fs::is_directory(input, ec) && !ec) {
        result = runFolderScan(singleExe, input);
    } else {
        std::wcerr << L"[FAIL] Input - Select a .vst3 plug-in or a folder containing VST3 plug-ins\n";
        result = 2;
    }

    if (interactive) {
        std::wcout << L"\nDruecke ENTER zum Schliessen..." << std::flush;
        std::wstring ignored;
        std::getline(std::wcin, ignored);
    }
    return result;
}
