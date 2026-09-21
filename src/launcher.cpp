#include <windows.h>

#include "report_paths.h"

#include <cstdio>
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

void stripOuterQuotes(std::wstring& value) {
    if (value.size() >= 2 && value.front() == L'\"' && value.back() == L'\"')
        value = value.substr(1, value.size() - 2);
}

void waitForEnter(bool interactive) {
    if (!interactive)
        return;
    std::wcout << L"\nDruecke ENTER zum Schliessen..." << std::flush;
    std::wstring ignored;
    std::getline(std::wcin, ignored);
}

fs::path executablePath() {
    std::vector<wchar_t> buffer(32768);
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size())
        return {};
    return fs::path(std::wstring(buffer.data(), length));
}

fs::path guardReportPath(const fs::path& pluginPath) {
    return ReportPaths::guard(pluginPath);
}

fs::path qaReportPath(const fs::path& pluginPath) {
    return ReportPaths::qa(pluginPath);
}

fs::path validatorReportPath(const fs::path& pluginPath) {
    return ReportPaths::validator(pluginPath);
}

fs::path legacyWorkerQaReportPath(const fs::path& pluginPath) {
    return pluginPath.parent_path() / (pluginPath.stem().wstring() + L"_125A_QA_Report.txt");
}

bool clearStaleReport(const fs::path& path) {
    std::error_code ec;
    const bool existed = fs::exists(path, ec);
    if (ec) {
        std::wcerr << L"[FAIL] Report cleanup - Could not inspect " << path.wstring() << L" (" << ec.message().c_str() << L")\n";
        return false;
    }
    if (!existed)
        return true;

    fs::remove(path, ec);
    if (ec || fs::exists(path)) {
        std::wcerr << L"[FAIL] Report cleanup - Could not remove stale report " << path.wstring() << L'\n';
        return false;
    }
    return true;
}

bool collectWorkerQaReport(const fs::path& pluginPath) {
    const fs::path source = legacyWorkerQaReportPath(pluginPath);
    const fs::path destination = qaReportPath(pluginPath);

    std::error_code ec;
    if (!fs::exists(source, ec)) {
        if (ec)
            return false;
        return fs::exists(destination);
    }

    fs::remove(destination, ec);
    ec.clear();
    fs::rename(source, destination, ec);
    if (!ec)
        return true;

    ec.clear();
    fs::copy_file(source, destination, fs::copy_options::overwrite_existing, ec);
    if (ec)
        return false;

    ec.clear();
    fs::remove(source, ec);
    return !ec;
}

std::string trimAscii(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
        return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::string validatorFailureSummary(const fs::path& pluginPath) {
    std::ifstream in(validatorReportPath(pluginPath), std::ios::binary);
    if (!in)
        return "Official Steinberg validator reported one or more failed tests; detailed validator report could not be read";

    std::string currentTest;
    std::string line;
    while (std::getline(in, line)) {
        line = trimAscii(line);
        if (line.empty())
            continue;

        if (line.size() >= 2 && line.front() == '[' && line.back() == ']' &&
            line.find("Succeeded") == std::string::npos && line.find("Failed") == std::string::npos) {
            currentTest = line.substr(1, line.size() - 2);
            continue;
        }

        constexpr const char* errorPrefix = "ERROR:";
        if (line.rfind(errorPrefix, 0) == 0) {
            const std::string error = trimAscii(line.substr(std::char_traits<char>::length(errorPrefix)));
            if (!currentTest.empty() && !error.empty())
                return currentTest + " - " + error;
            if (!error.empty())
                return error;
        }
    }

    return "Official Steinberg validator reported one or more failed tests; see detailed validator report";
}

std::string ioEventFailureSummary(const fs::path& pluginPath) {
    std::ifstream in(ReportPaths::ioEvent(pluginPath), std::ios::binary);
    if (!in)
        return "Isolated I/O/event probe detected a deterministic failure; detailed probe report could not be read";

    std::string line;
    constexpr const char* prefix = "FirstFailure:";
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.rfind(prefix, 0) != 0)
            continue;

        const std::string detail = trimAscii(line.substr(std::char_traits<char>::length(prefix)));
        if (!detail.empty() && detail != "None")
            return detail;
        break;
    }

    return "Isolated I/O/event probe detected a deterministic failure; see detailed probe report";
}

