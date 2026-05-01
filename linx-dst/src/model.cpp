#include "model.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <unistd.h>
#include <sys/wait.h>

std::string Trim(const std::string& s) {
    const auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    const auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

std::string ConfigPathBesideExecutable() {
    char buf[4096]{};
    const ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) return std::string("widgets.txt");
    buf[n] = '\0';
    std::string path(buf);
    const size_t slash = path.find_last_of('/');
    if (slash == std::string::npos) return std::string("widgets.txt");
    return path.substr(0, slash + 1) + "widgets.txt";
}

void EnsureDefaultWidgets(std::vector<Widget>& widgets) {
    if (!widgets.empty()) return;

    Widget ssh;
    ssh.name = "Shell profile";
    ssh.alwaysVisible = false;
    ssh.commands.push_back("echo Hello from Widget Nexus");
    widgets.push_back(ssh);

    Widget edit;
    edit.name = "List home";
    edit.alwaysVisible = false;
    edit.commands.push_back("ls -la \"$HOME\"");
    widgets.push_back(edit);
}

static int FindGroupIndexByName(const std::vector<Group>& groups, const std::string& groupName) {
    if (groupName.empty()) return -1;
    for (size_t i = 0; i < groups.size(); ++i) {
        if (groups[i].name == groupName) return static_cast<int>(i);
    }
    return -1;
}

bool LoadWidgets(const std::string& path, std::vector<Widget>& widgets, std::vector<Group>& groups) {
    widgets.clear();
    groups.clear();
    std::ifstream in(path);
    if (!in.is_open()) {
        EnsureDefaultWidgets(widgets);
        return false;
    }

    Widget currentWidget;
    Group currentGroup;
    enum class Section { None, Widget, Group };
    Section section = Section::None;
    std::string line;
    while (std::getline(in, line)) {
        line = Trim(line);
        if (line.empty()) continue;

        if (line == "[Widget]") {
            if (section == Section::Widget) widgets.push_back(currentWidget);
            else if (section == Section::Group && !currentGroup.name.empty()) groups.push_back(currentGroup);
            currentWidget = Widget{};
            section = Section::Widget;
            continue;
        }

        if (line == "[Group]") {
            if (section == Section::Widget) widgets.push_back(currentWidget);
            else if (section == Section::Group && !currentGroup.name.empty()) groups.push_back(currentGroup);
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

    if (section == Section::Widget) widgets.push_back(currentWidget);
    else if (section == Section::Group && !currentGroup.name.empty()) groups.push_back(currentGroup);

    groups.erase(std::remove_if(groups.begin(), groups.end(), [](const Group& g) { return g.name.empty(); }), groups.end());
    widgets.erase(std::remove_if(widgets.begin(), widgets.end(), [](const Widget& w) { return w.name.empty(); }), widgets.end());
    for (auto& w : widgets) {
        if (FindGroupIndexByName(groups, w.groupName) < 0) w.groupName.clear();
    }
    EnsureDefaultWidgets(widgets);
    return true;
}

bool SaveWidgets(const std::string& path, const std::vector<Widget>& widgets, const std::vector<Group>& groups) {
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

static bool LooksInteractive(const std::string& command) {
    std::string lower = command;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lower.find("ssh ") != std::string::npos ||
           lower.find("telnet") != std::string::npos ||
           lower.find("vim ") != std::string::npos ||
           lower.find("nano ") != std::string::npos ||
           lower.find("less ") != std::string::npos ||
           lower.find("top ") != std::string::npos ||
           lower == "top";
}

static bool LaunchInTerminal(const std::string& command, std::string& output) {
    const char* term = std::getenv("TERMINAL");
    if (!term || !term[0]) term = "x-terminal-emulator";

    pid_t pid = fork();
    if (pid == -1) {
        output = "fork() failed.";
        return false;
    }
    if (pid == 0) {
        setsid();
        execlp(term, term, "-e", "/bin/bash", "-lc", command.c_str(), static_cast<char*>(nullptr));
        execlp("x-terminal-emulator", "x-terminal-emulator", "-e", "/bin/bash", "-lc", command.c_str(),
               static_cast<char*>(nullptr));
        execlp("gnome-terminal", "gnome-terminal", "--", "bash", "-lc", command.c_str(), static_cast<char*>(nullptr));
        _exit(127);
    }
    output = "Launched in terminal.";
    return true;
}

bool RunSingleCommand(const std::string& command, std::string& output) {
    if (LooksInteractive(command)) return LaunchInTerminal(command, output);

    int pipefd[2]{};
    if (pipe(pipefd) != 0) return false;

    pid_t pid = fork();
    if (pid == -1) {
        close(pipefd[0]);
        close(pipefd[1]);
        return false;
    }

    if (pid == 0) {
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[0]);
        close(pipefd[1]);
        execl("/bin/sh", "sh", "-c", command.c_str(), static_cast<char*>(nullptr));
        _exit(127);
    }

    close(pipefd[1]);
    output.clear();
    char buf[4096]{};
    ssize_t n = 0;
    while ((n = read(pipefd[0], buf, sizeof(buf) - 1)) > 0) {
        buf[n] = '\0';
        output += buf;
    }
    close(pipefd[0]);

    int status = 0;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}
