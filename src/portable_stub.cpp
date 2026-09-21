#include <windows.h>
#include <shellapi.h>

#include "embedded_helper_ids.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace {

struct EmbeddedFile {
    int resourceId;
    const wchar_t* fileName;
};

constexpr EmbeddedFile kFiles[] = {
    {EmbeddedHelpers::kGui, L"125A_Plugin_Tester_GUI.exe"},
    {EmbeddedHelpers::kDispatcher, L"125A_Plugin_Tester.exe"},
    {EmbeddedHelpers::kSingle, L"125A_Plugin_Tester_Single.exe"},
    {EmbeddedHelpers::kWorker, L"125A_Plugin_Tester_Worker.exe"},
    {EmbeddedHelpers::kReloadProbe, L"125A_Plugin_Tester_ReloadProbe.exe"},
    {EmbeddedHelpers::kStateProbe, L"125A_Plugin_Tester_StateProbe.exe"},
    {EmbeddedHelpers::kIOEventProbe, L"125A_Plugin_Tester_IOEventProbe.exe"},
    {EmbeddedHelpers::kValidator, L"125A_Plugin_Tester_SteinbergValidator.exe"},
    {EmbeddedHelpers::kValidatorCore, L"125A_Plugin_Tester_SteinbergValidatorCore.exe"},
    {EmbeddedHelpers::kEditorLifecycleProbe, L"125A_Plugin_Tester_EditorLifecycleProbe.exe"},
};

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

fs::path makeRuntimeDirectory() {
    std::vector<wchar_t> buffer(32768, L'\0');
    const DWORD length = GetTempPathW(static_cast<DWORD>(buffer.size()), buffer.data());
    if (length == 0 || length >= buffer.size())
        return {};

    fs::path root(std::wstring(buffer.data(), length));
    root /= L"125A Plugin Tester";
    root /= L"Runtime";
    root /= std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
    return root;
}

bool writeResourceToFile(HMODULE module, int resourceId, const fs::path& destination) {
    HRSRC resource = FindResourceW(module, MAKEINTRESOURCEW(resourceId), RT_RCDATA);
    if (!resource)
        return false;

    const DWORD size = SizeofResource(module, resource);
    if (size == 0)
        return false;

    HGLOBAL loaded = LoadResource(module, resource);
    if (!loaded)
        return false;

    const void* data = LockResource(loaded);
    if (!data)
        return false;

    HANDLE file = CreateFileW(destination.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;

    const auto* bytes = static_cast<const unsigned char*>(data);
    DWORD totalWritten = 0;
    bool ok = true;
    while (totalWritten < size) {
        DWORD written = 0;
        const DWORD remaining = size - totalWritten;
        if (!WriteFile(file, bytes + totalWritten, remaining, &written, nullptr) || written == 0) {
            ok = false;
            break;
        }
        totalWritten += written;
    }

    if (ok)
        ok = FlushFileBuffers(file) != FALSE;
    CloseHandle(file);
    return ok && totalWritten == size;
}

bool extractRuntime(const fs::path& runtimeDir) {
    std::error_code ec;
    fs::create_directories(runtimeDir, ec);
    if (ec)
        return false;

    HMODULE module = GetModuleHandleW(nullptr);
    if (!module)
        return false;

    for (const auto& file : kFiles) {
        if (!writeResourceToFile(module, file.resourceId, runtimeDir / file.fileName))
            return false;
    }
    return true;
}

int runChild(const fs::path& executable,
             const std::vector<std::wstring>& arguments,
             bool hidden) {
    std::wstring command = quoteArg(executable.wstring());
    for (const auto& argument : arguments) {
        command.push_back(L' ');
        command += quoteArg(argument);
    }

    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    const DWORD flags = hidden ? CREATE_NO_WINDOW : 0;

    if (!CreateProcessW(executable.c_str(), mutableCommand.data(), nullptr, nullptr, FALSE,
                        flags, nullptr, executable.parent_path().c_str(), &startup, &process)) {
        return 127;
    }

    const DWORD waitResult = WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exitCode = 125;
    if (waitResult == WAIT_OBJECT_0) {
        if (!GetExitCodeProcess(process.hProcess, &exitCode))
            exitCode = 125;
    } else {
        TerminateProcess(process.hProcess, 125);
        WaitForSingleObject(process.hProcess, 5000);
    }

    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return static_cast<int>(exitCode);
}

std::vector<std::wstring> commandLineArguments() {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::vector<std::wstring> result;
    if (!argv)
        return result;
    for (int i = 1; i < argc; ++i)
        result.emplace_back(argv[i]);
    LocalFree(argv);
    return result;
}

void cleanupRuntime(const fs::path& runtimeDir) {
    std::error_code ec;
    fs::remove_all(runtimeDir, ec);
    fs::path runtimeRoot = runtimeDir.parent_path();
    if (!runtimeRoot.empty()) {
        fs::remove(runtimeRoot, ec);
        const fs::path productRoot = runtimeRoot.parent_path();
        if (!productRoot.empty())
            fs::remove(productRoot, ec);
    }
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    const auto arguments = commandLineArguments();
    const bool portableTest = arguments.size() == 2 && arguments[0] == L"--portable-test";

    const fs::path runtimeDir = makeRuntimeDirectory();
    if (runtimeDir.empty() || !extractRuntime(runtimeDir)) {
        if (!portableTest) {
            MessageBoxW(nullptr,
                        L"Die internen 125A-Testmodule konnten nicht in das temporäre Laufzeitverzeichnis entpackt werden.",
                        L"125A Plugin Tester", MB_ICONERROR | MB_OK);
        }
        cleanupRuntime(runtimeDir);
        return 127;
    }

    int result = 127;
    if (portableTest) {
        result = runChild(runtimeDir / L"125A_Plugin_Tester.exe", {arguments[1]}, true);
    } else {
        result = runChild(runtimeDir / L"125A_Plugin_Tester_GUI.exe", {}, false);
    }

    cleanupRuntime(runtimeDir);
    return result;
}