std::string editorLifecycleLastStage(const fs::path& pluginPath) {
    std::ifstream in(ReportPaths::editorLifecycle(pluginPath), std::ios::binary);
    if (!in)
        return {};

    std::string line;
    std::string lastStage;
    constexpr const char* prefix = "Stage:";
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.rfind(prefix, 0) != 0)
            continue;
        const std::string stage = trimAscii(line.substr(std::char_traits<char>::length(prefix)));
        if (!stage.empty())
            lastStage = stage;
    }
    return lastStage;
}

std::string editorLifecycleFailureDetail(const fs::path& pluginPath, const std::string& base) {
    const std::string stage = editorLifecycleLastStage(pluginPath);
    if (stage.empty())
        return base + "; exact stage unavailable";
    return base + "; last reached stage: " + stage;
}

void writeGuardReport(const fs::path& pluginPath,
                      const std::string& finding,
                      const std::string& detail,
                      bool inconclusive) {
    if (pluginPath.empty())
        return;

    std::error_code ec;
    if (!ReportPaths::ensureRoot(ec)) {
        std::wcerr << L"[WARN] Guard report - Could not create report directory "
                   << ReportPaths::root().wstring() << L" (" << ec.message().c_str() << L")\n";
        return;
    }

    const fs::path path = guardReportPath(pluginPath);
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        std::wcerr << L"[WARN] Guard report - Could not write " << path.wstring() << L'\n';
        return;
    }

    out << "125A Plugin Tester / Quality Checker\n";
    out << "Version: 0.2.6\n";
    out << "Guard: crash-isolated launcher with Windows Job Object containment\n";
    out << "Plugin: " << pluginPath.string() << "\n\n";
    out << "[FAIL] " << finding << " - " << detail << "\n\n";
    out << "RESULT: " << (inconclusive ? "TEST INCONCLUSIVE" : "FAIL") << "\n";
    out << "RELEASE: NOT RECOMMENDED" << (inconclusive ? " / INCONCLUSIVE" : "") << "\n";
    out.flush();

    if (out)
        std::wcout << L"Guard report: " << path.wstring() << L'\n';
}

bool annotateReport(const fs::path& pluginPath,
                    const std::string& level,
                    const std::string& test,
                    const std::string& detail,
                    int passDelta,
                    int warningDelta,
                    int failDelta) {
    const fs::path path = qaReportPath(pluginPath);
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;

    std::ostringstream buffer;
    buffer << in.rdbuf();
    std::string text = buffer.str();
    in.close();

    const std::string findingLine = "[" + level + "] " + test + (detail.empty() ? "" : " - " + detail) + "\n";
    if (text.find(findingLine) != std::string::npos)
        return true;

    const std::string resultMarker = "RESULT: ";
    const size_t resultPos = text.rfind(resultMarker);
    if (resultPos == std::string::npos)
        return false;

    const size_t resultEnd = text.find('\n', resultPos);
    const size_t resultLineLength = (resultEnd == std::string::npos ? text.size() : resultEnd) - resultPos;
    const std::string resultLine = text.substr(resultPos, resultLineLength);

    int passCount = 0;
    int warningCount = 0;
    int failCount = 0;
    if (std::sscanf(resultLine.c_str(), "RESULT: %d PASS / %d WARNING / %d FAIL", &passCount, &warningCount, &failCount) != 3)
        return false;

    passCount += passDelta;
    warningCount += warningDelta;
    failCount += failDelta;

    text.insert(resultPos, findingLine + "\n");

    const size_t shiftedResultPos = resultPos + findingLine.size() + 1;
    const size_t shiftedResultEnd = text.find('\n', shiftedResultPos);
    const size_t shiftedResultLineLength = (shiftedResultEnd == std::string::npos ? text.size() : shiftedResultEnd) - shiftedResultPos;

    std::ostringstream newResult;
    newResult << "RESULT: " << passCount << " PASS / " << warningCount << " WARNING / " << failCount << " FAIL";
    text.replace(shiftedResultPos, shiftedResultLineLength, newResult.str());

    const std::string releaseMarker = "RELEASE: ";
    const size_t releasePos = text.find(releaseMarker, shiftedResultPos);
    if (releasePos == std::string::npos)
        return false;
    const size_t releaseEnd = text.find('\n', releasePos);
    const size_t releaseLineLength = (releaseEnd == std::string::npos ? text.size() : releaseEnd) - releasePos;

    const std::string releaseValue = failCount > 0 ? "NOT RECOMMENDED" : (warningCount > 0 ? "REVIEW WARNINGS" : "PASS");
    text.replace(releasePos, releaseLineLength, releaseMarker + releaseValue);

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
        return false;
    out << text;
    out.flush();
    return static_cast<bool>(out);
}

