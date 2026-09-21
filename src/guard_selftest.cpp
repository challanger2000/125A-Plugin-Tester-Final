#include <windows.h>

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

std::string resolveMode(int argc, char** argv) {
    char buffer[64]{};
    const DWORD length = GetEnvironmentVariableA("125A_GUARD_SELFTEST_MODE", buffer, static_cast<DWORD>(sizeof(buffer)));
    if (length > 0 && length < sizeof(buffer))
        return std::string(buffer, length);

    if (argc == 2)
        return argv[1];

    return {};
}

} // namespace

int main(int argc, char** argv) {
    const std::string mode = resolveMode(argc, argv);
    if (mode.empty()) {
        std::cerr << "usage: 125A_Plugin_Tester_GuardSelfTest.exe <ok|fail|crash|hang>\n";
        return 2;
    }

    if (mode == "ok")
        return 0;
    if (mode == "fail")
        return 1;
    if (mode == "crash") {
        // Deterministic noncontinuable access violation used only to verify launcher crash isolation.
        RaiseException(EXCEPTION_ACCESS_VIOLATION, EXCEPTION_NONCONTINUABLE, 0, nullptr);
        return 3;
    }
    if (mode == "hang") {
        // Deliberately exceed the self-test timeout. The launcher must terminate us.
        Sleep(60000);
        return 4;
    }

    std::cerr << "unknown guard self-test mode\n";
    return 2;
}
