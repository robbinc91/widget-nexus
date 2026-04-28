#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <uxtheme.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

struct Widget {
    std::string name;
    bool alwaysVisible = false;
    std::vector<std::string> commands;
};

struct WidgetRow {
    Widget w;
    HWND floater = nullptr;
};

static std::vector<WidgetRow> g_rows;
static bool g_showNonPinned = true;
static int g_selectedModelIndex = -1;
static std::string g_configPath = "widgets.txt";
static HINSTANCE g_hInstance = nullptr;
static HWND g_nexusHwnd = nullptr;
static bool g_trayIconAdded = false;

static constexpr UINT WM_APP_TRAY = WM_USER + 101;
static constexpr const char FLOATER_CLASS_NAME[] = "NexusFloaterWnd";

// Circular floater + matrix layout (top-right: column fills top→bottom, columns progress right→left)
static constexpr int kFloaterDiameter = 76;
static constexpr int kFloaterDragRingPx = 12;
static constexpr int kFloaterGap = 8;
static constexpr int kFloaterMargin = 10;
static constexpr BYTE kFloaterAlpha = 200; // layered window opacity (0 = invisible, 255 = opaque)

enum ControlIds {
    IDC_SHOW_NON_PINNED = 101,
    IDC_LIST_WIDGETS = 102,
    IDC_BTN_RUN = 103,
    IDC_BTN_ADD = 104,
    IDC_BTN_DELETE = 105,
    IDC_BTN_SAVE = 106,
    IDC_EDIT_NAME = 107,
    IDC_CHECK_ALWAYS = 108,
    IDC_EDIT_COMMANDS = 109,
    IDC_STATIC_STATUS = 110,
    IDC_BTN_HIDE_NEXUS = 111
};

// Neon palette (dark cyber / synthwave)
static constexpr COLORREF kRgbDeep0 = RGB(6, 8, 18);
static constexpr COLORREF kRgbPanel = RGB(14, 18, 40);
static constexpr COLORREF kRgbEdit = RGB(10, 14, 32);
static constexpr COLORREF kRgbNeonCyan = RGB(0, 245, 255);
static constexpr COLORREF kRgbNeonMagenta = RGB(255, 0, 200);
static constexpr COLORREF kRgbNeonDim = RGB(0, 140, 180);
static constexpr COLORREF kRgbTextHi = RGB(220, 255, 255);
static constexpr COLORREF kRgbTextLo = RGB(140, 180, 200);

static HBRUSH g_brDeep = nullptr;
static HBRUSH g_brPanel = nullptr;
static HBRUSH g_brEdit = nullptr;
static HBRUSH g_brListSel = nullptr;
static HBRUSH g_brBtn = nullptr;
static HBRUSH g_brBtnHot = nullptr;
static HPEN g_penFrame = nullptr;
static HPEN g_penGlow = nullptr;
static HFONT g_fontTitle = nullptr;
static HFONT g_fontUi = nullptr;
static HFONT g_fontMono = nullptr;
static int g_listItemHeight = 40;

static void ThemeCreate() {
    if (g_brDeep) return;
    g_brDeep = CreateSolidBrush(kRgbDeep0);
    g_brPanel = CreateSolidBrush(kRgbPanel);
    g_brEdit = CreateSolidBrush(kRgbEdit);
    g_brListSel = CreateSolidBrush(RGB(24, 36, 72));
    g_brBtn = CreateSolidBrush(RGB(18, 26, 52));
    g_brBtnHot = CreateSolidBrush(RGB(28, 40, 78));
    g_penFrame = CreatePen(PS_SOLID, 2, kRgbNeonCyan);
    g_penGlow = CreatePen(PS_SOLID, 1, kRgbNeonDim);
    g_fontTitle = CreateFontA(26, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, "Segoe UI");
    g_fontUi = CreateFontA(16, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, "Segoe UI");
    g_fontMono = CreateFontA(15, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, "Consolas");
}

static void ThemeDestroy() {
    if (g_fontTitle) { DeleteObject(g_fontTitle); g_fontTitle = nullptr; }
    if (g_fontUi) { DeleteObject(g_fontUi); g_fontUi = nullptr; }
    if (g_fontMono) { DeleteObject(g_fontMono); g_fontMono = nullptr; }
    if (g_penFrame) { DeleteObject(g_penFrame); g_penFrame = nullptr; }
    if (g_penGlow) { DeleteObject(g_penGlow); g_penGlow = nullptr; }
    if (g_brDeep) { DeleteObject(g_brDeep); g_brDeep = nullptr; }
    if (g_brPanel) { DeleteObject(g_brPanel); g_brPanel = nullptr; }
    if (g_brEdit) { DeleteObject(g_brEdit); g_brEdit = nullptr; }
    if (g_brListSel) { DeleteObject(g_brListSel); g_brListSel = nullptr; }
    if (g_brBtn) { DeleteObject(g_brBtn); g_brBtn = nullptr; }
    if (g_brBtnHot) { DeleteObject(g_brBtnHot); g_brBtnHot = nullptr; }
}

static void ApplyNeonControlSkin(HWND hCtrl) {
    if (!hCtrl) return;
    SetWindowTheme(hCtrl, L"", L"");
    SendMessageA(hCtrl, WM_SETFONT, reinterpret_cast<WPARAM>(g_fontUi), TRUE);
}

