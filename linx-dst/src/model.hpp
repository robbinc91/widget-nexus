#pragma once

#include <string>
#include <vector>

struct Widget {
    std::string name;
    bool alwaysVisible = false;
    std::string groupName;
    std::vector<std::string> commands;
};

struct Group {
    std::string name;
    bool alwaysVisible = false;
    bool visible = true;
};

std::string Trim(const std::string& s);

std::string ConfigPathBesideExecutable();

void EnsureDefaultWidgets(std::vector<Widget>& widgets);

bool LoadWidgets(const std::string& path, std::vector<Widget>& widgets, std::vector<Group>& groups);

bool SaveWidgets(const std::string& path, const std::vector<Widget>& widgets, const std::vector<Group>& groups);

bool RunSingleCommand(const std::string& command, std::string& output);
