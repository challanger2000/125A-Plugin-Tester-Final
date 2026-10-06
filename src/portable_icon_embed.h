#pragma once

#include <windows.h>
#include <cstdint>
#include <iostream>
#include <vector>

namespace PortableIcon {

inline void word(std::vector<std::uint8_t>& d, WORD v) {
    d.push_back(static_cast<std::uint8_t>(v));
    d.push_back(static_cast<std::uint8_t>(v >> 8));
}

inline void dword(std::vector<std::uint8_t>& d, DWORD v) {
    d.push_back(static_cast<std::uint8_t>(v));
    d.push_back(static_cast<std::uint8_t>(v >> 8));
    d.push_back(static_cast<std::uint8_t>(v >> 16));
    d.push_back(static_cast<std::uint8_t>(v >> 24));
}

inline bool embed125AIcon(HANDLE update) {
    constexpr int n = 16;
    constexpr char glyph[n][n + 1] = {
        "................",
        "................",
        ".......##.......",
        "......####......",
        "......#..#......",
        ".....##..##.....",
        ".....#....#.....",
        "....##....##....",
        "....########....",
        "...##########...",
        "...##......##...",
        "...#........#...",
        "..##........##..",
        "..##........##..",
        "................",
        "................"
    };

    constexpr size_t pixelBytes = n * n * 4u;
    constexpr size_t maskBytes = n * 4u;
    std::vector<std::uint8_t> pixels(pixelBytes, 0);
    for (int y = 0; y < n; ++y) {
        for (int x = 0; x < n; ++x) {
            const int dibY = n - 1 - y;
            const size_t p = (static_cast<size_t>(dibY) * n + x) * 4u;
            std::uint8_t r = 35, g = 39, b = 44;
            if (x < 2) { r = 190; g = 38; b = 51; }
            if (glyph[y][x] == '#') { r = 236; g = 238; b = 241; }
            pixels[p + 0] = b; pixels[p + 1] = g; pixels[p + 2] = r; pixels[p + 3] = 255;
        }
    }

    BITMAPINFOHEADER bih{};
    bih.biSize = sizeof(bih); bih.biWidth = n; bih.biHeight = n * 2;
    bih.biPlanes = 1; bih.biBitCount = 32; bih.biCompression = BI_RGB;
    bih.biSizeImage = static_cast<DWORD>(pixelBytes + maskBytes);

    std::vector<std::uint8_t> image;
    const auto* h = reinterpret_cast<const std::uint8_t*>(&bih);
    image.insert(image.end(), h, h + sizeof(bih));
    image.insert(image.end(), pixels.begin(), pixels.end());
    image.resize(image.size() + maskBytes, 0);

    std::vector<std::uint8_t> group;
    word(group, 0); word(group, 1); word(group, 1);
    group.push_back(n); group.push_back(n); group.push_back(0); group.push_back(0);
    word(group, 1); word(group, 32); dword(group, static_cast<DWORD>(image.size())); word(group, 1);

    const WORD lang = MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL);
    if (!UpdateResourceW(update, RT_ICON, MAKEINTRESOURCEW(1), lang,
                         image.data(), static_cast<DWORD>(image.size()))) {
        std::wcerr << L"UpdateResource failed for 125A icon image: " << GetLastError() << L'\n';
        return false;
    }
    if (!UpdateResourceW(update, RT_GROUP_ICON, MAKEINTRESOURCEW(1), lang,
                         group.data(), static_cast<DWORD>(group.size()))) {
        std::wcerr << L"UpdateResource failed for 125A icon group: " << GetLastError() << L'\n';
        return false;
    }
    return true;
}

} // namespace PortableIcon
