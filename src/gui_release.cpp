#include <windows.h>
#include <commctrl.h>
#include <uxtheme.h>
#include <iterator>

namespace {
constexpr UINT_PTR kHeaderSubclassId = 125001;
constexpr COLORREF kHeaderBg = RGB(35,39,44);
constexpr COLORREF kHeaderBorder = RGB(61,66,73);
constexpr COLORREF kHeaderText = RGB(236,238,241);

void paint125AHeader(HWND header) {
    PAINTSTRUCT ps{};
    HDC dc = BeginPaint(header, &ps);
    if (!dc) return;
    RECT client{}; GetClientRect(header, &client);
    HBRUSH bg = CreateSolidBrush(kHeaderBg); FillRect(dc, &client, bg); DeleteObject(bg);
    HFONT font = reinterpret_cast<HFONT>(SendMessageW(header, WM_GETFONT, 0, 0));
    HGDIOBJ oldFont = font ? SelectObject(dc, font) : nullptr;
    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, kHeaderText);
    HPEN pen = CreatePen(PS_SOLID, 1, kHeaderBorder); HGDIOBJ oldPen = SelectObject(dc, pen);
    const int count = Header_GetItemCount(header);
    for (int i=0; i<count; ++i) {
        RECT r{}; if (!Header_GetItemRect(header, i, &r)) continue;
        wchar_t text[256]{};
        HDITEMW item{}; item.mask = HDI_TEXT | HDI_FORMAT; item.pszText = text; item.cchTextMax = static_cast<int>(std::size(text));
        Header_GetItem(header, i, &item);
        RECT tr = r; tr.left += 8; tr.right -= 8;
        UINT fmt = DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS;
        fmt |= (item.fmt & HDF_RIGHT) ? DT_RIGHT : ((item.fmt & HDF_CENTER) ? DT_CENTER : DT_LEFT);
        DrawTextW(dc, text, -1, &tr, fmt);
        MoveToEx(dc, r.right-1, r.top, nullptr); LineTo(dc, r.right-1, r.bottom);
    }
    MoveToEx(dc, client.left, client.bottom-1, nullptr); LineTo(dc, client.right, client.bottom-1);
    SelectObject(dc, oldPen); DeleteObject(pen); if (oldFont) SelectObject(dc, oldFont); EndPaint(header, &ps);
}

LRESULT CALLBACK headerSubclassProc(HWND header, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR) {
    switch (message) {
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: paint125AHeader(header); return 0;
        case WM_THEMECHANGED:
        case WM_SYSCOLORCHANGE:
        case WM_SETTINGCHANGE: InvalidateRect(header, nullptr, TRUE); break;
        case WM_NCDESTROY: RemoveWindowSubclass(header, headerSubclassProc, kHeaderSubclassId); break;
        default: break;
    }
    return DefSubclassProc(header, message, wParam, lParam);
}

HWND hookedListViewGetHeader(HWND list) {
    HWND header = reinterpret_cast<HWND>(SendMessageW(list, LVM_GETHEADER, 0, 0));
    if (header) {
        SetWindowTheme(header, L"", L"");
        SetWindowSubclass(header, headerSubclassProc, kHeaderSubclassId, 0);
    }
    return header;
}
}

#ifdef ListView_GetHeader
#undef ListView_GetHeader
#endif
#define ListView_GetHeader hookedListViewGetHeader
#include "gui_final.cpp"
#undef ListView_GetHeader