void closeHandleIfValid(HANDLE& handle) {
    if (handle && handle != INVALID_HANDLE_VALUE) {
        CloseHandle(handle);
        handle = nullptr;
    }
}

int runIsolated(const fs::path& executable,
                const std::wstring& argument,
                const fs::path& reportSubject,
                DWORD timeoutMs,
                const std::string& stage,
                bool emitGuardReport = true) {
    std::wstring command = quoteArg(executable.wstring()) + L" " + quoteArg(argument);
    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');

    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (!job) {
        const DWORD error = GetLastError();
        if (emitGuardReport)
            writeGuardReport(reportSubject, stage + " isolation", "CreateJobObjectW failed with Windows error " + std::to_string(error), true);
        return 127;
    }

    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
        const DWORD error = GetLastError();
        CloseHandle(job);
        if (emitGuardReport)
            writeGuardReport(reportSubject, stage + " isolation", "SetInformationJobObject failed with Windows error " + std::to_string(error), true);
        return 127;
    }

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};

    const BOOL created = CreateProcessW(
        executable.c_str(),
        mutableCommand.data(),
        nullptr,
        nullptr,
        FALSE,
        CREATE_SUSPENDED,
        nullptr,
        executable.parent_path().c_str(),
        &startup,
        &process);

    if (!created) {
        const DWORD error = GetLastError();
        CloseHandle(job);
        if (emitGuardReport)
            writeGuardReport(reportSubject, stage + " launch", "CreateProcessW failed with Windows error " + std::to_string(error), true);
        return 2;
    }

    if (!AssignProcessToJobObject(job, process.hProcess)) {
        const DWORD error = GetLastError();
        TerminateProcess(process.hProcess, 127);
        WaitForSingleObject(process.hProcess, 5000);
        closeHandleIfValid(process.hThread);
        closeHandleIfValid(process.hProcess);
        CloseHandle(job);
        if (emitGuardReport)
            writeGuardReport(reportSubject, stage + " isolation", "AssignProcessToJobObject failed with Windows error " + std::to_string(error), true);
        return 127;
    }

    if (ResumeThread(process.hThread) == static_cast<DWORD>(-1)) {
        const DWORD error = GetLastError();
        TerminateJobObject(job, 127);
        WaitForSingleObject(process.hProcess, 5000);
        closeHandleIfValid(process.hThread);
        closeHandleIfValid(process.hProcess);
        CloseHandle(job);
        if (emitGuardReport)
            writeGuardReport(reportSubject, stage + " launch", "ResumeThread failed with Windows error " + std::to_string(error), true);
        return 127;
    }

    const DWORD waitResult = WaitForSingleObject(process.hProcess, timeoutMs);
    if (waitResult == WAIT_TIMEOUT) {
        if (!TerminateJobObject(job, 124))
            TerminateProcess(process.hProcess, 124);
        WaitForSingleObject(process.hProcess, 5000);
        closeHandleIfValid(process.hThread);
        closeHandleIfValid(process.hProcess);
        CloseHandle(job);
        const std::string timeoutDetail = stage + " exceeded " + std::to_string(timeoutMs / 1000.0) + " seconds and its process tree was terminated";
        if (emitGuardReport)
            writeGuardReport(reportSubject, stage + " timeout", timeoutDetail, true);
        return 124;
    }

    if (waitResult != WAIT_OBJECT_0) {
        const DWORD error = GetLastError();
        TerminateJobObject(job, 125);
        WaitForSingleObject(process.hProcess, 5000);
        closeHandleIfValid(process.hThread);
        closeHandleIfValid(process.hProcess);
        CloseHandle(job);
        if (emitGuardReport)
            writeGuardReport(reportSubject, stage + " wait", "WaitForSingleObject failed with Windows error " + std::to_string(error), true);
        return 125;
    }

    DWORD exitCode = 1;
    if (!GetExitCodeProcess(process.hProcess, &exitCode)) {
        const DWORD error = GetLastError();
        closeHandleIfValid(process.hThread);
        closeHandleIfValid(process.hProcess);
        CloseHandle(job);
        if (emitGuardReport)
            writeGuardReport(reportSubject, stage + " exit code", "GetExitCodeProcess failed with Windows error " + std::to_string(error), true);
        return 125;
    }

    closeHandleIfValid(process.hThread);
    closeHandleIfValid(process.hProcess);
    CloseHandle(job);

    if (exitCode >= 0xC0000000u) {
        std::ostringstream code;
        code << "0x" << std::hex << std::uppercase << exitCode;
        if (emitGuardReport)
            writeGuardReport(reportSubject, stage + " crash", "Process terminated abnormally with exit code " + code.str(), true);
        return 126;
    }

    return static_cast<int>(exitCode);
}

