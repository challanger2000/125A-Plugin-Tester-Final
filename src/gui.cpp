#include <windows.h>
#include <commctrl.h>
#include <shobjidl.h>
#include <shellapi.h>

#include "report_paths.h"

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr int kPathEdit = 1001;
constexpr int kSelectPlugin = 1002;
constexpr int kSelectFolder = 1003;
constexpr int kStart = 1004;
constexpr int kOpenReports = 1005;
constexpr int kStatus = 1006;
constexpr int kProgress = 1007;
constexpr int kResults = 1008;
constexpr int kProgressText = 1009;
constexpr UINT kTestDone = WM_APP + 1;
constexpr UINT_PTR kProgressTimer = 1;

HFONT gFont = nullptr;
HFONT gTitleFont = nullptr;
HWND gTitle = nullptr;
HWND gSubtitle = nullptr;
HWND gPath = nullptr;
HWND gSelectPlugin = nullptr;
HWND gSelectFolder = nullptr;
HWND gStart = nullptr;
HWND gOpenReports = nullptr;
HWND gStatus = nullptr;
HWND gProgress = nullptr;
HWND gProgressText = nullptr;
HWND gResults = nullptr;
bool gRunning = false;
bool gFolderScan = false;
int gDpi = 96;

int px(int logical) {
    return MulDiv(logical, gDpi, 96);
}

std::wstring executablePath() {
    std::vector<wchar_t> buffer(32768);
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size())
        return {};
    return std::wstring(buffer.data(), length);
}

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

bool isVst3Path(const fs::path& path) {
    std::wstring extension = path.extension().wstring();
    for (auto& ch : extension)
        ch = static_cast<wchar_t>(std::towlower(ch));
    return extension == L".vst3";
}

std::wstring readControlText(HWND control) {
    const int length = GetWindowTextLengthW(control);
    if (length <= 0)
        return {};
    std::wstring text(static_cast<size_t>(length) + 1u, L'\0');
    const int copied = GetWindowTextW(control, text.data(), length + 1);
    text.resize(copied > 0 ? static_cast<size_t>(copied) : 0u);
    return text;
}

std::wstring utf8ToWide(const std::string& text) {
    if (text.empty())
        return {};
    const int required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                              static_cast<int>(text.size()), nullptr, 0);
    if (required <= 0)
        return {};
    std::wstring result(static_cast<size_t>(required), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
                            result.data(), required) != required)
        return {};
    return result;
}

std::wstring readTextFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return {};
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return utf8ToWide(buffer.str());
}

void deleteFonts() {
    if (gTitleFont) {
        DeleteObject(gTitleFont);
        gTitleFont = nullptr;
    }
    if (gFont) {
        DeleteObject(gFont);
        gFont = nullptr;
    }
}

void createFonts() {
    deleteFonts();
    gFont = CreateFontW(-px(17), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    gTitleFont = CreateFontW(-px(25), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                             DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
}

void applyFont(HWND control, HFONT font = nullptr) {
    if (control && (font || gFont))
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font ? font : gFont), TRUE);
}

HWND makeControl(const wchar_t* className,
                 const wchar_t* text,
                 DWORD style,
                 HWND parent,
                 int id,
                 DWORD exStyle = 0) {
    HWND control = CreateWindowExW(exStyle, className, text, style, 0, 0, 0, 0,
                                   parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                   GetModuleHandleW(nullptr), nullptr);
    applyFont(control);
    return control;
}

void refreshFonts() {
    createFonts();
    applyFont(gTitle, gTitleFont);
    applyFont(gSubtitle);
    applyFont(gPath);
    applyFont(gSelectPlugin);
    applyFont(gSelectFolder);
    applyFont(gStart);
    applyFont(gOpenReports);
    applyFont(gStatus);
    applyFont(gProgressText);
    applyFont(gResults);
}