static void PaintNeonWindow(HWND hWnd, HDC hdc) {
    RECT rc{};
    GetClientRect(hWnd, &rc);
    FillRect(hdc, &rc, g_brDeep);

    // Scanline-style tint in header only (keeps repaint cheap)
    const int savedDc = SaveDC(hdc);
    IntersectClipRect(hdc, rc.left, rc.top, rc.right, 58);
    const int clipH = 58;
    for (int y = 0; y < clipH; y += 3) {
        const int t = (y * 255) / std::max(1, clipH);
        const COLORREF c = RGB(6 + t / 18, 8 + t / 22, 22 + t / 6);
        RECT strip{ rc.left, y, rc.right, y + 3 };
        HBRUSH b = CreateSolidBrush(c);
        FillRect(hdc, &strip, b);
        DeleteObject(b);
    }
    if (savedDc) RestoreDC(hdc, savedDc);

    // Panel wells (behind controls)
    RECT leftPanel{ 8, 74, 388, rc.bottom - 44 };
    RECT rightPanel{ 392, 74, rc.right - 8, rc.bottom - 44 };
    FillRect(hdc, &leftPanel, g_brPanel);
    FillRect(hdc, &rightPanel, g_brPanel);
    HPEN wellPen = CreatePen(PS_SOLID, 1, kRgbNeonDim);
    HPEN oldPen2 = static_cast<HPEN>(SelectObject(hdc, wellPen));
    HBRUSH oldBr2 = static_cast<HBRUSH>(SelectObject(hdc, GetStockObject(NULL_BRUSH)));
    Rectangle(hdc, leftPanel.left, leftPanel.top, leftPanel.right, leftPanel.bottom);
    Rectangle(hdc, rightPanel.left, rightPanel.top, rightPanel.right, rightPanel.bottom);
    SelectObject(hdc, oldPen2);
    SelectObject(hdc, oldBr2);
    DeleteObject(wellPen);

    // Outer neon frame (double line glow)
    HPEN oldPen = static_cast<HPEN>(SelectObject(hdc, g_penGlow));
    HBRUSH oldBr = static_cast<HBRUSH>(SelectObject(hdc, GetStockObject(NULL_BRUSH)));
    Rectangle(hdc, rc.left + 6, rc.top + 6, rc.right - 6, rc.bottom - 6);
    SelectObject(hdc, g_penFrame);
    Rectangle(hdc, rc.left + 8, rc.top + 8, rc.right - 8, rc.bottom - 8);
    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBr);

    // Accent line under title zone
    HPEN mag = CreatePen(PS_SOLID, 2, kRgbNeonMagenta);
    HPEN prevForMag = static_cast<HPEN>(SelectObject(hdc, mag));
    MoveToEx(hdc, 18, 52, nullptr);
    LineTo(hdc, rc.right - 18, 52);
    SelectObject(hdc, prevForMag);
    DeleteObject(mag);

    SetBkMode(hdc, TRANSPARENT);
    HFONT oldF = static_cast<HFONT>(SelectObject(hdc, g_fontTitle));
    RECT titleRc{ 18, 10, rc.right - 18, 52 };
    SetTextColor(hdc, RGB(0, 60, 70));
    RECT shadow = titleRc;
    OffsetRect(&shadow, 2, 2);
    DrawTextA(hdc, "WIDGET NEXUS", -1, &shadow, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    SetTextColor(hdc, kRgbNeonCyan);
    DrawTextA(hdc, "WIDGET NEXUS", -1, &titleRc, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    SelectObject(hdc, oldF);
}

static void DrawOwnerDrawButton(const DRAWITEMSTRUCT& dis) {
    const bool pressed = (dis.itemState & ODS_SELECTED) != 0;
    const bool focus = (dis.itemState & ODS_FOCUS) != 0;
    HDC hdc = dis.hDC;
    RECT rc = dis.rcItem;

    HBRUSH fill = pressed ? g_brBtnHot : g_brBtn;
    FillRect(hdc, &rc, fill);

    HPEN penOuter = CreatePen(PS_SOLID, 2, kRgbNeonCyan);
    HPEN penInner = CreatePen(PS_SOLID, 1, kRgbNeonMagenta);
    HPEN old = static_cast<HPEN>(SelectObject(hdc, penOuter));
    HBRUSH oldBr = static_cast<HBRUSH>(SelectObject(hdc, GetStockObject(NULL_BRUSH)));
    Rectangle(hdc, rc.left, rc.top, rc.right, rc.bottom);
    InflateRect(&rc, -2, -2);
    SelectObject(hdc, penInner);
    Rectangle(hdc, rc.left, rc.top, rc.right, rc.bottom);
    SelectObject(hdc, old);
    SelectObject(hdc, oldBr);
    DeleteObject(penOuter);
    DeleteObject(penInner);
    SelectObject(hdc, GetStockObject(BLACK_PEN));

    char caption[64]{};
    GetWindowTextA(dis.hwndItem, caption, static_cast<int>(sizeof(caption)));
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, kRgbTextHi);
    HFONT oldF = static_cast<HFONT>(SelectObject(hdc, g_fontUi));
    RECT tr = dis.rcItem;
    DrawTextA(hdc, caption, -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hdc, oldF);

    if (focus) {
        RECT fr = dis.rcItem;
        InflateRect(&fr, -4, -4);
        DrawFocusRect(hdc, &fr);
    }
}