int runPluginStage(const fs::path& executable,
                   const fs::path& pluginPath,
                   DWORD timeoutMs,
                   const std::string& stage) {
    return runIsolated(executable, pluginPath.wstring(), pluginPath, timeoutMs, stage, true);
}

bool isInfrastructureFailure(int code) {
    return code == 2 || code == 124 || code == 125 || code == 126 || code == 127;
}

bool isExpectedStateResult(int code) {
    return code == 0 || code == 1 || code == 10 || code == 11;
}

bool isExpectedIOEventResult(int code) {
    return code == 0 || code == 1 || code == 10;
}

bool isEditorInfrastructureFailure(int code) {
    return code == 2 || code == 125 || code == 127;
}

bool isExpectedEditorLifecycleResult(int code) {
    return code == 0 || code == 1 || code == 10 || code == 124 || code == 126;
}

int runGuardSelfTest(const fs::path& helper) {
    if (!fs::exists(helper)) {
        std::wcerr << L"[FAIL] Production guard self-test - helper not found: " << helper.wstring() << L'\n';
        return 1;
    }

    struct Case {
        const wchar_t* mode;
        DWORD timeoutMs;
        int expected;
    };
    const Case cases[] = {
        {L"ok", 10000, 0},
        {L"fail", 10000, 1},
        {L"crash", 10000, 126},
        {L"hang", 1500, 124},
    };

    for (const auto& test : cases) {
        const int result = runIsolated(helper,
                                       test.mode,
                                       {},
                                       test.timeoutMs,
                                       "Production guard self-test",
                                       false);
        if (result != test.expected) {
            std::wcerr << L"[FAIL] Production guard self-test mode '" << test.mode
                       << L"' returned " << result << L", expected " << test.expected << L'\n';
            return 1;
        }
    }

    std::wcout << L"[PASS] Production guard self-test - normal, failure, crash and timeout paths passed through the real Job Object supervisor\n";
    return 0;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc == 3 && std::wstring(argv[1]) == L"--guard-selftest")
        return runGuardSelfTest(fs::path(argv[2]));

    const bool interactive = argc < 2;

    std::wcout << L"============================================================\n";
    std::wcout << L"  125A Plugin Tester / Quality Checker v0.2.6\n";
    std::wcout << L"  Job Object crash/hang isolation: ON\n";
    std::wcout << L"============================================================\n\n";

    std::wstring pathText;
    if (!interactive) {
        pathText = argv[1];
    } else {
        std::wcout << L"VST3 path eingeben:\n> ";
        std::getline(std::wcin, pathText);
    }

    stripOuterQuotes(pathText);

    if (pathText.empty()) {
        std::wcerr << L"[FAIL] Input - No VST3 path supplied\n";
        waitForEnter(interactive);
        return 2;
    }

    const fs::path pluginPath(pathText);
    if (!fs::exists(pluginPath)) {
        std::wcerr << L"[FAIL] Plugin path - Path does not exist: " << pluginPath.wstring() << L'\n';
        waitForEnter(interactive);
        return 2;
    }

    std::error_code reportEc;
    if (!ReportPaths::ensureRoot(reportEc)) {
        std::wcerr << L"[FAIL] Report storage - Could not create " << ReportPaths::root().wstring()
                   << L" (" << reportEc.message().c_str() << L")\n";
        waitForEnter(interactive);
        return 2;
    }

    if (!clearStaleReport(guardReportPath(pluginPath)) ||
        !clearStaleReport(qaReportPath(pluginPath)) ||
        !clearStaleReport(validatorReportPath(pluginPath)) ||
        !clearStaleReport(ReportPaths::ioEvent(pluginPath)) ||
        !clearStaleReport(ReportPaths::editorLifecycle(pluginPath)) ||
        !clearStaleReport(legacyWorkerQaReportPath(pluginPath))) {
        std::wcerr << L"Refusing to start because an old report could be mistaken for the new test result.\n";
        waitForEnter(interactive);
        return 2;
    }

    const fs::path self = executablePath();
    if (self.empty()) {
        std::wcerr << L"[FAIL] Launcher - Could not determine executable path\n";
        waitForEnter(interactive);
        return 2;
    }

    const fs::path reloadProbe = self.parent_path() / L"125A_Plugin_Tester_ReloadProbe.exe";
    const fs::path stateProbe = self.parent_path() / L"125A_Plugin_Tester_StateProbe.exe";
    const fs::path editorLifecycleProbe = self.parent_path() / L"125A_Plugin_Tester_EditorLifecycleProbe.exe";
    const fs::path ioEventProbe = self.parent_path() / L"125A_Plugin_Tester_IOEventProbe.exe";
    const fs::path validator = self.parent_path() / L"125A_Plugin_Tester_SteinbergValidator.exe";
    const fs::path worker = self.parent_path() / L"125A_Plugin_Tester_Worker.exe";

    for (const auto& required : {reloadProbe, stateProbe, editorLifecycleProbe, ioEventProbe, validator, worker}) {
        if (!fs::exists(required)) {
            std::wcerr << L"[FAIL] Package - Missing " << required.wstring() << L'\n';
            std::wcerr << L"Keep all EXE files from the release package in the same folder.\n";
            waitForEnter(interactive);
            return 2;
        }
    }

    constexpr DWORD probeTimeoutMs = 180000;
    constexpr DWORD validatorTimeoutMs = 300000;
    constexpr DWORD workerTimeoutMs = 180000;

    std::wcout << L"Reload/instantiation isolation: ON | Timeout: 180 s\n";
    const int reloadResult = runPluginStage(reloadProbe, pluginPath, probeTimeoutMs, "Reload/instantiation probe");
    if (reloadResult != 0) {
        if (reloadResult == 1)
            writeGuardReport(pluginPath, "Reload/instantiation stress", "Probe returned a deterministic failure", false);
        waitForEnter(interactive);
        return 1;
    }

    std::wcout << L"Fresh-instance state isolation: ON | Timeout: 180 s\n";
    const int stateResult = runPluginStage(stateProbe, pluginPath, probeTimeoutMs, "Fresh-instance state probe");
    if (isInfrastructureFailure(stateResult)) {
        waitForEnter(interactive);
        return 1;
    }
    if (!isExpectedStateResult(stateResult)) {
        writeGuardReport(pluginPath,
                         "Fresh-instance state probe",
                         "Probe returned unexpected exit code " + std::to_string(stateResult),
                         true);
        waitForEnter(interactive);
        return 1;
    }

    std::wcout << L"Editor lifecycle / GUI runtime isolation: ON | Timeout: 180 s\n";
    const int editorLifecycleResult = runIsolated(editorLifecycleProbe,
                                                  pluginPath.wstring(),
                                                  pluginPath,
                                                  probeTimeoutMs,
                                                  "Editor lifecycle / GUI runtime",
                                                  false);
    if (isEditorInfrastructureFailure(editorLifecycleResult)) {
        writeGuardReport(pluginPath,
                         "Editor lifecycle / GUI runtime infrastructure",
                         "Probe could not be executed safely; exit code " + std::to_string(editorLifecycleResult),
                         true);
        waitForEnter(interactive);
        return 1;
    }
    if (!isExpectedEditorLifecycleResult(editorLifecycleResult)) {
        writeGuardReport(pluginPath,
                         "Editor lifecycle / GUI runtime",
                         "Probe returned unexpected exit code " + std::to_string(editorLifecycleResult),
                         true);
        waitForEnter(interactive);
        return 1;
    }

    std::wcout << L"I/O, sidechain and event isolation: ON | Timeout: 180 s\n";
    const int ioEventResult = runPluginStage(ioEventProbe, pluginPath, probeTimeoutMs, "I/O and event probe");
    if (isInfrastructureFailure(ioEventResult)) {
        waitForEnter(interactive);
        return 1;
    }
    if (!isExpectedIOEventResult(ioEventResult)) {
        writeGuardReport(pluginPath,
                         "I/O and event probe",
                         "Probe returned unexpected exit code " + std::to_string(ioEventResult),
                         true);
        waitForEnter(interactive);
        return 1;
    }

    std::wcout << L"Steinberg VST3 validator isolation: ON | Timeout: 300 s\n";
    const int validatorResult = runPluginStage(validator, pluginPath, validatorTimeoutMs, "Steinberg validator");
    if (isInfrastructureFailure(validatorResult)) {
        waitForEnter(interactive);
        return 1;
    }
    if (validatorResult != 0 && validatorResult != 1) {
        writeGuardReport(pluginPath,
                         "Steinberg validator",
                         "Validator returned unexpected exit code " + std::to_string(validatorResult),
                         true);
        waitForEnter(interactive);
        return 1;
    }

    std::wcout << L"Worker isolation: ON | Timeout: 180 s\n\n";
    const int workerResult = runPluginStage(worker, pluginPath, workerTimeoutMs, "Worker");
    if (isInfrastructureFailure(workerResult)) {
        waitForEnter(interactive);
        return 1;
    }
    if (workerResult != 0 && workerResult != 1) {
        writeGuardReport(pluginPath,
                         "Worker",
                         "Worker returned unexpected exit code " + std::to_string(workerResult),
                         true);
        waitForEnter(interactive);
        return 1;
    }

    if (!collectWorkerQaReport(pluginPath)) {
        writeGuardReport(pluginPath,
                         "Worker report collection",
                         "QA report could not be moved into the per-user 125A report directory",
                         true);
        waitForEnter(interactive);
        return 1;
    }

    if (!annotateReport(pluginPath,
                        "PASS",
                        "Reload/instantiation stress",
                        "5 complete module reload and instance lifecycle cycles passed",
                        1, 0, 0)) {
        std::wcerr << L"[WARN] Report annotation - Reload result could not be integrated\n";
    }

    bool stateAnnotated = false;
    if (stateResult == 0)
        stateAnnotated = annotateReport(pluginPath, "PASS", "Fresh-instance state verification", "Component state survived transfer to a separately initialized instance and re-save", 1, 0, 0);
    else if (stateResult == 10)
        stateAnnotated = annotateReport(pluginPath, "INFO", "Fresh-instance state verification", "Not applicable because no component state was available", 0, 0, 0);
    else if (stateResult == 11)
        stateAnnotated = annotateReport(pluginPath, "WARN", "Fresh-instance state verification", "Component persistence probe completed with persistence/controller/lifecycle warning(s)", 0, 1, 0);
    else if (stateResult == 1)
        stateAnnotated = annotateReport(pluginPath, "FAIL", "Fresh-instance state verification", "Fresh-instance state transfer or verification failed", 0, 0, 1);
    if (!stateAnnotated)
        std::wcerr << L"[WARN] Report annotation - Fresh-instance state result could not be integrated\n";

    bool editorAnnotated = false;
    if (editorLifecycleResult == 0)
        editorAnnotated = annotateReport(pluginPath, "PASS", "Editor lifecycle / GUI runtime", "5 complete create/attach/runtime/detach/destroy cycles passed for each supplied editor", 1, 0, 0);
    else if (editorLifecycleResult == 10)
        editorAnnotated = annotateReport(pluginPath, "INFO", "Editor lifecycle / GUI runtime", "Not applicable because the plugin exposes no editor view", 0, 0, 0);
    else if (editorLifecycleResult == 1)
        editorAnnotated = annotateReport(pluginPath, "FAIL", "Editor lifecycle / GUI runtime",
                                         editorLifecycleFailureDetail(pluginPath, "Editor creation, native attachment, runtime, detach or destruction returned a deterministic failure"),
                                         0, 0, 1);
    else if (editorLifecycleResult == 124)
        editorAnnotated = annotateReport(pluginPath, "FAIL", "Editor lifecycle / GUI runtime",
                                         editorLifecycleFailureDetail(pluginPath, "Editor lifecycle probe hung or exceeded the 180 second timeout"),
                                         0, 0, 1);
    else if (editorLifecycleResult == 126)
        editorAnnotated = annotateReport(pluginPath, "FAIL", "Editor lifecycle / GUI runtime",
                                         editorLifecycleFailureDetail(pluginPath, "Editor lifecycle probe crashed or terminated with a Windows exception/access violation"),
                                         0, 0, 1);
    if (!editorAnnotated)
        std::wcerr << L"[WARN] Report annotation - Editor lifecycle result could not be integrated\n";

    bool ioAnnotated = false;
    if (ioEventResult == 0)
        ioAnnotated = annotateReport(pluginPath, "PASS", "I/O, sidechain and event isolation", "Bus arrangements, activation/restoration plus applicable sidechain and dense event processing completed without contradiction", 1, 0, 0);
    else if (ioEventResult == 10)
        ioAnnotated = annotateReport(pluginPath, "INFO", "I/O, sidechain and event isolation", "Not applicable because no AudioEffect class was exposed to the isolated probe", 0, 0, 0);
    else if (ioEventResult == 1)
        ioAnnotated = annotateReport(pluginPath, "FAIL", "I/O, sidechain and event isolation", ioEventFailureSummary(pluginPath), 0, 0, 1);
    if (!ioAnnotated)
        std::wcerr << L"[WARN] Report annotation - I/O/event result could not be integrated\n";

    const bool validatorAnnotated = validatorResult == 0
        ? annotateReport(pluginPath, "PASS", "Steinberg VST3 validator", "Official Steinberg validator completed successfully", 1, 0, 0)
        : annotateReport(pluginPath, "FAIL", "Steinberg VST3 validator", validatorFailureSummary(pluginPath), 0, 0, 1);
    if (!validatorAnnotated)
        std::wcerr << L"[WARN] Report annotation - Steinberg validator result could not be integrated\n";

    const bool editorFailure = editorLifecycleResult == 1 || editorLifecycleResult == 124 || editorLifecycleResult == 126;
    const bool deterministicFailure = workerResult == 1 || stateResult == 1 || editorFailure || ioEventResult == 1 || validatorResult == 1;
    const int result = deterministicFailure ? 1 : 0;
    std::wcout << L"\nReport storage: " << ReportPaths::root().wstring() << L'\n';
    std::wcout << L"Launcher result code: " << result << L'\n';
    waitForEnter(interactive);
    return result;
}