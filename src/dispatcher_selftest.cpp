// Deterministic classification self-tests for the Windows dispatcher.
// Keep the production implementation in the test translation unit: these
// assertions must exercise the same parser and classification code.
#define wmain unusedDispatcherEntry
#include "dispatcher.cpp"
#undef wmain

#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

bool overwrite(const fs::path& path, const std::string& value) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << value;
    out.flush();
    return out.good();
}

struct TestCase {
    const char* name;
    std::string qa;
    std::string guard;
    int subprocessExit;
    ScanStatus expected;
};

} // namespace

int wmain() {
    wchar_t temporary[MAX_PATH]{};
    const DWORD len = GetTempPathW(MAX_PATH, temporary);
    if (len == 0 || len >= MAX_PATH) {
        std::cerr << "[FAIL] Selftest: temporary directory unavailable\n";
        return 2;
    }
    const fs::path folder = fs::path(temporary) /
        (L"125A_Classifier_Selftest_" + std::to_wstring(GetCurrentProcessId()));
    std::error_code ec;
    fs::create_directories(folder, ec);
    if (ec || !SetEnvironmentVariableW(L"125A_PLUGIN_TESTER_REPORT_DIR", folder.c_str())) {
        std::cerr << "[FAIL] Selftest: report sandbox unavailable\n";
        return 2;
    }

    const fs::path plugin = folder / L"Control.vst3";
    const std::string positive =
        "[PASS] Plugin check - completed\nRESULT: 1 PASS / 0 WARNING / 0 FAIL\nRELEASE: PASS\n";
    const std::string warning =
        "[WARN] Optional check - warning\nRESULT: 1 PASS / 1 WARNING / 0 FAIL\nRELEASE: REVIEW WARNINGS\n";
    const std::string negative =
        "[FAIL] Functional check - failed\nRESULT: 1 PASS / 0 WARNING / 1 FAIL\nRELEASE: NOT RECOMMENDED\n";

    const std::vector<TestCase> cases{
        {"valid pass", positive, "", 0, ScanStatus::Pass},
        {"warning result", warning, "", 0, ScanStatus::Warning},
        {"report says fail", negative, "", 0, ScanStatus::Fail},
        {"contradicting failed exit", positive, "", 1, ScanStatus::Fail},
        {"unexpected exit", positive, "", 7, ScanStatus::Inconclusive},
        {"no report", "", "", 0, ScanStatus::Inconclusive},
        {"missing release marker", "RESULT: 1 PASS / 0 WARNING / 0 FAIL\n", "", 0,
            ScanStatus::Inconclusive},
        {"negative result counters", "RESULT: -1 PASS / 0 WARNING / 0 FAIL\nRELEASE: PASS\n",
            "", 0, ScanStatus::Inconclusive},
        {"broken result counters", "RESULT: nonsense\nRELEASE: PASS\n", "", 0,
            ScanStatus::Inconclusive},
        {"guard crash", positive, "[FAIL] Child crash\nRESULT: FAIL\n", 0,
            ScanStatus::Fail},
        {"guard inconclusive", positive, "RESULT: TEST INCONCLUSIVE\n", 0,
            ScanStatus::Inconclusive}
    };

    int fails = 0;
    for (const auto& item : cases) {
        fs::remove(ReportPaths::qa(plugin), ec);
        ec.clear();
        fs::remove(ReportPaths::guard(plugin), ec);
        ec.clear();
        if ((!item.qa.empty() && !overwrite(ReportPaths::qa(plugin), item.qa)) ||
            (!item.guard.empty() && !overwrite(ReportPaths::guard(plugin), item.guard))) {
            std::cerr << "[FAIL] Selftest write: " << item.name << '\n';
            ++fails;
            continue;
        }
        const auto actual = classifyResult(plugin, item.subprocessExit);
        if (actual != item.expected) {
            std::cerr << "[FAIL] Classification: " << item.name << '\n';
            ++fails;
        } else {
            std::cout << "[PASS] Classification: " << item.name << '\n';
        }
    }

    const fs::path blocked = folder / L"blocked-root-file";
    if (!overwrite(blocked, "not a directory") ||
        !SetEnvironmentVariableW(L"125A_PLUGIN_TESTER_REPORT_DIR", blocked.c_str())) {
        ++fails;
    } else if (writePluginJson(plugin, ScanStatus::Pass, 0)) {
        std::cerr << "[FAIL] JSON writing succeeded through a path that is a regular file\n";
        ++fails;
    } else {
        std::cout << "[PASS] JSON output failure is detected\n";
    }

    SetEnvironmentVariableW(L"125A_PLUGIN_TESTER_REPORT_DIR", nullptr);
    fs::remove_all(folder, ec);
    if (fails) {
        std::cerr << "[FAIL] Dispatcher classifier self-tests: " << fails << '\n';
        return 1;
    }
    std::cout << "[PASS] Dispatcher classifier self-tests: " << cases.size()
              << " cases and JSON output failure check\n";
    return 0;
}
