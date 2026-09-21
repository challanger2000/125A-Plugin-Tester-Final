#define wWinMain legacyGuiEntry
#define windowProc legacyWindowProc
#include "gui.cpp"
#undef windowProc
#undef wWinMain

#include <iterator>
#include <uxtheme.h>

namespace {

constexpr COLORREF kBg = RGB(20, 22, 25);
constexpr COLORREF kPanel = RGB(28, 31, 35);
constexpr COLORREF kPanelAlt = RGB(31, 34, 38);
constexpr COLORREF kPanelRaised = RGB(35, 39, 44);
constexpr COLORREF kBorder = RGB(61, 66, 73);
constexpr COLORREF kGrid = RGB(49, 53, 59);
constexpr COLORREF kText = RGB(236, 238, 241);
constexpr COLORREF kMuted = RGB(151, 158, 168);
constexpr COLORREF kAccent = RGB(190, 38, 51);
constexpr COLORREF kAccentHot = RGB(216, 48, 63);
constexpr COLORREF kPass = RGB(89, 184, 122);
constexpr COLORREF kWarn = RGB(225, 174, 73);
constexpr COLORREF kFail = RGB(232, 83, 91);
constexpr COLORREF kInfo = RGB(116, 164, 214);

HBRUSH gBgBrush = nullptr;
HBRUSH gPanelBrush = nullptr;
HBRUSH gEditBrush = nullptr;
HICON gAppIcon = nullptr;

void deleteThemeResources() {
    if (gAppIcon) { DestroyIcon(gAppIcon); gAppIcon = nullptr; }
    if (gBgBrush) { DeleteObject(gBgBrush); gBgBrush = nullptr; }
    if (gPanelBrush) { DeleteObject(gPanelBrush); gPanelBrush = nullptr; }
    if (gEditBrush) { DeleteObject(gEditBrush); gEditBrush = nullptr; }
}

void createThemeBrushes() {
    if (gBgBrush) DeleteObject(gBgBrush);
    if (gPanelBrush) DeleteObject(gPanelBrush);
    if (gEditBrush) DeleteObject(gEditBrush);
    gBgBrush = CreateSolidBrush(kBg);
    gPanelBrush = CreateSolidBrush(kPanel);
    gEditBrush = CreateSolidBrush(kPanelRaised);
}

HICON create125AIcon() {
    constexpr int size = 32;
    HDC screen = GetDC(nullptr);
    if (!screen) return nullptr;
    HDC dc = CreateCompatibleDC(screen);
    HBITMAP color = CreateCompatibleBitmap(screen, size, size);
    HBITMAP mask = CreateBitmap(size, size, 1, 1, nullptr);
    ReleaseDC(nullptr, screen);
    if (!dc || !color || !mask) {
        if (dc) DeleteDC(dc);
        if (color) DeleteObject(color);
        if (mask) DeleteObject(mask);
        return nullptr;
    }

    HGDIOBJ old = SelectObject(dc, color);
    RECT r{0, 0, size, size};
    HBRUSH bg = CreateSolidBrush(kPanelRaised);
    FillRect(dc, &r, bg);
    DeleteObject(bg);
    HBRUSH accent = CreateSolidBrush(kAccent);
    RECT bar{0, 0, 4, size};
    FillRect(dc, &bar, accent);
    DeleteObject(accent);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, kText);
    HFONT font = CreateFontW(-19, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                             DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HGDIOBJ oldFont = SelectObject(dc, font);
    DrawTextW(dc, L"A", -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(dc, oldFont);
    DeleteObject(font);
    SelectObject(dc, old);
    DeleteDC(dc);

    ICONINFO info{};
    info.fIcon = TRUE;
    info.hbmMask = mask;
    info.hbmColor = color;
    HICON icon = CreateIconIndirect(&info);
    DeleteObject(mask);
    DeleteObject(color);
    return icon;
}

void enableImmersiveDarkTitleBar(HWND window) {
    HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
    if (!dwm) return;
    using Fn = HRESULT (WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
    auto setAttribute = reinterpret_cast<Fn>(GetProcAddress(dwm, "DwmSetWindowAttribute"));
    if (setAttribute) {
        const BOOL enabled = TRUE;
        constexpr DWORD dark20 = 20, dark19 = 19;
        if (FAILED(setAttribute(window, dark20, &enabled, sizeof(enabled))))
            setAttribute(window, dark19, &enabled, sizeof(enabled));
    }
    FreeLibrary(dwm);
}

void makeOwnerDrawButton(HWND button) {
    if (!button) return;
    LONG_PTR style = GetWindowLongPtrW(button, GWL_STYLE);
    style = (style & ~static_cast<LONG_PTR>(0x0F)) | BS_OWNERDRAW;
    SetWindowLongPtrW(button, GWL_STYLE, style);
    InvalidateRect(button, nullptr, TRUE);
}

void applyProfessionalTheme(HWND window) {
    enableImmersiveDarkTitleBar(window);
    for (HWND button : {gSelectPlugin, gSelectFolder, gStart, gOpenReports})
        makeOwnerDrawButton(button);

    if (gResults) {
        ListView_SetBkColor(gResults, kPanel);
        ListView_SetTextBkColor(gResults, kPanel);
        ListView_SetTextColor(gResults, kText);
        DWORD ex = ListView_GetExtendedListViewStyle(gResults);
        ex &= ~LVS_EX_GRIDLINES;
        ex |= LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER;
        ListView_SetExtendedListViewStyle(gResults, ex);
        SetWindowTheme(gResults, L"", L"");
        HWND header = ListView_GetHeader(gResults);
        if (header) SetWindowTheme(header, L"", L"");
    }
    if (gProgress) {
        SetWindowTheme(gProgress, L"", L"");
        SendMessageW(gProgress, PBM_SETBKCOLOR, 0, static_cast<LPARAM>(kPanelRaised));
        SendMessageW(gProgress, PBM_SETBARCOLOR, 0, static_cast<LPARAM>(kAccent));
    }
    InvalidateRect(window, nullptr, TRUE);
}

COLORREF statusColor() {
    const std::wstring text = readControlText(gStatus);
    if (text.rfind(L"PASS", 0) == 0) return kPass;
    if (text.rfind(L"FAIL", 0) == 0) return kFail;
    if (text.rfind(L"WARNING", 0) == 0 || text.rfind(L"INCONCLUSIVE", 0) == 0) return kWarn;
    if (text.rfind(L"TEST LÄUFT", 0) == 0) return kInfo;
    return kMuted;
}

void drawButton(const DRAWITEMSTRUCT& draw) {
    RECT rect = draw.rcItem;
    const bool disabled = (draw.itemState & ODS_DISABLED) != 0;
    const bool pressed = (draw.itemState & ODS_SELECTED) != 0;
    const bool focused = (draw.itemState & ODS_FOCUS) != 0;
    const bool start = draw.CtlID == kStart;
    COLORREF fill = start ? kAccent : kPanelRaised;
    COLORREF border = start ? kAccent : kBorder;
    if (pressed) fill = start ? RGB(151, 30, 41) : RGB(45, 49, 55);
    else if (focused) border = start ? kAccentHot : RGB(94, 101, 111);
    HBRUSH fillBrush = CreateSolidBrush(fill);
    HPEN borderPen = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ oldBrush = SelectObject(draw.hDC, fillBrush);
    HGDIOBJ oldPen = SelectObject(draw.hDC, borderPen);
    RoundRect(draw.hDC, rect.left, rect.top, rect.right, rect.bottom, px(5), px(5));
    SelectObject(draw.hDC, oldPen); SelectObject(draw.hDC, oldBrush);
    DeleteObject(borderPen); DeleteObject(fillBrush);
    wchar_t label[256]{};
    GetWindowTextW(draw.hwndItem, label, static_cast<int>(std::size(label)));
    SetBkMode(draw.hDC, TRANSPARENT);
    SetTextColor(draw.hDC, disabled ? RGB(105, 109, 114) : kText);
    HFONT font = reinterpret_cast<HFONT>(SendMessageW(draw.hwndItem, WM_GETFONT, 0, 0));
    HGDIOBJ oldFont = font ? SelectObject(draw.hDC, font) : nullptr;
    DrawTextW(draw.hDC, label, -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    if (oldFont) SelectObject(draw.hDC, oldFont);
}

void drawWindowBackground(HWND window) {
    PAINTSTRUCT paint{};
    HDC dc = BeginPaint(window, &paint);
    RECT client{}; GetClientRect(window, &client);
    FillRect(dc, &client, gBgBrush);
    HBRUSH accentBrush = CreateSolidBrush(kAccent);
    RECT accent{0, 0, client.right, px(4)}; FillRect(dc, &accent, accentBrush); DeleteObject(accentBrush);
    HBRUSH borderBrush = CreateSolidBrush(kBorder);
    RECT separator{px(20), px(72), client.right - px(20), px(73)}; FillRect(dc, &separator, borderBrush);
    DeleteObject(borderBrush);
    EndPaint(window, &paint);
}

LRESULT handleListCustomDraw(LPARAM lParam) {
    auto* custom = reinterpret_cast<NMLVCUSTOMDRAW*>(lParam);
    if (!custom) return CDRF_DODEFAULT;
    if (custom->nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
    if (custom->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
        const int row = static_cast<int>(custom->nmcd.dwItemSpec);
        custom->clrTextBk = (row % 2 == 0) ? kPanel : kPanelAlt;
        custom->clrText = kText;
        return CDRF_NOTIFYSUBITEMDRAW;
    }
    if (custom->nmcd.dwDrawStage == (CDDS_ITEMPREPAINT | CDDS_SUBITEM)) {
        if (custom->iSubItem == 1) {
            wchar_t text[1024]{};
            ListView_GetItemText(gResults, static_cast<int>(custom->nmcd.dwItemSpec), 1, text,
                                 static_cast<int>(std::size(text)));
            const std::wstring value(text);
            if (value.rfind(L"PASS", 0) == 0) custom->clrText = kPass;
            else if (value.rfind(L"WARNING", 0) == 0) custom->clrText = kWarn;
            else if (value.rfind(L"FAIL", 0) == 0) custom->clrText = kFail;
            else if (value.rfind(L"INFO", 0) == 0) custom->clrText = kInfo;
        }
        return CDRF_DODEFAULT;
    }
    return CDRF_DODEFAULT;
}

LRESULT handleHeaderCustomDraw(LPARAM lParam) {
    auto* custom = reinterpret_cast<NMCUSTOMDRAW*>(lParam);
    if (!custom) return CDRF_DODEFAULT;
    if (custom->dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
    if (custom->dwDrawStage != CDDS_ITEMPREPAINT) return CDRF_DODEFAULT;

    HWND header = custom->hdr.hwndFrom;
    const int index = static_cast<int>(custom->dwItemSpec);
    RECT rect{};
    if (!Header_GetItemRect(header, index, &rect)) return CDRF_DODEFAULT;
    HBRUSH fill = CreateSolidBrush(kPanelRaised);
    FillRect(custom->hdc, &rect, fill); DeleteObject(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, kBorder);
    HGDIOBJ oldPen = SelectObject(custom->hdc, pen);
    MoveToEx(custom->hdc, rect.right - 1, rect.top, nullptr);
    LineTo(custom->hdc, rect.right - 1, rect.bottom);
    MoveToEx(custom->hdc, rect.left, rect.bottom - 1, nullptr);
    LineTo(custom->hdc, rect.right, rect.bottom - 1);
    SelectObject(custom->hdc, oldPen); DeleteObject(pen);

    wchar_t text[256]{};
    HDITEMW item{}; item.mask = HDI_TEXT; item.pszText = text; item.cchTextMax = static_cast<int>(std::size(text));
    Header_GetItem(header, index, &item);
    RECT textRect = rect; textRect.left += px(8); textRect.right -= px(8);
    SetBkMode(custom->hdc, TRANSPARENT); SetTextColor(custom->hdc, kText);
    HFONT font = reinterpret_cast<HFONT>(SendMessageW(header, WM_GETFONT, 0, 0));
    HGDIOBJ oldFont = font ? SelectObject(custom->hdc, font) : nullptr;
    DrawTextW(custom->hdc, text, -1, &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    if (oldFont) SelectObject(custom->hdc, oldFont);
    return CDRF_SKIPDEFAULT;
}

LRESULT CALLBACK professionalWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_CREATE: {
            const LRESULT result = legacyWindowProc(window, message, wParam, lParam);
            applyProfessionalTheme(window);
            return result;
        }
        case WM_ERASEBKGND:
            if (gBgBrush) { RECT client{}; GetClientRect(window, &client); FillRect(reinterpret_cast<HDC>(wParam), &client, gBgBrush); return 1; }
            break;
        case WM_PAINT: drawWindowBackground(window); return 0;
        case WM_DRAWITEM:
            if (lParam) {
                const auto& draw = *reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);
                if (draw.CtlType == ODT_BUTTON) { drawButton(draw); return TRUE; }
            }
            break;
        case WM_CTLCOLORSTATIC: {
            HDC dc = reinterpret_cast<HDC>(wParam); HWND control = reinterpret_cast<HWND>(lParam);
            SetBkMode(dc, TRANSPARENT);
            if (control == gTitle) SetTextColor(dc, kText);
            else if (control == gStatus) SetTextColor(dc, statusColor());
            else if (control == gSubtitle || control == gProgressText) SetTextColor(dc, kMuted);
            else SetTextColor(dc, kText);
            return reinterpret_cast<LRESULT>(gBgBrush);
        }
        case WM_CTLCOLOREDIT: {
            HDC dc = reinterpret_cast<HDC>(wParam); SetTextColor(dc, kText); SetBkColor(dc, kPanelRaised);
            return reinterpret_cast<LRESULT>(gEditBrush);
        }
        case WM_NOTIFY: {
            auto* hdr = reinterpret_cast<NMHDR*>(lParam);
            if (hdr && hdr->code == NM_CUSTOMDRAW) {
                if (hdr->hwndFrom == gResults) return handleListCustomDraw(lParam);
                if (gResults && hdr->hwndFrom == ListView_GetHeader(gResults)) return handleHeaderCustomDraw(lParam);
            }
            break;
        }
        case kTestDone: {
            const LRESULT result = legacyWindowProc(window, message, wParam, lParam);
            InvalidateRect(gStatus, nullptr, TRUE); InvalidateRect(gResults, nullptr, TRUE);
            return result;
        }
        case WM_DESTROY: {
            const LRESULT result = legacyWindowProc(window, message, wParam, lParam);
            deleteThemeResources(); return result;
        }
        default: break;
    }
    return legacyWindowProc(window, message, wParam, lParam);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    SetProcessDPIAware();
    INITCOMMONCONTROLSEX controls{}; controls.dwSize = sizeof(controls); controls.dwICC = ICC_PROGRESS_CLASS | ICC_LISTVIEW_CLASSES;
    InitCommonControlsEx(&controls);
    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    createThemeBrushes();
    gAppIcon = create125AIcon();

    HDC screen = GetDC(nullptr); int dpi = 96;
    if (screen) { dpi = std::max(96, GetDeviceCaps(screen, LOGPIXELSX)); ReleaseDC(nullptr, screen); }
    const wchar_t* className = L"125APluginTesterWindowFinal";
    WNDCLASSEXW wc{}; wc.cbSize = sizeof(wc); wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = professionalWindowProc; wc.hInstance = instance; wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = gAppIcon ? gAppIcon : LoadIconW(nullptr, IDI_APPLICATION);
    wc.hIconSm = wc.hIcon; wc.hbrBackground = gBgBrush; wc.lpszClassName = className;
    if (!RegisterClassExW(&wc)) {
        deleteThemeResources(); if (SUCCEEDED(comResult)) CoUninitialize(); return 1;
    }

    const int windowWidth = MulDiv(1100, dpi, 96), windowHeight = MulDiv(760, dpi, 96);
    HWND window = CreateWindowExW(0, className, L"125A Plugin Tester / Quality Checker", WS_OVERLAPPEDWINDOW,
                                  CW_USEDEFAULT, CW_USEDEFAULT, windowWidth, windowHeight,
                                  nullptr, nullptr, instance, nullptr);
    if (!window) {
        deleteThemeResources(); if (SUCCEEDED(comResult)) CoUninitialize(); return 1;
    }
    ShowWindow(window, showCommand); UpdateWindow(window);
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) { TranslateMessage(&message); DispatchMessageW(&message); }
    if (SUCCEEDED(comResult)) CoUninitialize();
    return static_cast<int>(message.wParam);
}
