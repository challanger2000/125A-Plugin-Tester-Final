#define wmain legacyDispatcherMain
#include "dispatcher.cpp"
#undef wmain

#include <unordered_set>

namespace {

std::wstring lowerPathKey(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
    return value;
}

std::wstring directoryIdentityKey(const fs::path& path) {
    HANDLE handle = CreateFileW(path.c_str(),
                                0,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                nullptr,
                                OPEN_EXISTING,
                                FILE_FLAG_BACKUP_SEMANTICS,
                                nullptr);
    if (handle != INVALID_HANDLE_VALUE) {
        std::vector<wchar_t> buffer(32768);
        const DWORD length = GetFinalPathNameByHandleW(handle,
                                                       buffer.data(),
                                                       static_cast<DWORD>(buffer.size()),
                                                       FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
        CloseHandle(handle);
        if (length > 0 && length < buffer.size())
            return lowerPathKey(std::wstring(buffer.data(), length));
    }

    std::error_code ec;
    fs::path canonical = fs::weakly_canonical(path, ec);
    if (!ec)
        return lowerPathKey(canonical.wstring());

    ec.clear();
    fs::path absolute = fs::absolute(path, ec);
    if (!ec)
        return lowerPathKey(absolute.lexically_normal().wstring());

    return lowerPathKey(path.lexically_normal().wstring());
}

DiscoveryResult discoverPluginsRobust(const fs::path& root) {
    DiscoveryResult result;
    std::vector<fs::path> pending;
    pending.push_back(root);
    std::unordered_set<std::wstring> visitedDirectories;

    while (!pending.empty()) {
        const fs::path directory = pending.back();
        pending.pop_back();

        const std::wstring identity = directoryIdentityKey(directory);
        if (!visitedDirectories.insert(identity).second)
            continue;

        std::error_code ec;
        fs::directory_iterator it(directory, fs::directory_options::skip_permission_denied, ec);
        const fs::directory_iterator end;
        if (ec) {
            if (ec != std::errc::permission_denied)
                result.errors.push_back(L"Could not enumerate " + directory.wstring() + L": " +
                                        utf8ToWide(ec.message()));
            continue;
        }

        while (it != end) {
            const fs::directory_entry entry = *it;
            const fs::path entryPath = entry.path();

            // A .vst3 bundle is one plug-in target. Never descend into its
            // internal Contents tree, even when the bundle itself is a directory.
            if (isVst3Path(entryPath)) {
                result.plugins.push_back(entryPath);
            } else {
                std::error_code typeError;
                if (entry.is_directory(typeError))
                    pending.push_back(entryPath);
                if (typeError && typeError != std::errc::permission_denied)
                    result.errors.push_back(L"Could not inspect " + entryPath.wstring() + L": " +
                                            utf8ToWide(typeError.message()));
            }

            it.increment(ec);
            if (ec) {
                if (ec != std::errc::permission_denied)
                    result.errors.push_back(L"Directory enumeration error in " + directory.wstring() + L": " +
                                            utf8ToWide(ec.message()));
                ec.clear();
            }
        }
    }

    std::sort(result.plugins.begin(), result.plugins.end(), [](const fs::path& a, const fs::path& b) {
        return lowerPathKey(a.wstring()) < lowerPathKey(b.wstring());
    });
    result.plugins.erase(std::unique(result.plugins.begin(), result.plugins.end(), [](const fs::path& a, const fs::path& b) {
        return lowerPathKey(a.wstring()) == lowerPathKey(b.wstring());
    }), result.plugins.end());

    return result;
}

int runFolderScanRobust(const fs::path& singleExe, const fs::path& root) {
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

    const DiscoveryResult discovery = discoverPluginsRobust(root);
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
        result = runFolderScanRobust(singleExe, input);
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
