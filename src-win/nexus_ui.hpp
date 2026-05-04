#pragma once

#include <windows.h>

#include <string>

void SetStatus(HWND hWnd, const std::string& text);
std::string ReadEditText(HWND hEdit);
void RefreshGroupCombo(HWND hWnd);
void WriteWidgetToEditor(HWND hWnd, int modelIndex);
void ReadEditorToWidget(HWND hWnd, int modelIndex);
void WriteGroupToEditor(HWND hWnd, int groupIndex);
void ReadEditorToGroup(HWND hWnd, int groupIndex);
void RefreshGroupList(HWND hWnd);
void RefreshWidgetList(HWND hWnd);
void SaveCurrentEditorWidget(HWND hWnd);

int FindGroupIndexByName(const std::string& groupName);
void RemoveGroupAssignment(const std::string& groupName);

bool SaveWidgetsToDisk(const std::string& path);
bool LoadWidgetsFromDisk(const std::string& path);

std::string GetSelectedCommandLine(HWND hWnd, int* lineIndexOut = nullptr);
bool AddCommandLineAtSelection(HWND hWnd);
bool DeleteCommandLineAtSelection(HWND hWnd);
bool MoveCommandLineAtSelection(HWND hWnd, bool up);

void LayoutNexusUi(HWND hWnd, UINT dpi);
