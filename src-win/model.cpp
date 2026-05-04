#include "model.hpp"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <fstream>

std::string Trim(const std::string& s) {
    const auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    const auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

void InitConfigPathBesideExe(std::string& configPath) {
    char exe[MAX_PATH]{};
    const DWORD n = GetModuleFileNameA(nullptr, exe, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return;
    std::string path(exe, exe + static_cast<size_t>(n));
    const size_t slash = path.find_last_of("\\/");
    if (slash == std::string::npos) return;
    configPath = path.substr(0, slash + 1) + "widgets.txt";
}

void EnsureDefaultWidgets(std::vector<Widget>& widgets) {
    if (!widgets.empty()) return;

    Widget ssh;
    ssh.name = "SSH on WSL";
    ssh.alwaysVisible = true;
    ssh.commands.push_back("wsl.exe -e bash -lc \"ssh user@server\"");
    widgets.push_back(ssh);

    Widget wsl;
    wsl.name = "Open WSL Home";
    wsl.alwaysVisible = false;
    wsl.commands.push_back("wsl.exe -e bash -lc \"cd ~ && exec bash\"");
    widgets.push_back(wsl);
}

bool LoadWidgetsFile(const std::string& path, std::vector<Widget>& outWidgets, std::vector<Group>& outGroups) {
    outWidgets.clear();
    outGroups.clear();
    std::ifstream in(path);
    if (!in.is_open()) return false;

    Widget currentWidget;
    Group currentGroup;
    enum class Section { None, Widget, Group };
    Section section = Section::None;
    std::string line;
    while (std::getline(in, line)) {
        line = Trim(line);
        if (line.empty()) continue;

        if (line == "[Widget]") {
            if (section == Section::Widget) outWidgets.push_back(currentWidget);
            else if (section == Section::Group && !currentGroup.name.empty()) outGroups.push_back(currentGroup);
            currentWidget = Widget{};
            section = Section::Widget;
            continue;
        }

        if (line == "[Group]") {
            if (section == Section::Widget) outWidgets.push_back(currentWidget);
            else if (section == Section::Group && !currentGroup.name.empty()) outGroups.push_back(currentGroup);
            currentGroup = Group{};
            section = Section::Group;
            continue;
        }

        if (section == Section::None) continue;
        const auto pos = line.find('=');
        if (pos == std::string::npos) continue;
        const std::string key = Trim(line.substr(0, pos));
        const std::string value = Trim(line.substr(pos + 1));

        if (section == Section::Widget) {
            if (key == "name") currentWidget.name = value;
            else if (key == "alwaysVisible") currentWidget.alwaysVisible = (value == "1" || value == "true" || value == "True");
            else if (key == "group") currentWidget.groupName = value;
            else if (key == "command") currentWidget.commands.push_back(value);
            continue;
        }

        if (key == "name") currentGroup.name = value;
        else if (key == "alwaysVisible") currentGroup.alwaysVisible = (value == "1" || value == "true" || value == "True");
        else if (key == "visible") currentGroup.visible = (value == "1" || value == "true" || value == "True");
    }

    if (section == Section::Widget) outWidgets.push_back(currentWidget);
    else if (section == Section::Group && !currentGroup.name.empty()) outGroups.push_back(currentGroup);

    outGroups.erase(std::remove_if(outGroups.begin(), outGroups.end(), [](const Group& g) { return g.name.empty(); }), outGroups.end());
    outWidgets.erase(std::remove_if(outWidgets.begin(), outWidgets.end(), [](const Widget& w) { return w.name.empty(); }), outWidgets.end());

    auto findGroup = [&](const std::string& name) -> int {
        if (name.empty()) return -1;
        for (size_t i = 0; i < outGroups.size(); ++i) {
            if (outGroups[i].name == name) return static_cast<int>(i);
        }
        return -1;
    };
    for (auto& w : outWidgets) {
        if (findGroup(w.groupName) < 0) w.groupName.clear();
    }

    EnsureDefaultWidgets(outWidgets); // no-op when file contained widgets
    return true;
}

bool SaveWidgetsFile(const std::string& path, const std::vector<Widget>& widgets, const std::vector<Group>& groups) {
    std::ofstream out(path, std::ios::trunc);
    if (!out.is_open()) return false;
    for (const Widget& w : widgets) {
        out << "[Widget]\n";
        out << "name=" << w.name << "\n";
        out << "alwaysVisible=" << (w.alwaysVisible ? "1" : "0") << "\n";
        if (!w.groupName.empty()) out << "group=" << w.groupName << "\n";
        for (const auto& cmd : w.commands) out << "command=" << cmd << "\n";
        out << "\n";
    }
    for (const Group& g : groups) {
        out << "[Group]\n";
        out << "name=" << g.name << "\n";
        out << "alwaysVisible=" << (g.alwaysVisible ? "1" : "0") << "\n";
        out << "visible=" << (g.visible ? "1" : "0") << "\n";
        out << "\n";
    }
    return true;
}
