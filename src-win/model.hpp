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

void InitConfigPathBesideExe(std::string& configPath);

void EnsureDefaultWidgets(std::vector<Widget>& widgets);

bool LoadWidgetsFile(const std::string& path, std::vector<Widget>& outWidgets, std::vector<Group>& outGroups);

bool SaveWidgetsFile(const std::string& path, const std::vector<Widget>& widgets, const std::vector<Group>& groups);
