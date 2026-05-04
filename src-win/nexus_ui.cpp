#include "nexus_ui.hpp"

#include "app_ids.hpp"
#include "dpi.hpp"
#include "globals.hpp"
#include "layout.hpp"
#include "model.hpp"
#include "nexus_paint.hpp"

#include <sstream>
#include <string>
#include <vector>

namespace {
int CurrentCommandLineIndex(HWND hWnd) {
    HWND hCmd = GetDlgItem(hWnd, IDC_EDIT_COMMANDS);
    if (!hCmd) return -1;
    DWORD sel = static_cast<DWORD>(SendMessageA(hCmd, EM_GETSEL, 0, 0));
    const int caret = static_cast<int>(LOWORD(sel));
    return static_cast<int>(SendMessageA(hCmd, EM_LINEFROMCHAR, static_cast<WPARAM>(caret), 0));
}
}

void SetStatus(HWND /*hWnd*/, const std::string& text) {
    if (!g_nexusHwnd) return;
    SetWindowTextA(GetDlgItem(g_nexusHwnd, IDC_STATIC_STATUS), text.c_str());
}

std::string ReadEditText(HWND hEdit) {
    const int len = GetWindowTextLengthA(hEdit);
    std::string text(len, '\0');
    if (len > 0) GetWindowTextA(hEdit, &text[0], len + 1);
    return text;
}

int FindGroupIndexByName(const std::string& groupName) {
    if (groupName.empty()) return -1;
    for (size_t i = 0; i < g_groups.size(); ++i) {
        if (g_groups[i].g.name == groupName) return static_cast<int>(i);
    }
    return -1;
}

void RemoveGroupAssignment(const std::string& groupName) {
    if (groupName.empty()) return;
    for (auto& row : g_rows) {
        if (row.w.groupName == groupName) row.w.groupName.clear();
    }
}

void RefreshGroupCombo(HWND hWnd) {
    HWND hCombo = GetDlgItem(hWnd, IDC_COMBO_GROUP);
    if (!hCombo) return;
    const int prevSel = static_cast<int>(SendMessageA(hCombo, CB_GETCURSEL, 0, 0));
    std::string prevName;
    if (prevSel > 0) {
        char buf[256]{};
        SendMessageA(hCombo, CB_GETLBTEXT, prevSel, reinterpret_cast<LPARAM>(buf));
        prevName = buf;
    }

    SendMessageA(hCombo, CB_RESETCONTENT, 0, 0);
    SendMessageA(hCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>("(no group)"));
    for (const auto& group : g_groups) {
        SendMessageA(hCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(group.g.name.c_str()));
    }
    if (!prevName.empty()) {
        const int idx = FindGroupIndexByName(prevName);
        if (idx >= 0) SendMessageA(hCombo, CB_SETCURSEL, static_cast<WPARAM>(idx + 1), 0);
        else SendMessageA(hCombo, CB_SETCURSEL, 0, 0);
    } else {
        SendMessageA(hCombo, CB_SETCURSEL, 0, 0);
    }
}

void WriteWidgetToEditor(HWND hWnd, int modelIndex) {
    HWND hName = GetDlgItem(hWnd, IDC_EDIT_NAME);
    HWND hAlways = GetDlgItem(hWnd, IDC_CHECK_ALWAYS);
    HWND hGroup = GetDlgItem(hWnd, IDC_COMBO_GROUP);
    HWND hCommands = GetDlgItem(hWnd, IDC_EDIT_COMMANDS);
    if (modelIndex < 0 || modelIndex >= static_cast<int>(g_rows.size())) {
        SetWindowTextA(hName, "");
        SendMessageA(hAlways, BM_SETCHECK, BST_UNCHECKED, 0);
        SendMessageA(hGroup, CB_SETCURSEL, 0, 0);
        SetWindowTextA(hCommands, "");
        return;
    }

    const Widget& w = g_rows[static_cast<size_t>(modelIndex)].w;
    SetWindowTextA(hName, w.name.c_str());
    SendMessageA(hAlways, BM_SETCHECK, w.alwaysVisible ? BST_CHECKED : BST_UNCHECKED, 0);
    const int groupIndex = FindGroupIndexByName(w.groupName);
    SendMessageA(hGroup, CB_SETCURSEL, static_cast<WPARAM>(groupIndex >= 0 ? groupIndex + 1 : 0), 0);

    std::ostringstream oss;
    for (size_t i = 0; i < w.commands.size(); ++i) {
        oss << w.commands[i];
        if (i + 1 < w.commands.size()) oss << "\r\n";
    }
    SetWindowTextA(hCommands, oss.str().c_str());
    SetStatus(hWnd, "Selected " + w.name + " (" + std::to_string(w.commands.size()) + " cmd)");
}