static void DrawOwnerDrawListItem(const DRAWITEMSTRUCT& dis) {
    HDC hdc = dis.hDC;
    RECT rc = dis.rcItem;
    const bool selected = (dis.itemState & ODS_SELECTED) != 0;

    FillRect(hdc, &rc, selected ? g_brListSel : g_brPanel);

    HPEN pen = CreatePen(PS_SOLID, selected ? 2 : 1, selected ? kRgbNeonCyan : kRgbNeonDim);
    HPEN oldP = static_cast<HPEN>(SelectObject(hdc, pen));
    HBRUSH oldB = static_cast<HBRUSH>(SelectObject(hdc, GetStockObject(NULL_BRUSH)));
    Rectangle(hdc, rc.left + 2, rc.top + 2, rc.right - 2, rc.bottom - 2);
    SelectObject(hdc, oldP);
    SelectObject(hdc, oldB);
    DeleteObject(pen);
    SelectObject(hdc, GetStockObject(BLACK_PEN));

    char buf[512]{};
    SendMessageA(dis.hwndItem, LB_GETTEXT, dis.itemID, reinterpret_cast<LPARAM>(buf));
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, selected ? kRgbNeonCyan : kRgbTextLo);
    HFONT oldF = static_cast<HFONT>(SelectObject(hdc, g_fontUi));
    RECT tr = rc;
    InflateRect(&tr, -10, 0);
    DrawTextA(hdc, buf, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    SelectObject(hdc, oldF);

    if ((dis.itemState & ODS_FOCUS) != 0) {
        RECT fr = dis.rcItem;
        InflateRect(&fr, -4, -4);
        DrawFocusRect(hdc, &fr);
    }
}

