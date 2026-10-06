#include <algorithm>
#include <cstddef>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>

#define wmain diagnosticsLauncherMain
#include "launcher_diagnostics.cpp"
#undef wmain

namespace {

std::string readFirstIoFailure(const fs::path& pluginPath) {
    std::ifstream in(ReportPaths::ioEvent(pluginPath), std::ios::binary);
    if (!in)
        return {};

    std::string line;
    constexpr const char* prefix = "FirstFailure: ";
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.rfind(prefix, 0) != 0)
            continue;
        std::string value = line.substr(std::char_traits<char>::length(prefix));
        if (value == "None")
            return {};
        return trimAscii(std::move(value));
    }
    return {};
}

bool integrateExactIoFailure(const fs::path& pluginPath) {
    const std::string firstFailure = readFirstIoFailure(pluginPath);
    if (firstFailure.empty())
        return true;

    const fs::path qaPath = qaReportPath(pluginPath);
    std::ifstream in(qaPath, std::ios::binary);
    if (!in)
        return false;

    std::ostringstream buffer;
    buffer << in.rdbuf();
    std::string text = buffer.str();
    in.close();

    constexpr const char* generic =
        "[FAIL] I/O, sidechain and event isolation - Isolated I/O/event probe detected a deterministic failure";
    const size_t pos = text.find(generic);
    if (pos == std::string::npos)
        return true;

    size_t end = text.find('\n', pos);
    if (end == std::string::npos)
        end = text.size();

    const std::string exact =
        "[FAIL] I/O, sidechain and event isolation - " + firstFailure;
    text.replace(pos, end - pos, exact);

    std::ofstream out(qaPath, std::ios::binary | std::ios::trunc);
    if (!out)
        return false;
    out << text;
    out.flush();
    return static_cast<bool>(out);
}

bool isHiddenLauncherMode(int argc, wchar_t** argv) {
    if (argc != 2 || !argv || !argv[1])
        return false;
    const std::wstring arg(argv[1]);
    return arg.rfind(L"--", 0) == 0;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    const int result = diagnosticsLauncherMain(argc, argv);

    if (argc == 2 && argv && argv[1] && !isHiddenLauncherMode(argc, argv)) {
        const fs::path pluginPath(argv[1]);
        if (!integrateExactIoFailure(pluginPath))
            std::wcerr << L"[WARN] Report annotation - Exact I/O failure detail could not be integrated\n";
    }

    return result;
}