void setListColumns(const wchar_t* left, const wchar_t* right) {
    if (!gResults)
        return;

    LVCOLUMNW column{};
    column.mask = LVCF_TEXT | LVCF_FMT;
    column.fmt = LVCFMT_LEFT;
    column.pszText = const_cast<wchar_t*>(left);
    if (!ListView_SetColumn(gResults, 0, &column)) {
        column.mask |= LVCF_WIDTH;
        column.cx = px(360);
        ListView_InsertColumn(gResults, 0, &column);
    }

    column.mask = LVCF_TEXT | LVCF_FMT;
    column.fmt = LVCFMT_LEFT;
    column.pszText = const_cast<wchar_t*>(right);
    if (!ListView_SetColumn(gResults, 1, &column)) {
        column.mask |= LVCF_WIDTH;
        column.cx = px(540);
        ListView_InsertColumn(gResults, 1, &column);
    }
}

void layoutControls(HWND window) {
    RECT client{};
    GetClientRect(window, &client);
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
    const int margin = px(20);
    const int gap = px(10);

    MoveWindow(gTitle, margin, px(14), std::max(0, width - 2 * margin), px(30), TRUE);
    MoveWindow(gSubtitle, margin, px(46), std::max(0, width - 2 * margin), px(22), TRUE);

    const int pickerButtonWidth = px(145);
    const int pathY = px(80);
    const int pathH = px(32);
    const int buttonsTotal = pickerButtonWidth * 2 + gap * 2;
    const int pathWidth = std::max(px(220), width - 2 * margin - buttonsTotal);
    MoveWindow(gPath, margin, pathY, pathWidth, pathH, TRUE);
    MoveWindow(gSelectPlugin, margin + pathWidth + gap, pathY, pickerButtonWidth, pathH, TRUE);
    MoveWindow(gSelectFolder, margin + pathWidth + gap + pickerButtonWidth + gap,
               pathY, pickerButtonWidth, pathH, TRUE);

    const int rowY = px(125);
    const int rowH = px(36);
    const int startW = px(155);
    const int reportW = px(145);
    const int statusX = margin + startW + gap + reportW + gap;
    const int remaining = std::max(px(300), width - statusX - margin);
    const int statusW = std::max(px(220), remaining * 45 / 100);
    const int progressX = statusX + statusW + gap;
    const int progressW = std::max(px(80), width - progressX - margin);

    MoveWindow(gStart, margin, rowY, startW, rowH, TRUE);
    MoveWindow(gOpenReports, margin + startW + gap, rowY, reportW, rowH, TRUE);
    MoveWindow(gStatus, statusX, rowY, statusW, rowH, TRUE);
    MoveWindow(gProgress, progressX, rowY + px(6), progressW, px(24), TRUE);

    const int infoY = px(170);
    MoveWindow(gProgressText, margin, infoY, std::max(0, width - 2 * margin), px(24), TRUE);

    const int resultsY = px(202);
    const int resultsHeight = std::max(px(180), height - resultsY - margin);
    MoveWindow(gResults, margin, resultsY, std::max(0, width - 2 * margin), resultsHeight, TRUE);

    const int listWidth = std::max(1, width - 2 * margin - GetSystemMetrics(SM_CXVSCROLL) - px(4));
    ListView_SetColumnWidth(gResults, 0, listWidth * 42 / 100);
    ListView_SetColumnWidth(gResults, 1, listWidth - (listWidth * 42 / 100));
}

bool chooseFolder(HWND owner, const wchar_t* title, fs::path& result) {
    IFileOpenDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&dialog))) || !dialog)
        return false;

    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    dialog->SetTitle(title);

    if (FAILED(dialog->Show(owner))) {
        dialog->Release();
        return false;
    }

    IShellItem* item = nullptr;
    if (FAILED(dialog->GetResult(&item)) || !item) {
        dialog->Release();
        return false;
    }

    PWSTR path = nullptr;
    if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
        result = fs::path(path);
        CoTaskMemFree(path);
    }

    item->Release();
    dialog->Release();
    return !result.empty();
}