void ReadEditorToWidget(HWND hWnd, int modelIndex) {
    if (modelIndex < 0 || modelIndex >= static_cast<int>(g_rows.size())) return;
    Widget& w = g_rows[static_cast<size_t>(modelIndex)].w;
    w.name = Trim(ReadEditText(GetDlgItem(hWnd, IDC_EDIT_NAME)));
    w.alwaysVisible = (SendMessageA(GetDlgItem(hWnd, IDC_CHECK_ALWAYS), BM_GETCHECK, 0, 0) == BST_CHECKED);
    const int groupSel = static_cast<int>(SendMessageA(GetDlgItem(hWnd, IDC_COMBO_GROUP), CB_GETCURSEL, 0, 0));
    if (groupSel <= 0) w.groupName.clear();
    else if (groupSel - 1 < static_cast<int>(g_groups.size())) w.groupName = g_groups[static_cast<size_t>(groupSel - 1)].g.name;

    const std::string commandsText = ReadEditText(GetDlgItem(hWnd, IDC_EDIT_COMMANDS));
    std::istringstream iss(commandsText);
    std::string line;
    w.commands.clear();
    while (std::getline(iss, line)) {
        line = Trim(line);
        if (!line.empty()) w.commands.push_back(line);
    }
}

void WriteGroupToEditor(HWND hWnd, int groupIndex) {
    HWND hName = GetDlgItem(hWnd, IDC_EDIT_GROUP_NAME);
    HWND hAlways = GetDlgItem(hWnd, IDC_CHECK_GROUP_ALWAYS);
    if (groupIndex < 0 || groupIndex >= static_cast<int>(g_groups.size())) {
        SetWindowTextA(hName, "");
        SendMessageA(hAlways, BM_SETCHECK, BST_UNCHECKED, 0);
        return;
    }
    const Group& g = g_groups[static_cast<size_t>(groupIndex)].g;
    SetWindowTextA(hName, g.name.c_str());
    SendMessageA(hAlways, BM_SETCHECK, g.alwaysVisible ? BST_CHECKED : BST_UNCHECKED, 0);
}

void ReadEditorToGroup(HWND hWnd, int groupIndex) {
    if (groupIndex < 0 || groupIndex >= static_cast<int>(g_groups.size())) return;
    Group& g = g_groups[static_cast<size_t>(groupIndex)].g;
    const std::string oldName = g.name;
    g.name = Trim(ReadEditText(GetDlgItem(hWnd, IDC_EDIT_GROUP_NAME)));
    g.alwaysVisible = (SendMessageA(GetDlgItem(hWnd, IDC_CHECK_GROUP_ALWAYS), BM_GETCHECK, 0, 0) == BST_CHECKED);
    if (oldName != g.name) {
        for (auto& row : g_rows) {
            if (row.w.groupName == oldName) row.w.groupName = g.name;
        }
    }
}

