#pragma once

#include <windows.h>

#include <string>

constexpr DWORD kCommandWaitMs = 15 * 60 * 1000;

bool RunSingleCommand(const std::string& command, std::string& output, DWORD waitMs = kCommandWaitMs);

void RequestRunWidget(HWND notifyHwnd, int modelIndex);
void RequestRunSingleCommand(HWND notifyHwnd, const std::string& command, const std::string& label);

bool CommandRunnerIsBusy();