bool choosePluginFile(HWND owner, fs::path& result) {
    IFileOpenDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&dialog))) || !dialog)
        return false;

    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions((options & ~FOS_PICKFOLDERS) | FOS_FORCEFILESYSTEM |
                       FOS_PATHMUSTEXIST | FOS_FILEMUSTEXIST);
    dialog->SetTitle(L"VST3 Plug-in-Datei auswählen");

    const COMDLG_FILTERSPEC filters[] = {
        {L"VST3 Plug-ins (*.vst3)", L"*.vst3"},
        {L"Alle Dateien (*.*)", L"*.*"}
    };
    dialog->SetFileTypes(static_cast<UINT>(std::size(filters)), filters);
    dialog->SetFileTypeIndex(1);
    dialog->SetDefaultExtension(L"vst3");

    if (FAILED(dialog->Show(owner))) {
        dialog->Release();
        return false;
    }

    IShellItem* item = nullptr;
    if (FAILED(dialog->GetResult(&item)) || !item) {
        dialog->Release();
        return false;
    }

    PWSTR path = nullptr;
    if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
        result = fs::path(path);
        CoTaskMemFree(path);
    }

    item->Release();
    dialog->Release();
    return !result.empty();
}

void selectPlugin(HWND owner) {
    fs::path selected;
    if (!choosePluginFile(owner, selected))
        return;
    if (!isVst3Path(selected)) {
        MessageBoxW(owner, L"Bitte eine VST3-Datei mit der Endung .vst3 auswählen.",
                    L"125A Plugin Tester", MB_ICONINFORMATION | MB_OK);
        return;
    }
    SetWindowTextW(gPath, selected.c_str());
}

void selectFolder(HWND owner) {
    fs::path selected;
    if (chooseFolder(owner, L"VST3-Ordner auswählen", selected))
        SetWindowTextW(gPath, selected.c_str());
}

void clearResults(const wchar_t* leftHeading, const wchar_t* rightHeading) {
    ListView_DeleteAllItems(gResults);
    setListColumns(leftHeading, rightHeading);
}

void addResultRow(const std::wstring& left, const std::wstring& right) {
    if (!gResults)
        return;
    LVITEMW item{};
    item.mask = LVIF_TEXT;
    item.iItem = ListView_GetItemCount(gResults);
    item.iSubItem = 0;
    item.pszText = const_cast<wchar_t*>(left.c_str());
    const int index = ListView_InsertItem(gResults, &item);
    if (index >= 0)
        ListView_SetItemText(gResults, index, 1, const_cast<wchar_t*>(right.c_str()));
}

void setRunning(HWND window, bool running, bool folderScan) {
    gRunning = running;
    gFolderScan = running && folderScan;
    EnableWindow(gPath, !running);
    EnableWindow(gStart, !running);
    EnableWindow(gSelectPlugin, !running);
    EnableWindow(gSelectFolder, !running);

    if (!running) {
        KillTimer(window, kProgressTimer);
        SendMessageW(gProgress, PBM_SETMARQUEE, FALSE, 0);
        SendMessageW(gProgress, PBM_SETPOS, 0, 0);
        return;
    }

    if (folderScan) {
        SendMessageW(gProgress, PBM_SETMARQUEE, FALSE, 0);
        SendMessageW(gProgress, PBM_SETRANGE32, 0, 1);
        SendMessageW(gProgress, PBM_SETPOS, 0, 0);
        SetTimer(window, kProgressTimer, 400, nullptr);
    } else {
        SendMessageW(gProgress, PBM_SETMARQUEE, TRUE, 35);
    }
}

int runTesterProcess(const fs::path& input) {
    const fs::path guiExe(executablePath());
    const fs::path tester = guiExe.parent_path() / L"125A_Plugin_Tester.exe";
    if (!fs::exists(tester))
        return 127;

    std::wstring command = quoteArg(tester.wstring()) + L" " + quoteArg(input.wstring());
    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(tester.c_str(), mutableCommand.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, tester.parent_path().c_str(), &startup, &process))
        return 127;

    const DWORD waitResult = WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exitCode = 125;
    if (waitResult == WAIT_OBJECT_0 && !GetExitCodeProcess(process.hProcess, &exitCode))
        exitCode = 125;

    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return static_cast<int>(exitCode);
}