void RefreshGroupList(HWND hWnd) {
    HWND hList = GetDlgItem(hWnd, IDC_LIST_GROUPS);
    SendMessageA(hList, LB_RESETCONTENT, 0, 0);
    int selectedListIndex = -1;
    for (size_t i = 0; i < g_groups.size(); ++i) {
        const Group& g = g_groups[i].g;
        std::string label = (g.visible ? "o " : "x ") + g.name + (g.alwaysVisible ? " ^" : "");
        SendMessageA(hList, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
        SendMessageA(hList, LB_SETITEMDATA, static_cast<WPARAM>(i), static_cast<LPARAM>(i));
        if (static_cast<int>(i) == g_selectedGroupIndex) selectedListIndex = static_cast<int>(i);
    }
    if (selectedListIndex >= 0) SendMessageA(hList, LB_SETCURSEL, selectedListIndex, 0);
    else g_selectedGroupIndex = -1;
    RefreshGroupCombo(hWnd);
}

void RefreshWidgetList(HWND hWnd) {
    HWND hList = GetDlgItem(hWnd, IDC_LIST_WIDGETS);
    SendMessageA(hList, LB_RESETCONTENT, 0, 0);

    int selectedListIndex = -1;
    for (size_t i = 0; i < g_rows.size(); ++i) {
        const Widget& w = g_rows[i].w;
        std::string label = (w.alwaysVisible ? "^ " : "  ") + w.name;
        if (!w.groupName.empty()) label += " /" + w.groupName;
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

void SaveCurrentEditorWidget(HWND hWnd) {
    if (g_selectedModelIndex >= 0 && g_selectedModelIndex < static_cast<int>(g_rows.size())) {
        ReadEditorToWidget(hWnd, g_selectedModelIndex);
        RefreshFloaterChrome(static_cast<size_t>(g_selectedModelIndex));
    }
}

bool SaveWidgetsToDisk(const std::string& path) {
    std::vector<Widget> widgets;
    widgets.reserve(g_rows.size());
    for (const auto& row : g_rows) widgets.push_back(row.w);
    std::vector<Group> groups;
    groups.reserve(g_groups.size());
    for (const auto& row : g_groups) groups.push_back(row.g);
    return SaveWidgetsFile(path, widgets, groups);
}

bool LoadWidgetsFromDisk(const std::string& path) {
    std::vector<Widget> w;
    std::vector<Group> g;
    const bool opened = LoadWidgetsFile(path, w, g);
    if (!opened) {
        w.clear();
        g.clear();
        EnsureDefaultWidgets(w);
    }
    g_rows.clear();
    g_groups.clear();
    for (auto& widget : w) g_rows.push_back({ widget, nullptr });
    for (auto& group : g) g_groups.push_back({ group, nullptr });
    return opened;
}

std::string GetSelectedCommandLine(HWND hWnd, int* lineIndexOut) {
    if (lineIndexOut) *lineIndexOut = -1;
    if (g_selectedModelIndex < 0 || g_selectedModelIndex >= static_cast<int>(g_rows.size())) return "";
    const Widget& w = g_rows[static_cast<size_t>(g_selectedModelIndex)].w;
    if (w.commands.empty()) return "";
    int line = CurrentCommandLineIndex(hWnd);
    if (line < 0) line = 0;
    if (line >= static_cast<int>(w.commands.size())) line = static_cast<int>(w.commands.size()) - 1;
    if (lineIndexOut) *lineIndexOut = line;
    return w.commands[static_cast<size_t>(line)];
}

bool AddCommandLineAtSelection(HWND hWnd) {
    if (g_selectedModelIndex < 0 || g_selectedModelIndex >= static_cast<int>(g_rows.size())) return false;
    SaveCurrentEditorWidget(hWnd);
    Widget& w = g_rows[static_cast<size_t>(g_selectedModelIndex)].w;
    int line = CurrentCommandLineIndex(hWnd);
    if (line < 0 || line >= static_cast<int>(w.commands.size())) {
        w.commands.push_back("echo new step");
    } else {
        w.commands.insert(w.commands.begin() + line + 1, "echo new step");
    }
    WriteWidgetToEditor(hWnd, g_selectedModelIndex);
    RefreshWidgetList(hWnd);
    return true;
}

bool DeleteCommandLineAtSelection(HWND hWnd) {
    if (g_selectedModelIndex < 0 || g_selectedModelIndex >= static_cast<int>(g_rows.size())) return false;
    SaveCurrentEditorWidget(hWnd);
    Widget& w = g_rows[static_cast<size_t>(g_selectedModelIndex)].w;
    if (w.commands.empty()) return false;
    int line = CurrentCommandLineIndex(hWnd);
    if (line < 0 || line >= static_cast<int>(w.commands.size())) line = static_cast<int>(w.commands.size()) - 1;
    w.commands.erase(w.commands.begin() + line);
    WriteWidgetToEditor(hWnd, g_selectedModelIndex);
    RefreshWidgetList(hWnd);
    return true;
}

bool MoveCommandLineAtSelection(HWND hWnd, bool up) {
    if (g_selectedModelIndex < 0 || g_selectedModelIndex >= static_cast<int>(g_rows.size())) return false;
    SaveCurrentEditorWidget(hWnd);
    Widget& w = g_rows[static_cast<size_t>(g_selectedModelIndex)].w;
    if (w.commands.size() < 2) return false;
    int line = CurrentCommandLineIndex(hWnd);
    if (line < 0 || line >= static_cast<int>(w.commands.size())) return false;
    const int target = up ? line - 1 : line + 1;
    if (target < 0 || target >= static_cast<int>(w.commands.size())) return false;
    std::swap(w.commands[static_cast<size_t>(line)], w.commands[static_cast<size_t>(target)]);
    WriteWidgetToEditor(hWnd, g_selectedModelIndex);
    RefreshWidgetList(hWnd);
    return true;
}

void LayoutNexusUi(HWND hWnd, UINT dpi) {
    const int d = static_cast<int>(dpi);
    const int densityPct = g_compactDensity ? 90 : 100;
    const auto P = [d, densityPct](int x) { return MulDiv(MulDiv(x, densityPct, 100), d, 96); };
    RECT rc{};
    GetClientRect(hWnd, &rc);
    const int winW = rc.right - rc.left;
    const int winH = rc.bottom - rc.top;
    const int margin = P(16);
    const int top = P(62);
    const int statusH = P(24);
    const int statusY = std::max(top + P(320), winH - margin - statusH);
    const int contentBottom = statusY - P(8);
    const int colGap = P(14);
    const int leftMin = P(300);
    const int leftMax = P(420);
    int leftW = (winW - margin * 2 - colGap) * 36 / 100;
    leftW = std::max(leftMin, std::min(leftMax, leftW));
    if (leftW > winW - margin * 2 - P(280)) leftW = std::max(P(220), winW - margin * 2 - P(280));
    const int leftX = margin;
    const int rightX = leftX + leftW + colGap;
    const int rightW = std::max(P(260), winW - rightX - margin);
    const int rowH = P(32);
    const int btnGap = P(6);
    const int leftButtonsY = contentBottom - rowH;
    const int checkY = top;
    const int listY = checkY + P(30);
    const int listH = std::max(P(140), leftButtonsY - listY - P(8));

    auto pos = [&](int id, int x, int y, int w, int h) {
        if (HWND c = GetDlgItem(hWnd, id)) SetWindowPos(c, nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
    };

    pos(IDC_SHOW_NON_PINNED, leftX, checkY, leftW, P(24));
    pos(IDC_LIST_WIDGETS, leftX, listY, leftW, listH);

    const int leftBtnW = std::max(P(56), (leftW - btnGap * 4) / 5);
    pos(IDC_BTN_RUN, leftX, leftButtonsY, leftBtnW, rowH);
    pos(IDC_BTN_ADD, leftX + (leftBtnW + btnGap), leftButtonsY, leftBtnW, rowH);
    pos(IDC_BTN_DELETE, leftX + 2 * (leftBtnW + btnGap), leftButtonsY, leftBtnW, rowH);
    pos(IDC_BTN_SAVE, leftX + 3 * (leftBtnW + btnGap), leftButtonsY, leftBtnW, rowH);
    pos(IDC_BTN_HIDE_NEXUS, leftX + 4 * (leftBtnW + btnGap), leftButtonsY, leftBtnW, rowH);

    const int groupsW = std::max(P(128), rightW * 28 / 100);
    const int detailsW = std::max(P(220), rightW - groupsW - P(10));
    const int detailsX = rightX;
    const int groupsX = detailsX + detailsW + P(10);

    const int headerY = top;
    const int headerH = P(26);
    const int nameLblW = P(56);
    const int groupLblW = P(50);
    const int groupComboW = std::max(P(110), detailsW - P(260));
    const int nameEditW = std::max(P(100), detailsW - nameLblW - groupLblW - groupComboW - P(22));
    const int nameEditX = detailsX + nameLblW + P(4);
    const int groupLblX = nameEditX + nameEditW + P(8);
    const int groupComboX = groupLblX + groupLblW + P(4);

    pos(IDC_LBL_NAME, detailsX, headerY + P(2), nameLblW, headerH);
    pos(IDC_EDIT_NAME, nameEditX, headerY, nameEditW, headerH + P(2));
    pos(IDC_LBL_GROUP, groupLblX, headerY + P(2), groupLblW, headerH);
    pos(IDC_COMBO_GROUP, groupComboX, headerY, groupComboW, P(260));
    pos(IDC_CHECK_ALWAYS, detailsX, headerY + P(32), detailsW, P(22));

    const int cmdLblY = headerY + P(64);
    pos(IDC_LBL_COMMANDS, detailsX, cmdLblY, detailsW, P(20));
    const int cmdButtonsY = contentBottom - rowH;
    const int cmdTop = cmdLblY + P(24);
    const int cmdH = std::max(P(90), cmdButtonsY - cmdTop - P(8));
    pos(IDC_EDIT_COMMANDS, detailsX, cmdTop, detailsW, cmdH);

    const int cmdRunW = std::max(P(96), detailsW / 3);
    const int cmdSmallW = std::max(P(52), (detailsW - cmdRunW - btnGap * 4) / 4);
    pos(IDC_BTN_CMD_ADD, detailsX, cmdButtonsY, cmdSmallW, rowH);
    pos(IDC_BTN_CMD_DEL, detailsX + (cmdSmallW + btnGap), cmdButtonsY, cmdSmallW, rowH);
    pos(IDC_BTN_CMD_UP, detailsX + 2 * (cmdSmallW + btnGap), cmdButtonsY, cmdSmallW, rowH);
    pos(IDC_BTN_CMD_DOWN, detailsX + 3 * (cmdSmallW + btnGap), cmdButtonsY, cmdSmallW, rowH);
    pos(IDC_BTN_CMD_RUN, detailsX + 4 * (cmdSmallW + btnGap), cmdButtonsY, cmdRunW, rowH);

    pos(IDC_LBL_GROUPS, groupsX, cmdLblY, groupsW, P(20));
    const int groupsListY = cmdTop;
    const int groupsBottomControlsH = P(24) + P(22) + rowH + P(12);
    const int groupsListH = std::max(P(80), contentBottom - groupsListY - groupsBottomControlsH);
    pos(IDC_LIST_GROUPS, groupsX, groupsListY, groupsW, groupsListH);
    const int groupNameY = groupsListY + groupsListH + P(8);
    pos(IDC_EDIT_GROUP_NAME, groupsX, groupNameY, groupsW, P(24));
    pos(IDC_CHECK_GROUP_ALWAYS, groupsX, groupNameY + P(28), groupsW, P(22));
    const int gBtnW = (groupsW - btnGap) / 2;
    pos(IDC_BTN_ADD_GROUP, groupsX, contentBottom - rowH, gBtnW, rowH);
    pos(IDC_BTN_DELETE_GROUP, groupsX + gBtnW + btnGap, contentBottom - rowH, groupsW - gBtnW - btnGap, rowH);

    pos(IDC_STATIC_STATUS, margin, statusY, winW - margin * 2, statusH);

    if (HWND hList = GetDlgItem(hWnd, IDC_LIST_WIDGETS)) {
        SendMessageA(hList, LB_SETITEMHEIGHT, static_cast<WPARAM>(g_listItemHeight), 0);
    }
    if (HWND hCmd = GetDlgItem(hWnd, IDC_EDIT_COMMANDS)) {
        SendMessageA(hCmd, WM_SETFONT, reinterpret_cast<WPARAM>(g_fontMono), TRUE);
    }
    ApplyNeonControlSkin(GetDlgItem(hWnd, IDC_SHOW_NON_PINNED));
    ApplyNeonControlSkin(GetDlgItem(hWnd, IDC_LIST_WIDGETS));
    ApplyNeonControlSkin(GetDlgItem(hWnd, IDC_EDIT_NAME));
    ApplyNeonControlSkin(GetDlgItem(hWnd, IDC_COMBO_GROUP));
    ApplyNeonControlSkin(GetDlgItem(hWnd, IDC_CHECK_ALWAYS));
    ApplyNeonControlSkin(GetDlgItem(hWnd, IDC_EDIT_COMMANDS));
    ApplyNeonControlSkin(GetDlgItem(hWnd, IDC_LIST_GROUPS));
    ApplyNeonControlSkin(GetDlgItem(hWnd, IDC_EDIT_GROUP_NAME));
    ApplyNeonControlSkin(GetDlgItem(hWnd, IDC_CHECK_GROUP_ALWAYS));
    ApplyNeonControlSkin(GetDlgItem(hWnd, IDC_STATIC_STATUS));
    ApplyNeonControlSkin(GetDlgItem(hWnd, IDC_LBL_NAME));
    ApplyNeonControlSkin(GetDlgItem(hWnd, IDC_LBL_GROUP));
    ApplyNeonControlSkin(GetDlgItem(hWnd, IDC_LBL_COMMANDS));
    ApplyNeonControlSkin(GetDlgItem(hWnd, IDC_LBL_GROUPS));
}
