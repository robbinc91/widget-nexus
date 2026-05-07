#include "command_runner.hpp"

#include "app_ids.hpp"
#include "globals.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstring>
#include <thread>
#include <vector>

namespace {

std::atomic<bool> g_runBusy{false};

bool PostRunStatus(HWND h, const std::string& msg) {
    if (!h) return false;
    const size_t n = msg.size() + 1;
    char* p = new (std::nothrow) char[n];
    if (!p) return false;
    memcpy(p, msg.c_str(), n);
    if (!PostMessageA(h, WM_APP_CMD_STATUS, 0, reinterpret_cast<LPARAM>(p))) {
        delete[] p;
        return false;
    }
    return true;
}

} // namespace

bool RunSingleCommand(const std::string& command, std::string& output, DWORD waitMs) {
    (void)waitMs;
    STARTUPINFOA si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    // Dedicated console per command; /C avoids leaving an extra idle wrapper shell.
    std::string cmdLine = "cmd.exe /C " + command;
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
        &pi);
    if (!ok) {
        output = "Failed to launch dedicated command console.";
        return false;
    }

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    output = "Command launched in dedicated console window.";
    return true;
}

bool CommandRunnerIsBusy() { return g_runBusy.load(std::memory_order_acquire); }

void RequestRunSingleCommand(HWND notifyHwnd, const std::string& command, const std::string& label) {
    if (!notifyHwnd || command.empty()) return;
    bool expected = false;
    if (!g_runBusy.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        PostRunStatus(notifyHwnd, "Another run is still in progress.");
        return;
    }
    std::thread([notifyHwnd, command, label]() {
        PostRunStatus(notifyHwnd, "Running: " + label);
        std::string out;
        if (!RunSingleCommand(command, out, kCommandWaitMs)) {
            PostRunStatus(notifyHwnd, "Failed: " + label);
            g_runBusy.store(false, std::memory_order_release);
            PostMessageA(notifyHwnd, WM_APP_CMD_DONE, 0, 0);
            return;
        }
        PostRunStatus(notifyHwnd, "Completed: " + label);
        g_runBusy.store(false, std::memory_order_release);
        PostMessageA(notifyHwnd, WM_APP_CMD_DONE, 1, 0);
    }).detach();
}

void RequestRunWidget(HWND notifyHwnd, int modelIndex) {
    if (!notifyHwnd) return;

    bool expected = false;
    if (!g_runBusy.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        PostRunStatus(notifyHwnd, "Another run is still in progress.");
        return;
    }

    std::string name;
    std::vector<std::string> cmds;
    {
        if (modelIndex < 0 || modelIndex >= static_cast<int>(g_rows.size())) {
            g_runBusy.store(false, std::memory_order_release);
            return;
        }
        name = g_rows[static_cast<size_t>(modelIndex)].w.name;
        cmds = g_rows[static_cast<size_t>(modelIndex)].w.commands;
    }

    if (cmds.empty()) {
        g_runBusy.store(false, std::memory_order_release);
        PostRunStatus(notifyHwnd, "No commands to run.");
        return;
    }

    std::thread([notifyHwnd, name, cmds]() {
        PostRunStatus(notifyHwnd, "Running: " + name);
        for (size_t i = 0; i < cmds.size(); ++i) {
            std::string step = "Running " + name + " (" + std::to_string(i + 1) + "/" + std::to_string(cmds.size()) + ")";
            PostRunStatus(notifyHwnd, step);
            std::string out;
            if (!RunSingleCommand(cmds[i], out, kCommandWaitMs)) {
                PostRunStatus(notifyHwnd, "Failed: " + name);
                g_runBusy.store(false, std::memory_order_release);
                PostMessageA(notifyHwnd, WM_APP_CMD_DONE, 0, 0);
                return;
            }
        }
        PostRunStatus(notifyHwnd, "Completed: " + name);
        g_runBusy.store(false, std::memory_order_release);
        PostMessageA(notifyHwnd, WM_APP_CMD_DONE, 1, 0);
    }).detach();
}