std::wstring resultTextFor(const fs::path& input, int exitCode) {
    if (!isVst3Path(input)) {
        const std::wstring text = readTextFile(ReportPaths::folderSummaryTxt());
        if (!text.empty())
            return text;
        return L"Der Ordnerscan wurde beendet, aber der Zusammenfassungsbericht konnte nicht gelesen werden.";
    }

    std::wstring text = readTextFile(ReportPaths::qa(input));
    if (!text.empty())
        return text;
    text = readTextFile(ReportPaths::guard(input));
    if (!text.empty())
        return text;

    std::wostringstream fallback;
    fallback << L"Der Test wurde mit Exitcode " << exitCode
             << L" beendet. Es konnte kein lesbarer QA-Bericht gefunden werden.";
    return fallback.str();
}

int summaryCount(const std::wstring& report, const std::wstring& label) {
    const size_t pos = report.find(label);
    if (pos == std::wstring::npos)
        return -1;
    size_t valuePos = pos + label.size();
    while (valuePos < report.size() && iswspace(report[valuePos]))
        ++valuePos;
    wchar_t* end = nullptr;
    const long value = wcstol(report.c_str() + valuePos, &end, 10);
    if (end == report.c_str() + valuePos)
        return -1;
    return static_cast<int>(value);
}

std::wstring statusFor(const fs::path& input, int exitCode, const std::wstring& report) {
    if (isVst3Path(input)) {
        if (report.find(L"TEST INCONCLUSIVE") != std::wstring::npos)
            return L"INCONCLUSIVE";
        if (report.find(L"[FAIL]") != std::wstring::npos)
            return L"FAIL";
        if (report.find(L"[WARN]") != std::wstring::npos)
            return L"WARNING";
        return exitCode == 0 ? L"PASS" : (exitCode == 1 ? L"FAIL" : L"INCONCLUSIVE");
    }

    const int fail = summaryCount(report, L"FAIL:");
    const int inconclusive = summaryCount(report, L"INCONCLUSIVE:");
    const int warning = summaryCount(report, L"WARNING:");
    if (fail > 0)
        return L"FAIL";
    if (inconclusive > 0)
        return L"INCONCLUSIVE";
    if (warning > 0)
        return L"WARNING";
    if (inconclusive == 0 && fail == 0 && warning == 0)
        return L"PASS";
    return exitCode == 0 ? L"PASS" : L"INCONCLUSIVE";
}

std::wstring trimLine(std::wstring line) {
    while (!line.empty() && (line.back() == L'\r' || line.back() == L'\n'))
        line.pop_back();
    return line;
}

void populatePluginResults(const std::wstring& report) {
    clearResults(L"Prüfung", L"Befund");
    std::wistringstream input(report);
    std::wstring line;
    int rows = 0;
    while (std::getline(input, line)) {
        line = trimLine(std::move(line));
        std::wstring status;
        size_t prefixLength = 0;
        if (line.rfind(L"[PASS] ", 0) == 0) {
            status = L"PASS";
            prefixLength = 7;
        } else if (line.rfind(L"[WARN] ", 0) == 0) {
            status = L"WARNING";
            prefixLength = 7;
        } else if (line.rfind(L"[FAIL] ", 0) == 0) {
            status = L"FAIL";
            prefixLength = 7;
        } else if (line.rfind(L"[INFO] ", 0) == 0) {
            status = L"INFO";
            prefixLength = 7;
        } else {
            continue;
        }

        const std::wstring body = line.substr(prefixLength);
        const size_t separator = body.find(L" - ");
        if (separator == std::wstring::npos) {
            addResultRow(body, status);
        } else {
            const std::wstring test = body.substr(0, separator);
            const std::wstring detail = body.substr(separator + 3);
            addResultRow(test, status + L" – " + detail);
        }
        ++rows;
    }

    if (rows == 0)
        addResultRow(L"Testergebnis", report.empty() ? L"Kein lesbarer Bericht vorhanden" : report);
}

void populateFolderResults(const std::wstring& report) {
    clearResults(L"Plugin", L"Gesamtergebnis");
    std::wistringstream input(report);
    std::wstring line;
    int rows = 0;
    while (std::getline(input, line)) {
        line = trimLine(std::move(line));
        if (line.empty() || line.front() != L'[')
            continue;
        const size_t close = line.find(L"] ");
        if (close == std::wstring::npos)
            continue;
        const std::wstring status = line.substr(1, close - 1);
        std::wstring path = line.substr(close + 2);
        const size_t pipe = path.find(L" | ");
        if (pipe != std::wstring::npos)
            path.resize(pipe);
        fs::path pluginPath(path);
        std::wstring display = pluginPath.filename().wstring();
        if (display.empty())
            display = path;
        addResultRow(display, status);
        ++rows;
    }

    if (rows == 0)
        addResultRow(L"Ordnerscan", report.empty() ? L"Kein lesbarer Zusammenfassungsbericht vorhanden" : report);
}

