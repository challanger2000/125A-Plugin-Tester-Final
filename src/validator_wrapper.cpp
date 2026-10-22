#include <windows.h>

#include "report_paths.h"

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cstdio>
#include <iostream>
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

bool isVst3Path(const fs::path& path) {
    std::wstring extension = path.extension().wstring();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
    return extension == L".vst3";
}

fs::path findPluginArgument(int argc, wchar_t** argv) {
    for (int i = 1; i < argc; ++i) {
        fs::path candidate(argv[i]);
        if (isVst3Path(candidate))
            return candidate;
    }
    return {};
}

fs::path validatorReportPath(const fs::path& pluginPath) {
    return ReportPaths::validator(pluginPath);
}

void closeIfValid(HANDLE& handle) {
    if (handle != nullptr && handle != INVALID_HANDLE_VALUE) {
        CloseHandle(handle);
        handle = INVALID_HANDLE_VALUE;
    }
}

// 0 = a positive number of tests passed; 1 = test failure;
// 11 = validator ran but tested NOTHING; 2 = missing/untrustworthy evidence.
// Checking ONLY the validator process exit code is not enough for Mix FX.
int classifyValidatorSummary(const std::string& output) {
    std::istringstream stream(output);
    std::string line;
    bool found = false;
    unsigned int passed = 0;
    unsigned int failed = 0;
    while (std::getline(stream, line)) {
        const auto i = line.find("Result:");
        if (i == std::string::npos) continue;
        unsigned int p = 0, f = 0;
        if (std::sscanf(line.c_str() + i, "Result: %u tests passed, %u tests failed", &p, &f) == 2) {
            passed = p;
            failed = f;
            found = true;
        }
    }
    if (!found) return 2;
    if (failed > 0) return 1;
    if (passed == 0) return 11;
    return 0;
}

