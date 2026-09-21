#include <windows.h>
#include <shellapi.h>

#include "report_paths.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#define main ioEventProbeMain
#include "io_event_probe.cpp"
#undef main

namespace fs = std::filesystem;

namespace {

fs::path exactPluginPathFromCommandLine() {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv)
        return {};

    fs::path result;
    if (argc == 2)
        result = fs::path(argv[1]);
    LocalFree(argv);
    return result;
}

std::string firstFailureLine(const std::string& text) {
    std::istringstream input(text);
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.rfind("[FAIL] ", 0) == 0)
            return line.substr(7);
    }
    return {};
}

const char* resultLabel(int result) {
    if (result == 0)
        return "PASS";
    if (result == 10)
        return "NOT_APPLICABLE";
    if (result == 1)
        return "FAIL";
    return "UNEXPECTED";
}

void writeProbeLog(const fs::path& pluginPath, const std::string& text, int result) {
    if (pluginPath.empty())
        return;

    std::error_code ec;
    if (!ReportPaths::ensureRoot(ec))
        return;

    std::ofstream out(ReportPaths::ioEvent(pluginPath), std::ios::binary | std::ios::trunc);
    if (!out)
        return;

    const std::string firstFailure = firstFailureLine(text);

    out << "125A Plugin Tester / Isolated I/O + Event Probe\n";
    out << "Plugin: " << pluginPath.string() << "\n";
    out << "ExitCode: " << result << "\n";
    out << "Result: " << resultLabel(result) << "\n";
    out << "FirstFailure: " << (firstFailure.empty() ? "None" : firstFailure) << "\n";
    out << "\n";
    out << text;
}

} // namespace

int main(int argc, char** argv) {
    const fs::path pluginPath = exactPluginPathFromCommandLine();

    std::ostringstream capture;
    std::streambuf* oldOut = std::cout.rdbuf(capture.rdbuf());
    std::streambuf* oldErr = std::cerr.rdbuf(capture.rdbuf());

    int result = 2;
    try {
        result = ioEventProbeMain(argc, argv);
    } catch (...) {
        std::cerr << "[FAIL] I/O and event probe wrapper - unhandled C++ exception\n";
        result = 1;
    }

    std::cout.rdbuf(oldOut);
    std::cerr.rdbuf(oldErr);

    const std::string text = capture.str();
    writeProbeLog(pluginPath, text, result);

    // Preserve the diagnostic stream for console/CI use as well as the sidecar report.
    if (result == 0 || result == 10)
        std::cout << text;
    else
        std::cerr << text;

    return result;
}
