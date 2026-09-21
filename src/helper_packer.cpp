#include <windows.h>

#include "portable_icon_embed.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

bool readFile(const fs::path& path, std::vector<std::uint8_t>& data) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in)
        return false;
    const std::streamoff size = in.tellg();
    if (size <= 0 || size > static_cast<std::streamoff>(MAXDWORD))
        return false;
    data.resize(static_cast<size_t>(size));
    in.seekg(0, std::ios::beg);
    return static_cast<bool>(in.read(reinterpret_cast<char*>(data.data()), size));
}

bool parseResourceId(const wchar_t* text, int& result) {
    if (!text || !*text)
        return false;
    wchar_t* end = nullptr;
    const long value = wcstol(text, &end, 10);
    if (!end || *end != L'\0' || value <= 0 || value > 65535)
        return false;
    result = static_cast<int>(value);
    return true;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 4 || ((argc - 2) % 2) != 0) {
        std::wcerr << L"Usage: 125A_Helper_Packer.exe <target.exe> <resourceId> <file> [<resourceId> <file> ...]\n";
        return 2;
    }

    const fs::path target(argv[1]);
    if (!fs::exists(target)) {
        std::wcerr << L"Target does not exist: " << target << L'\n';
        return 2;
    }

    HANDLE update = BeginUpdateResourceW(target.c_str(), FALSE);
    if (!update) {
        std::wcerr << L"BeginUpdateResource failed: " << GetLastError() << L'\n';
        return 3;
    }

    bool success = PortableIcon::embed125AIcon(update);
    for (int i = 2; success && i < argc; i += 2) {
        int resourceId = 0;
        if (!parseResourceId(argv[i], resourceId)) {
            std::wcerr << L"Invalid resource id: " << argv[i] << L'\n';
            success = false;
            break;
        }

        const fs::path source(argv[i + 1]);
        std::vector<std::uint8_t> data;
        if (!readFile(source, data)) {
            std::wcerr << L"Could not read helper: " << source << L'\n';
            success = false;
            break;
        }

        if (!UpdateResourceW(update, RT_RCDATA, MAKEINTRESOURCEW(resourceId),
                             MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL),
                             data.data(), static_cast<DWORD>(data.size()))) {
            std::wcerr << L"UpdateResource failed for " << source << L": " << GetLastError() << L'\n';
            success = false;
            break;
        }
    }

    if (!EndUpdateResourceW(update, success ? FALSE : TRUE)) {
        std::wcerr << L"EndUpdateResource failed: " << GetLastError() << L'\n';
        return 4;
    }

    if (!success)
        return 3;

    std::wcout << L"Embedded 125A icon and helper payloads into " << target << L'\n';
    return 0;
}