std::wstring firstFinding(const std::wstring& report, const wchar_t* prefix) {
    const size_t pos = report.find(prefix);
    if (pos == std::wstring::npos)
        return {};
    const size_t start = pos + wcslen(prefix);
    size_t end = report.find_first_of(L"\r\n", start);
    if (end == std::wstring::npos)
        end = report.size();
    return report.substr(start, end - start);
}

struct ProgressInfo {
    bool valid = false;
    int total = 0;
    int current = 0;
    int pass = 0;
    int warning = 0;
    int fail = 0;
    int inconclusive = 0;
    std::wstring plugin;
};

bool parseInt(const std::string& value, int& result) {
    try {
        size_t consumed = 0;
        const int parsed = std::stoi(value, &consumed);
        if (consumed != value.size())
            return false;
        result = parsed;
        return true;
    } catch (...) {
        return false;
    }
}

ProgressInfo readProgress() {
    ProgressInfo info;
    std::ifstream in(ReportPaths::folderProgressTxt(), std::ios::binary);
    if (!in)
        return info;

    std::string line;
    bool hasTotal = false;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        const size_t equals = line.find('=');
        if (equals == std::string::npos)
            continue;
        const std::string key = line.substr(0, equals);
        const std::string value = line.substr(equals + 1);
        if (key == "total") {
            hasTotal = parseInt(value, info.total);
        } else if (key == "current") {
            parseInt(value, info.current);
        } else if (key == "pass") {
            parseInt(value, info.pass);
        } else if (key == "warning") {
            parseInt(value, info.warning);
        } else if (key == "fail") {
            parseInt(value, info.fail);
        } else if (key == "inconclusive") {
            parseInt(value, info.inconclusive);
        } else if (key == "plugin") {
            info.plugin = utf8ToWide(value);
        }
    }
    info.valid = hasTotal && info.total >= 0 && info.current >= 0;
    return info;
}

void refreshProgressDisplay() {
    if (!gRunning || !gFolderScan)
        return;
    const ProgressInfo info = readProgress();
    if (!info.valid)
        return;

    const int total = std::max(1, info.total);
    const int current = std::clamp(info.current, 0, total);
    SendMessageW(gProgress, PBM_SETRANGE32, 0, total);
    SendMessageW(gProgress, PBM_SETPOS, current, 0);

    std::wostringstream text;
    text << info.current << L" / " << info.total;
    if (!info.plugin.empty()) {
        fs::path currentPlugin(info.plugin);
        text << L"   ·   Aktuell: " << currentPlugin.filename().wstring();
    }
    text << L"   ·   PASS " << info.pass
         << L"   ·   WARNING " << info.warning
         << L"   ·   FAIL " << info.fail
         << L"   ·   INCONCLUSIVE " << info.inconclusive;
    SetWindowTextW(gProgressText, text.str().c_str());
}