static std::string Trim(const std::string& s) {
    const auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    const auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

static void EnsureDefaultWidgets() {
    if (!g_rows.empty()) return;

    Widget ssh;
    ssh.name = "SSH on WSL";
    ssh.alwaysVisible = true;
    ssh.commands.push_back("wsl.exe -e bash -lc \"ssh user@server\"");
    g_rows.push_back({ ssh, nullptr });

    Widget wsl;
    wsl.name = "Open WSL Home";
    wsl.alwaysVisible = false;
    wsl.commands.push_back("wsl.exe -e bash -lc \"cd ~ && exec bash\"");
    g_rows.push_back({ wsl, nullptr });
}

static bool LoadWidgets(const std::string& path) {
    g_rows.clear();
    std::ifstream in(path);
    if (!in.is_open()) {
        EnsureDefaultWidgets();
        return false;
    }

    Widget current;
    bool inWidget = false;
    std::string line;
    while (std::getline(in, line)) {
        line = Trim(line);
        if (line.empty()) continue;

        if (line == "[Widget]") {
            if (inWidget) g_rows.push_back({ current, nullptr });
            current = Widget{};
            inWidget = true;
            continue;
        }

        if (!inWidget) continue;
        const auto pos = line.find('=');
        if (pos == std::string::npos) continue;
        const std::string key = Trim(line.substr(0, pos));
        const std::string value = Trim(line.substr(pos + 1));

        if (key == "name") current.name = value;
        else if (key == "alwaysVisible") current.alwaysVisible = (value == "1" || value == "true" || value == "True");
        else if (key == "command") current.commands.push_back(value);
    }

    if (inWidget) g_rows.push_back({ current, nullptr });
    g_rows.erase(std::remove_if(g_rows.begin(), g_rows.end(), [](const WidgetRow& r) { return r.w.name.empty(); }), g_rows.end());
    EnsureDefaultWidgets();
    return true;
}

static bool SaveWidgets(const std::string& path) {
    std::ofstream out(path, std::ios::trunc);
    if (!out.is_open()) return false;
    for (const auto& row : g_rows) {
        const Widget& w = row.w;
        out << "[Widget]\n";
        out << "name=" << w.name << "\n";
        out << "alwaysVisible=" << (w.alwaysVisible ? "1" : "0") << "\n";
        for (const auto& cmd : w.commands) out << "command=" << cmd << "\n";
        out << "\n";
    }
    return true;
}

static void SetStatus(HWND /*hWnd*/, const std::string& text) {
    if (!g_nexusHwnd) return;
    SetWindowTextA(GetDlgItem(g_nexusHwnd, IDC_STATIC_STATUS), text.c_str());
}

static std::string ReadEditText(HWND hEdit) {
    const int len = GetWindowTextLengthA(hEdit);
    std::string text(len, '\0');
    if (len > 0) GetWindowTextA(hEdit, &text[0], len + 1);
    return text;
}

static void WriteWidgetToEditor(HWND hWnd, int modelIndex) {
    HWND hName = GetDlgItem(hWnd, IDC_EDIT_NAME);
    HWND hAlways = GetDlgItem(hWnd, IDC_CHECK_ALWAYS);
    HWND hCommands = GetDlgItem(hWnd, IDC_EDIT_COMMANDS);
    if (modelIndex < 0 || modelIndex >= static_cast<int>(g_rows.size())) {
        SetWindowTextA(hName, "");
        SendMessageA(hAlways, BM_SETCHECK, BST_UNCHECKED, 0);
        SetWindowTextA(hCommands, "");
        return;
    }

    const Widget& w = g_rows[modelIndex].w;
    SetWindowTextA(hName, w.name.c_str());
    SendMessageA(hAlways, BM_SETCHECK, w.alwaysVisible ? BST_CHECKED : BST_UNCHECKED, 0);

    std::ostringstream oss;
    for (size_t i = 0; i < w.commands.size(); ++i) {
        oss << w.commands[i];
        if (i + 1 < w.commands.size()) oss << "\r\n";
    }
    SetWindowTextA(hCommands, oss.str().c_str());
}

static void ReadEditorToWidget(HWND hWnd, int modelIndex) {
    if (modelIndex < 0 || modelIndex >= static_cast<int>(g_rows.size())) return;
    Widget& w = g_rows[modelIndex].w;
    w.name = Trim(ReadEditText(GetDlgItem(hWnd, IDC_EDIT_NAME)));
    w.alwaysVisible = (SendMessageA(GetDlgItem(hWnd, IDC_CHECK_ALWAYS), BM_GETCHECK, 0, 0) == BST_CHECKED);

    const std::string commandsText = ReadEditText(GetDlgItem(hWnd, IDC_EDIT_COMMANDS));
    std::istringstream iss(commandsText);
    std::string line;
    w.commands.clear();
    while (std::getline(iss, line)) {
        line = Trim(line);
        if (!line.empty()) w.commands.push_back(line);
    }
}

static void RefreshWidgetList(HWND hWnd) {
    HWND hList = GetDlgItem(hWnd, IDC_LIST_WIDGETS);
    SendMessageA(hList, LB_RESETCONTENT, 0, 0);

    int selectedListIndex = -1;
    for (size_t i = 0; i < g_rows.size(); ++i) {
        const Widget& w = g_rows[i].w;
        std::string label = (w.alwaysVisible ? "[PIN] " : "[   ] ") + w.name;
        SendMessageA(hList, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
        SendMessageA(hList, LB_SETITEMDATA, static_cast<WPARAM>(i), static_cast<LPARAM>(i));
        if (static_cast<int>(i) == g_selectedModelIndex) selectedListIndex = static_cast<int>(i);
    }

    if (selectedListIndex >= 0) {
        SendMessageA(hList, LB_SETCURSEL, selectedListIndex, 0);
    } else {
        g_selectedModelIndex = -1;
        WriteWidgetToEditor(hWnd, -1);
    }
}

static bool RunSingleCommand(const std::string& command, std::string& output) {
    auto toLower = [](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    };

    const std::string cmdLower = toLower(command);
    const bool interactive =
        (cmdLower.find("wsl") != std::string::npos) ||
        (cmdLower.find("ssh ") != std::string::npos) ||
        (cmdLower.find("wt.exe") != std::string::npos);

    if (interactive) {
        // Interactive commands (like ssh password prompts) must run in a visible console.
        STARTUPINFOA si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};

        std::string cmdLine = "cmd.exe /K " + command;
        std::vector<char> mutableCmd(cmdLine.begin(), cmdLine.end());
        mutableCmd.push_back('\0');

        const BOOL ok = CreateProcessA(
            nullptr,
            mutableCmd.data(),
            nullptr,
            nullptr,
            FALSE,
            CREATE_NEW_CONSOLE,
            nullptr,
            nullptr,
            &si,
            &pi
        );

        if (!ok) {
            output = "Failed to launch interactive terminal command.";
            return false;
        }

        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        output = "Interactive command launched in new terminal window.";
        return true;
    }

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE hRead = nullptr, hWrite = nullptr;
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) return false;
    SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = hWrite;
    si.hStdError = hWrite;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION pi{};
    std::string cmdLine = "cmd.exe /C " + command;
    std::vector<char> mutableCmd(cmdLine.begin(), cmdLine.end());
    mutableCmd.push_back('\0');

    const BOOL ok = CreateProcessA(nullptr, mutableCmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(hWrite);
    if (!ok) {
        CloseHandle(hRead);
        return false;
    }

    DWORD waitResult = WaitForSingleObject(pi.hProcess, INFINITE);
    (void)waitResult;

    char buffer[4096];
    DWORD bytesRead = 0;
    output.clear();
    while (ReadFile(hRead, buffer, sizeof(buffer) - 1, &bytesRead, nullptr) && bytesRead > 0) {
        buffer[bytesRead] = '\0';
        output += buffer;
    }

    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    CloseHandle(hRead);
    return exitCode == 0;
}

static void RunWidget(int modelIndex) {
    if (modelIndex < 0 || modelIndex >= static_cast<int>(g_rows.size())) return;
    const Widget& w = g_rows[modelIndex].w;
    if (w.commands.empty()) {
        SetStatus(nullptr, "No commands to run.");
        return;
    }

    SetStatus(nullptr, "Running: " + w.name);

    for (const auto& cmd : w.commands) {
        std::string out;
        if (!RunSingleCommand(cmd, out)) {
            SetStatus(nullptr, "Failed: " + w.name);
            return;
        }
    }

    SetStatus(nullptr, "Completed: " + w.name);
}

static void UpdateFloaterIndices() {
    for (size_t j = 0; j < g_rows.size(); ++j) {
        if (g_rows[j].floater) SetWindowLongPtr(g_rows[j].floater, GWLP_USERDATA, static_cast<LONG_PTR>(j));
    }
}

static void ApplyFloaterTopmost(size_t i) {
    if (i >= g_rows.size() || !g_rows[i].floater) return;
    SetWindowPos(
        g_rows[i].floater,
        g_rows[i].w.alwaysVisible ? HWND_TOPMOST : HWND_NOTOPMOST,
        0,
        0,
        0,
        0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

static void SyncFloaterVisibility() {
    for (auto& r : g_rows) {
        if (!r.floater) continue;
        const bool show = r.w.alwaysVisible || g_showNonPinned;
        ShowWindow(r.floater, show ? SW_SHOWNOACTIVATE : SW_HIDE);
    }
}

static void RefreshFloaterChrome(size_t i) {
    if (i >= g_rows.size() || !g_rows[i].floater) return;
    WidgetRow& r = g_rows[i];
    SetWindowTextA(r.floater, r.w.name.c_str());
    ApplyFloaterTopmost(i);
    InvalidateRect(r.floater, nullptr, FALSE);
}

static RECT GetPrimaryWorkArea() {
    RECT work{};
    SystemParametersInfoA(SPI_GETWORKAREA, 0, &work, 0);
    return work;
}

// Index i → column c (0 = rightmost), row r (0 = top). Columns fill downward first, then step left.
static void LayoutFloatingWidgets() {
    const RECT work = GetPrimaryWorkArea();
    const int usableH = std::max(1, static_cast<int>(work.bottom - work.top - 2 * kFloaterMargin));
    const int rowStride = kFloaterDiameter + kFloaterGap;
    const int rowsPerCol = std::max(1, usableH / rowStride);

    const size_t rowsPerColumn = static_cast<size_t>(rowsPerCol);
    for (size_t i = 0; i < g_rows.size(); ++i) {
        if (!g_rows[i].floater) continue;
        const int col = static_cast<int>(i / rowsPerColumn);
        const int row = static_cast<int>(i % rowsPerColumn);
        const int x = work.right - kFloaterMargin - kFloaterDiameter - col * (kFloaterDiameter + kFloaterGap);
        const int y = work.top + kFloaterMargin + row * rowStride;
        SetWindowPos(g_rows[i].floater, nullptr, x, y, kFloaterDiameter, kFloaterDiameter, SWP_NOACTIVATE | SWP_NOZORDER);
    }
}

static void CreateFloaterForIndex(HINSTANCE inst, size_t i) {
    if (i >= g_rows.size()) return;
    WidgetRow& r = g_rows[i];
    if (r.floater) {
        DestroyWindow(r.floater);
        r.floater = nullptr;
    }
    const DWORD exStyle = WS_EX_LAYERED | WS_EX_TOOLWINDOW | (r.w.alwaysVisible ? WS_EX_TOPMOST : 0);
    r.floater = CreateWindowExA(
        exStyle,
        FLOATER_CLASS_NAME,
        r.w.name.c_str(),
        WS_POPUP | WS_VISIBLE,
        0,
        0,
        kFloaterDiameter,
        kFloaterDiameter,
        nullptr,
        nullptr,
        inst,
        reinterpret_cast<LPVOID>(static_cast<INT_PTR>(i)));
    if (r.floater) {
        RECT rc{};
        GetClientRect(r.floater, &rc);
        if (HRGN rgn = CreateEllipticRgn(rc.left, rc.top, rc.right, rc.bottom)) {
            SetWindowRgn(r.floater, rgn, TRUE);
        }
        SetLayeredWindowAttributes(r.floater, 0, kFloaterAlpha, LWA_ALPHA);
        SendMessageA(r.floater, WM_SETFONT, reinterpret_cast<WPARAM>(g_fontUi), TRUE);
    }
}

static void DestroyAllFloaters() {
    for (auto& r : g_rows) {
        if (r.floater) {
            DestroyWindow(r.floater);
            r.floater = nullptr;
        }
    }
}

static void RebuildAllFloaters(HINSTANCE inst) {
    DestroyAllFloaters();
    for (size_t i = 0; i < g_rows.size(); ++i) CreateFloaterForIndex(inst, i);
    LayoutFloatingWidgets();
    SyncFloaterVisibility();
    for (size_t i = 0; i < g_rows.size(); ++i) ApplyFloaterTopmost(i);
}

static void TrayAdd(HWND hWnd) {
    if (g_trayIconAdded) return;
    NOTIFYICONDATAA nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hWnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_APP_TRAY;
    nid.hIcon = LoadIconA(nullptr, IDI_APPLICATION);
    lstrcpyA(nid.szTip, "Widget Nexus — right-click for menu");
    Shell_NotifyIconA(NIM_ADD, &nid);
    g_trayIconAdded = true;
}

static void TrayRemove() {
    if (!g_trayIconAdded) return;
    NOTIFYICONDATAA nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = g_nexusHwnd;
    nid.uID = 1;
    Shell_NotifyIconA(NIM_DELETE, &nid);
    g_trayIconAdded = false;
}

static LRESULT CALLBACK FloaterWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        const auto* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
        const int idx = static_cast<int>(reinterpret_cast<INT_PTR>(cs->lpCreateParams));
        SetWindowLongPtr(hwnd, GWLP_USERDATA, static_cast<LONG_PTR>(idx));
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_NCHITTEST: {
        // Outer ring of the circle = drag; inner disk = click to run.
        POINT pt{};
        pt.x = static_cast<int>(static_cast<short>(LOWORD(lParam)));
        pt.y = static_cast<int>(static_cast<short>(HIWORD(lParam)));
        ScreenToClient(hwnd, &pt);
        RECT rc{};
        GetClientRect(hwnd, &rc);
        if (!PtInRect(&rc, pt)) return DefWindowProc(hwnd, msg, wParam, lParam);
        const int cx = (rc.left + rc.right) / 2;
        const int cy = (rc.top + rc.bottom) / 2;
        const int R = (rc.right - rc.left) / 2;
        const long long dx = static_cast<long long>(pt.x - cx);
        const long long dy = static_cast<long long>(pt.y - cy);
        const long long dist2 = dx * dx + dy * dy;
        const long long rOuter = static_cast<long long>(R);
        const long long rInner = static_cast<long long>(std::max(1, R - kFloaterDragRingPx));
        if (dist2 > rOuter * rOuter) return DefWindowProc(hwnd, msg, wParam, lParam);
        if (dist2 >= rInner * rInner) return HTCAPTION;
        return HTCLIENT;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        const HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc{};
        GetClientRect(hwnd, &rc);
        if (g_brBtn) {
            HBRUSH oldB = static_cast<HBRUSH>(SelectObject(hdc, g_brBtn));
            Ellipse(hdc, rc.left, rc.top, rc.right, rc.bottom);
            SelectObject(hdc, oldB);
        }
        if (g_penGlow && g_penFrame) {
            HBRUSH oldBr = static_cast<HBRUSH>(SelectObject(hdc, GetStockObject(NULL_BRUSH)));
            HPEN oldP = static_cast<HPEN>(SelectObject(hdc, g_penGlow));
            Ellipse(hdc, rc.left + 1, rc.top + 1, rc.right - 1, rc.bottom - 1);
            SelectObject(hdc, g_penFrame);
            Ellipse(hdc, rc.left + 2, rc.top + 2, rc.right - 2, rc.bottom - 2);
            SelectObject(hdc, oldP);
            SelectObject(hdc, oldBr);
        }
        const int idx = static_cast<int>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
        std::string name = "Widget";
        if (idx >= 0 && idx < static_cast<int>(g_rows.size())) name = g_rows[idx].w.name;
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, kRgbNeonCyan);
        HFONT oldF = nullptr;
        if (g_fontUi) oldF = static_cast<HFONT>(SelectObject(hdc, g_fontUi));
        RECT textRc = rc;
        InflateRect(&textRc, -10, -10);
        DrawTextA(hdc, name.c_str(), -1, &textRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        if (oldF) SelectObject(hdc, oldF);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_LBUTTONUP: {
        const int idx = static_cast<int>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
        if (idx >= 0 && idx < static_cast<int>(g_rows.size())) RunWidget(idx);
        return 0;
    }
    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
}

static void SaveCurrentEditorWidget(HWND hWnd) {
    if (g_selectedModelIndex >= 0 && g_selectedModelIndex < static_cast<int>(g_rows.size())) {
        ReadEditorToWidget(hWnd, g_selectedModelIndex);
        RefreshFloaterChrome(static_cast<size_t>(g_selectedModelIndex));
    }
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_DISPLAYCHANGE) {
        LayoutFloatingWidgets();
        return DefWindowProc(hWnd, msg, wParam, lParam);
    }

    if (msg == WM_APP_TRAY) {
        if (lParam == WM_LBUTTONDBLCLK) {
            ShowWindow(hWnd, SW_SHOW);
            SetForegroundWindow(hWnd);
            return 0;
        }
        if (lParam == WM_RBUTTONUP) {
            POINT pt{};
            GetCursorPos(&pt);
            HMENU m = CreatePopupMenu();
            AppendMenuA(m, MF_STRING, 1, "Show Nexus");
            AppendMenuA(m, MF_STRING, 2, "Exit");
            SetForegroundWindow(hWnd);
            const UINT cmd = TrackPopupMenu(m, TPM_RIGHTALIGN | TPM_BOTTOMALIGN | TPM_RETURNCMD, pt.x, pt.y, 0, hWnd, nullptr);
            DestroyMenu(m);
            if (cmd == 1) {
                ShowWindow(hWnd, SW_SHOW);
                SetForegroundWindow(hWnd);
            } else if (cmd == 2) {
                DestroyWindow(hWnd);
            }
            return 0;
        }
        return 0;
    }

    switch (msg) {
    case WM_CREATE: {
        g_nexusHwnd = hWnd;
        const auto* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
        g_hInstance = cs->hInstance;

        InitCommonControls();
        ThemeCreate();

        constexpr int TOP = 58;
        constexpr int BTN_W = 88;
        constexpr int BTN_H = 34;
        constexpr int BTN_GAP = 6;

        HWND hShow = CreateWindowA("BUTTON", "Show non-pinned widget windows", WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX,
            16, TOP + 6, 280, 26, hWnd, reinterpret_cast<HMENU>(IDC_SHOW_NON_PINNED), nullptr, nullptr);
        ApplyNeonControlSkin(hShow);
        SendMessageA(hShow, BM_SETCHECK, BST_CHECKED, 0);

        HWND hList = CreateWindowA(
            "LISTBOX",
            "",
            WS_VISIBLE | WS_CHILD | WS_BORDER | LBS_NOTIFY | WS_VSCROLL | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | LBS_NOINTEGRALHEIGHT,
            16,
            TOP + 36,
            368,
            318,
            hWnd,
            reinterpret_cast<HMENU>(IDC_LIST_WIDGETS),
            nullptr,
            nullptr);
        ApplyNeonControlSkin(hList);
        SendMessageA(hList, LB_SETITEMHEIGHT, static_cast<WPARAM>(g_listItemHeight), 0);

        const int rowY = TOP + 364;
        const auto makeNeonBtn = [&](const char* text, int x, int id) {
            HWND b = CreateWindowA("BUTTON", text, WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
                x, rowY, BTN_W, BTN_H, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
            SetWindowTheme(b, L"", L"");
            return b;
        };
        makeNeonBtn("Run", 16, IDC_BTN_RUN);
        makeNeonBtn("Add", 16 + (BTN_W + BTN_GAP), IDC_BTN_ADD);
        makeNeonBtn("Delete", 16 + 2 * (BTN_W + BTN_GAP), IDC_BTN_DELETE);
        makeNeonBtn("Save", 16 + 3 * (BTN_W + BTN_GAP), IDC_BTN_SAVE);
        makeNeonBtn("Hide", 16 + 4 * (BTN_W + BTN_GAP), IDC_BTN_HIDE_NEXUS);

        HWND hLblName = CreateWindowA("STATIC", "Name:", WS_VISIBLE | WS_CHILD,
            400, TOP + 4, 64, 22, hWnd, nullptr, nullptr, nullptr);
        ApplyNeonControlSkin(hLblName);
        HWND hName = CreateWindowA("EDIT", "", WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL,
            468, TOP + 2, 396, 28, hWnd, reinterpret_cast<HMENU>(IDC_EDIT_NAME), nullptr, nullptr);
        ApplyNeonControlSkin(hName);

        HWND hAlways = CreateWindowA("BUTTON", "Always visible (pinned)", WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX,
            468, TOP + 36, 280, 26, hWnd, reinterpret_cast<HMENU>(IDC_CHECK_ALWAYS), nullptr, nullptr);
        ApplyNeonControlSkin(hAlways);

        HWND hLblCmd = CreateWindowA("STATIC", "Commands (one per line):", WS_VISIBLE | WS_CHILD,
            400, TOP + 72, 260, 22, hWnd, nullptr, nullptr, nullptr);
        ApplyNeonControlSkin(hLblCmd);
        HWND hCmd = CreateWindowA("EDIT", "", WS_VISIBLE | WS_CHILD | WS_BORDER | ES_LEFT | ES_MULTILINE | WS_VSCROLL | ES_AUTOVSCROLL,
            400, TOP + 96, 472, 258, hWnd, reinterpret_cast<HMENU>(IDC_EDIT_COMMANDS), nullptr, nullptr);
        ApplyNeonControlSkin(hCmd);
        SendMessageA(hCmd, WM_SETFONT, reinterpret_cast<WPARAM>(g_fontMono), TRUE);

        HWND hStatus = CreateWindowA("STATIC", "Ready.", WS_VISIBLE | WS_CHILD,
            20, TOP + 408, 880, 24, hWnd, reinterpret_cast<HMENU>(IDC_STATIC_STATUS), nullptr, nullptr);
        ApplyNeonControlSkin(hStatus);

        LoadWidgets(g_configPath);
        RebuildAllFloaters(g_hInstance);
        RefreshWidgetList(hWnd);
        SetStatus(hWnd, "Loaded widgets from widgets.txt");
        InvalidateRect(hWnd, nullptr, FALSE);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps{};
        const HDC hdc = BeginPaint(hWnd, &ps);
        PaintNeonWindow(hWnd, hdc);
        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_CTLCOLOREDIT: {
        const HDC hdc = reinterpret_cast<HDC>(wParam);
        SetBkColor(hdc, kRgbEdit);
        SetTextColor(hdc, kRgbTextHi);
        return reinterpret_cast<LRESULT>(g_brEdit);
    }

    case WM_CTLCOLORLISTBOX: {
        const HDC hdc = reinterpret_cast<HDC>(wParam);
        SetBkColor(hdc, kRgbPanel);
        SetTextColor(hdc, kRgbTextHi);
        return reinterpret_cast<LRESULT>(g_brPanel);
    }

    case WM_CTLCOLORSTATIC: {
        const HDC hdc = reinterpret_cast<HDC>(wParam);
        SetBkMode(hdc, OPAQUE);
        SetBkColor(hdc, kRgbPanel);
        SetTextColor(hdc, kRgbTextHi);
        return reinterpret_cast<LRESULT>(g_brPanel);
    }

    case WM_CTLCOLORBTN: {
        const HDC hdc = reinterpret_cast<HDC>(wParam);
        SetBkMode(hdc, OPAQUE);
        SetBkColor(hdc, kRgbPanel);
        SetTextColor(hdc, kRgbNeonCyan);
        return reinterpret_cast<LRESULT>(g_brPanel);
    }

    case WM_MEASUREITEM: {
        auto* mis = reinterpret_cast<MEASUREITEMSTRUCT*>(lParam);
        if (mis && mis->CtlID == IDC_LIST_WIDGETS) {
            mis->itemHeight = static_cast<UINT>(g_listItemHeight);
            return TRUE;
        }
        break;
    }

    case WM_DRAWITEM: {
        const auto* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
        if (!dis) break;
        if (dis->CtlType == ODT_BUTTON) {
            DrawOwnerDrawButton(*dis);
            return TRUE;
        }
        if (dis->CtlType == ODT_LISTBOX && dis->CtlID == IDC_LIST_WIDGETS) {
            DrawOwnerDrawListItem(*dis);
            return TRUE;
        }
        break;
    }

    case WM_COMMAND: {
        const int id = LOWORD(wParam);
        const int code = HIWORD(wParam);

        if (id == IDC_SHOW_NON_PINNED && code == BN_CLICKED) {
            SaveCurrentEditorWidget(hWnd);
            g_showNonPinned = (SendMessageA(GetDlgItem(hWnd, IDC_SHOW_NON_PINNED), BM_GETCHECK, 0, 0) == BST_CHECKED);
            SyncFloaterVisibility();
            RefreshWidgetList(hWnd);
            return 0;
        }

        if (id == IDC_CHECK_ALWAYS && code == BN_CLICKED) {
            ReadEditorToWidget(hWnd, g_selectedModelIndex);
            if (g_selectedModelIndex >= 0) {
                const size_t idx = static_cast<size_t>(g_selectedModelIndex);
                RefreshFloaterChrome(idx);
                SyncFloaterVisibility();
            }
            RefreshWidgetList(hWnd);
            return 0;
        }

        if (id == IDC_BTN_HIDE_NEXUS && code == BN_CLICKED) {
            TrayAdd(hWnd);
            ShowWindow(hWnd, SW_HIDE);
            return 0;
        }

        if (id == IDC_LIST_WIDGETS && code == LBN_SELCHANGE) {
            const int prevSel = g_selectedModelIndex;
            HWND hList = GetDlgItem(hWnd, IDC_LIST_WIDGETS);
            const int sel = static_cast<int>(SendMessageA(hList, LB_GETCURSEL, 0, 0));
            if (prevSel >= 0 && prevSel < static_cast<int>(g_rows.size())) {
                ReadEditorToWidget(hWnd, prevSel);
                RefreshFloaterChrome(static_cast<size_t>(prevSel));
            }
            if (sel != LB_ERR) {
                g_selectedModelIndex = static_cast<int>(SendMessageA(hList, LB_GETITEMDATA, sel, 0));
                WriteWidgetToEditor(hWnd, g_selectedModelIndex);
            } else {
                g_selectedModelIndex = -1;
                WriteWidgetToEditor(hWnd, -1);
            }
            return 0;
        }

        if (id == IDC_BTN_ADD && code == BN_CLICKED) {
            SaveCurrentEditorWidget(hWnd);
            Widget w;
            w.name = "New Widget";
            w.alwaysVisible = false;
            w.commands.push_back("echo hello");
            g_rows.push_back({ w, nullptr });
            g_selectedModelIndex = static_cast<int>(g_rows.size()) - 1;
            CreateFloaterForIndex(g_hInstance, static_cast<size_t>(g_selectedModelIndex));
            UpdateFloaterIndices();
            LayoutFloatingWidgets();
            SyncFloaterVisibility();
            ApplyFloaterTopmost(static_cast<size_t>(g_selectedModelIndex));
            RefreshWidgetList(hWnd);
            WriteWidgetToEditor(hWnd, g_selectedModelIndex);
            SetStatus(hWnd, "Widget added.");
            return 0;
        }

        if (id == IDC_BTN_DELETE && code == BN_CLICKED) {
            if (g_selectedModelIndex >= 0 && g_selectedModelIndex < static_cast<int>(g_rows.size())) {
                if (g_rows[g_selectedModelIndex].floater) {
                    DestroyWindow(g_rows[g_selectedModelIndex].floater);
                    g_rows[g_selectedModelIndex].floater = nullptr;
                }
                g_rows.erase(g_rows.begin() + g_selectedModelIndex);
                g_selectedModelIndex = -1;
                UpdateFloaterIndices();
                LayoutFloatingWidgets();
                RefreshWidgetList(hWnd);
                SetStatus(hWnd, "Widget deleted.");
            }
            return 0;
        }

        if (id == IDC_BTN_SAVE && code == BN_CLICKED) {
            SaveCurrentEditorWidget(hWnd);
            RefreshWidgetList(hWnd);
            if (SaveWidgets(g_configPath)) SetStatus(hWnd, "Saved to widgets.txt");
            else SetStatus(hWnd, "Failed to save widgets.txt");
            return 0;
        }

        if (id == IDC_BTN_RUN && code == BN_CLICKED) {
            SaveCurrentEditorWidget(hWnd);
            RefreshWidgetList(hWnd);
            RunWidget(g_selectedModelIndex);
            return 0;
        }
        break;
    }

    case WM_DESTROY:
        TrayRemove();
        DestroyAllFloaters();
        ThemeDestroy();
        g_nexusHwnd = nullptr;
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    g_hInstance = hInstance;

    const char CLASS_NAME[] = "WidgetLauncherWin32";

    WNDCLASSA wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(NULL_BRUSH));

    RegisterClassA(&wc);

    WNDCLASSA wf{};
    wf.lpfnWndProc = FloaterWndProc;
    wf.hInstance = hInstance;
    wf.lpszClassName = FLOATER_CLASS_NAME;
    wf.hCursor = LoadCursor(nullptr, IDC_HAND);
    wf.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(NULL_BRUSH));
    RegisterClassA(&wf);

    HWND hWnd = CreateWindowExA(
        0, CLASS_NAME, "WIDGET NEXUS",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 960, 700,
        nullptr, nullptr, hInstance, nullptr
    );

    if (!hWnd) return 0;
    g_nexusHwnd = hWnd;
    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    MSG msg{};
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 0;
}