int inspectValidatorSummary(const fs::path& reportPath) {
    std::ifstream report(reportPath, std::ios::binary);
    if (!report) return 2;
    std::ostringstream buffer;
    buffer << report.rdbuf();
    if (report.bad()) return 2;
    return classifyValidatorSummary(buffer.str());
}


} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc == 2 && std::wstring(argv[1]) == L"--qa-summary-selftest") {
        const bool ok =
            classifyValidatorSummary("Result: 47 tests passed, 0 tests failed\n") == 0 &&
            classifyValidatorSummary("Result: 0 tests passed, 0 tests failed\r\n") == 11 &&
            classifyValidatorSummary("Result: 0 tests passed, 1 tests failed\n") == 1 &&
            classifyValidatorSummary("Result: 46 tests passed, 1 tests failed\n") == 1 &&
            classifyValidatorSummary("no result\n") == 2 &&
            classifyValidatorSummary("Result: 0 tests passed, 0 tests failed\nResult: 47 tests passed, 0 tests failed\n") == 0;
        std::wcout << (ok ? L"[PASS]" : L"[FAIL]")
                   << L" validator outcome selftest: positive/failed/zero/absent\n";
        return ok ? 0 : 31;
    }
    if (argc < 2) {
        std::wcerr << L"usage: 125A_Plugin_Tester_SteinbergValidator.exe <validator arguments>\n";
        return 2;
    }

    const fs::path self = executablePath();
    if (self.empty())
        return 2;

    const fs::path core = self.parent_path() / L"125A_Plugin_Tester_SteinbergValidatorCore.exe";
    if (!fs::exists(core)) {
        std::wcerr << L"Steinberg validator core missing: " << core.wstring() << L'\n';
        return 2;
    }

    std::wstring command = quoteArg(core.wstring());
    for (int i = 1; i < argc; ++i) {
        command.push_back(L' ');
        command += quoteArg(argv[i]);
    }

    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');

    const fs::path pluginPath = findPluginArgument(argc, argv);
    const fs::path reportPath = pluginPath.empty() ? fs::path{} : validatorReportPath(pluginPath);

    HANDLE reportHandle = INVALID_HANDLE_VALUE;
    HANDLE nullInput = INVALID_HANDLE_VALUE;
    BOOL inheritHandles = FALSE;

    if (!reportPath.empty()) {
        std::error_code ec;
        if (!ReportPaths::ensureRoot(ec)) {
            std::wcerr << L"Could not create 125A report directory: " << ReportPaths::root().wstring()
                       << L" (" << ec.message().c_str() << L")\n";
            return 2;
        }

        if (fs::exists(reportPath, ec)) {
            fs::remove(reportPath, ec);
            if (ec) {
                std::wcerr << L"Could not remove stale Steinberg validator report: "
                           << reportPath.wstring() << L'\n';
                return 2;
            }
        } else if (ec) {
            std::wcerr << L"Could not inspect Steinberg validator report path: "
                       << reportPath.wstring() << L'\n';
            return 2;
        }

        SECURITY_ATTRIBUTES security{};
        security.nLength = sizeof(security);
        security.bInheritHandle = TRUE;

        reportHandle = CreateFileW(reportPath.c_str(),
                                   GENERIC_WRITE,
                                   FILE_SHARE_READ,
                                   &security,
                                   CREATE_ALWAYS,
                                   FILE_ATTRIBUTE_NORMAL,
                                   nullptr);
        if (reportHandle == INVALID_HANDLE_VALUE) {
            std::wcerr << L"Could not create Steinberg validator report. Windows error "
                       << GetLastError() << L'\n';
            return 2;
        }

        // The wrapper itself is started by the production launcher with handle
        // inheritance disabled. Therefore its console/stdin handle is not a
        // safe handle to pass to the validator core. Give the core a known,
        // inheritable NUL input handle instead.
        nullInput = CreateFileW(L"NUL",
                                GENERIC_READ,
                                FILE_SHARE_READ | FILE_SHARE_WRITE,
                                &security,
                                OPEN_EXISTING,
                                FILE_ATTRIBUTE_NORMAL,
                                nullptr);
        if (nullInput == INVALID_HANDLE_VALUE) {
            const DWORD error = GetLastError();
            closeIfValid(reportHandle);
            std::wcerr << L"Could not create validator NUL stdin handle. Windows error " << error << L'\n';
            return 2;
        }
        inheritHandles = TRUE;
    }

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    if (reportHandle != INVALID_HANDLE_VALUE) {
        startup.dwFlags |= STARTF_USESTDHANDLES;
        startup.hStdInput = nullInput;
        startup.hStdOutput = reportHandle;
        startup.hStdError = reportHandle;
    }

    PROCESS_INFORMATION process{};
    const BOOL created = CreateProcessW(core.c_str(),
                                        mutableCommand.data(),
                                        nullptr,
                                        nullptr,
                                        inheritHandles,
                                        0,
                                        nullptr,
                                        core.parent_path().c_str(),
                                        &startup,
                                        &process);
    if (!created) {
        const DWORD error = GetLastError();
        closeIfValid(nullInput);
        closeIfValid(reportHandle);
        std::wcerr << L"Could not launch Steinberg validator core. Windows error " << error << L'\n';
        return 2;
    }

    const DWORD waitResult = WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exitCode = 2;
    if (waitResult == WAIT_OBJECT_0) {
        if (!GetExitCodeProcess(process.hProcess, &exitCode))
            exitCode = 2;
    }

    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    closeIfValid(nullInput);
    closeIfValid(reportHandle);

    if (!reportPath.empty())
        std::wcout << L"Steinberg validator diagnostics: " << reportPath.wstring() << L'\n';

    // Steinberg's normal module-validation path returns -1 when one or more
    // tests fail. Windows exposes that as 0xFFFFFFFF. Normalize it to 1 so
    // the outer 125A launcher can distinguish a deterministic validator FAIL
    // from a genuine unhandled exception.
    if (exitCode == 0xFFFFFFFFu)
        return 1;

    if (exitCode == 0u && !reportPath.empty()) {
        // A successful process with zero executed tests is NOT validator PASS.
        // The actual summary is mandatory; a missing/truncated report is
        // infrastructure INCONCLUSIVE, not an inferred successful validation.
        return inspectValidatorSummary(reportPath);
    }
    if (exitCode == 0u || exitCode == 1u || exitCode == 2u)
        return static_cast<int>(exitCode);

    // Preserve genuine NT exception-style exit codes so the outer launcher can
    // classify them as crashes and write an inconclusive guard report.
    if ((exitCode & 0xF0000000u) == 0xC0000000u)
        TerminateProcess(GetCurrentProcess(), exitCode);

    return 2;
}