void startTest(HWND window) {
    if (gRunning)
        return;

    const std::wstring pathText = readControlText(gPath);
    if (pathText.empty()) {
        MessageBoxW(window, L"Bitte zuerst ein VST3-Plug-in oder einen VST3-Ordner auswählen.",
                    L"125A Plugin Tester", MB_ICONINFORMATION | MB_OK);
        return;
    }

    const fs::path input(pathText);
    std::error_code ec;
    if (!fs::exists(input, ec) || ec) {
        MessageBoxW(window, L"Der ausgewählte Pfad existiert nicht.",
                    L"125A Plugin Tester", MB_ICONERROR | MB_OK);
        return;
    }

    const bool folderScan = !isVst3Path(input);
    if (folderScan) {
        fs::remove(ReportPaths::folderProgressTxt(), ec);
        ec.clear();
        fs::remove(ReportPaths::folderSummaryTxt(), ec);
        ec.clear();
        fs::remove(ReportPaths::folderSummaryJson(), ec);
    }

    SetWindowTextW(gStatus, folderScan ? L"TEST LÄUFT – Ordnerscan" : L"TEST LÄUFT – Einzelplugin");
    SetWindowTextW(gProgressText, folderScan
        ? L"VST3-Plug-ins werden ermittelt …"
        : L"Technische Prüfung läuft in isolierten Prozessen …");
    clearResults(folderScan ? L"Plugin" : L"Prüfung",
                 folderScan ? L"Gesamtergebnis" : L"Befund");
    addResultRow(folderScan ? L"Ordnerscan" : L"Plugin", L"Prüfung läuft …");
    setRunning(window, true, folderScan);

    std::thread([window, input]() {
        const int exitCode = runTesterProcess(input);
        auto* result = new std::pair<fs::path, int>(input, exitCode);
        if (!PostMessageW(window, kTestDone, 0, reinterpret_cast<LPARAM>(result)))
            delete result;
    }).detach();
}

void finishTest(HWND window, std::pair<fs::path, int>* result) {
    if (!result)
        return;
    const fs::path input = result->first;
    const int exitCode = result->second;
    delete result;

    const bool folderScan = !isVst3Path(input);
    if (folderScan)
        refreshProgressDisplay();
    setRunning(window, false, folderScan);

    const std::wstring report = resultTextFor(input, exitCode);
    const std::wstring status = statusFor(input, exitCode, report);
    std::wstring statusText = status;
    if (status == L"FAIL") {
        const std::wstring reason = firstFinding(report, L"[FAIL] ");
        if (!reason.empty())
            statusText += L" – " + reason;
    } else if (status == L"WARNING") {
        const std::wstring reason = firstFinding(report, L"[WARN] ");
        if (!reason.empty())
            statusText += L" – " + reason;
    }
    SetWindowTextW(gStatus, statusText.c_str());

    if (folderScan) {
        populateFolderResults(report);
        const int found = summaryCount(report, L"FOUND:");
        const int pass = summaryCount(report, L"PASS:");
        const int warning = summaryCount(report, L"WARNING:");
        const int fail = summaryCount(report, L"FAIL:");
        const int inconclusive = summaryCount(report, L"INCONCLUSIVE:");
        std::wostringstream summary;
        if (found >= 0)
            summary << found << L" Plugins   ·   PASS " << std::max(0, pass)
                    << L"   ·   WARNING " << std::max(0, warning)
                    << L"   ·   FAIL " << std::max(0, fail)
                    << L"   ·   INCONCLUSIVE " << std::max(0, inconclusive);
        else
            summary << L"Ordnerscan beendet";
        SetWindowTextW(gProgressText, summary.str().c_str());
        if (found > 0) {
            SendMessageW(gProgress, PBM_SETRANGE32, 0, found);
            SendMessageW(gProgress, PBM_SETPOS, found, 0);
        }
    } else {
        populatePluginResults(report);
        SetWindowTextW(gProgressText, L"Einzelprüfung abgeschlossen. Vollständige TXT/JSON-Berichte liegen im Report-Ordner.");
    }

    if (exitCode == 127) {
        MessageBoxW(window, L"Die technische Tester-EXE wurde neben der GUI nicht gefunden oder konnte nicht gestartet werden.",
                    L"125A Plugin Tester", MB_ICONERROR | MB_OK);
    }
}

LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_CREATE: {
            HDC screen = GetDC(nullptr);
            if (screen) {
                gDpi = std::max(96, GetDeviceCaps(screen, LOGPIXELSX));
                ReleaseDC(nullptr, screen);
            }
            createFonts();

            gTitle = makeControl(L"STATIC", L"125A Plugin Tester / Quality Checker",
                                 WS_CHILD | WS_VISIBLE, window, 0);
            applyFont(gTitle, gTitleFont);
            gSubtitle = makeControl(L"STATIC", L"VST3 validation · stress testing · release QA",
                                    WS_CHILD | WS_VISIBLE, window, 0);
            gPath = makeControl(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                                window, kPathEdit);
            gSelectPlugin = makeControl(L"BUTTON", L"VST3 auswählen",
                                        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, window, kSelectPlugin);
            gSelectFolder = makeControl(L"BUTTON", L"Ordner auswählen",
                                        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, window, kSelectFolder);
            gStart = makeControl(L"BUTTON", L"TEST STARTEN",
                                 WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, window, kStart);
            gOpenReports = makeControl(L"BUTTON", L"Report-Ordner",
                                       WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, window, kOpenReports);
            gStatus = makeControl(L"STATIC", L"BEREIT",
                                  WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE | SS_LEFT, window, kStatus);
            gProgress = makeControl(PROGRESS_CLASSW, L"", WS_CHILD | WS_VISIBLE,
                                    window, kProgress);
            gProgressText = makeControl(L"STATIC",
                                        L"Bereit. Wähle ein einzelnes .vst3 oder einen kompletten VST3-Ordner.",
                                        WS_CHILD | WS_VISIBLE | SS_LEFT, window, kProgressText);
            gResults = makeControl(WC_LISTVIEWW, L"",
                                   WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SINGLESEL |
                                       LVS_SHOWSELALWAYS,
                                   window, kResults, WS_EX_CLIENTEDGE);
            ListView_SetExtendedListViewStyle(gResults,
                                              LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
            setListColumns(L"Prüfung", L"Befund");
            addResultRow(L"Status", L"Bereit");
            layoutControls(window);
            return 0;
        }

        case WM_SIZE:
            layoutControls(window);
            return 0;

        case WM_GETMINMAXINFO: {
            auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
            if (info) {
                info->ptMinTrackSize.x = px(900);
                info->ptMinTrackSize.y = px(600);
            }
            return 0;
        }

        case WM_TIMER:
            if (wParam == kProgressTimer) {
                refreshProgressDisplay();
                return 0;
            }
            break;

        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case kSelectPlugin: selectPlugin(window); return 0;
                case kSelectFolder: selectFolder(window); return 0;
                case kStart: startTest(window); return 0;
                case kOpenReports: {
                    std::error_code ec;
                    if (ReportPaths::ensureRoot(ec))
                        ShellExecuteW(window, L"open", ReportPaths::root().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                    return 0;
                }
                default: break;
            }
            break;

        case kTestDone:
            finishTest(window, reinterpret_cast<std::pair<fs::path, int>*>(lParam));
            return 0;

        case WM_CLOSE:
            if (gRunning) {
                MessageBoxW(window,
                            L"Der laufende Test wird zuerst abgeschlossen. Danach kann das Fenster geschlossen werden.",
                            L"125A Plugin Tester", MB_ICONINFORMATION | MB_OK);
                return 0;
            }
            DestroyWindow(window);
            return 0;

        case WM_DESTROY:
            KillTimer(window, kProgressTimer);
            deleteFonts();
            PostQuitMessage(0);
            return 0;

        default:
            break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    SetProcessDPIAware();

    INITCOMMONCONTROLSEX controls{};
    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_PROGRESS_CLASS | ICC_LISTVIEW_CLASSES;
    InitCommonControlsEx(&controls);

    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    HDC screen = GetDC(nullptr);
    int dpi = 96;
    if (screen) {
        dpi = std::max(96, GetDeviceCaps(screen, LOGPIXELSX));
        ReleaseDC(nullptr, screen);
    }

    const wchar_t* className = L"125APluginTesterWindow";
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = windowProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = className;
    if (!RegisterClassExW(&wc)) {
        if (SUCCEEDED(comResult))
            CoUninitialize();
        return 1;
    }

    const int windowWidth = MulDiv(1040, dpi, 96);
    const int windowHeight = MulDiv(720, dpi, 96);
    HWND window = CreateWindowExW(0, className, L"125A Plugin Tester / Quality Checker",
                                  WS_OVERLAPPEDWINDOW,
                                  CW_USEDEFAULT, CW_USEDEFAULT, windowWidth, windowHeight,
                                  nullptr, nullptr, instance, nullptr);
    if (!window) {
        if (SUCCEEDED(comResult))
            CoUninitialize();
        return 1;
    }

    ShowWindow(window, showCommand);
    UpdateWindow(window);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    if (SUCCEEDED(comResult))
        CoUninitialize();
    return static_cast<int>(message.wParam);
}
