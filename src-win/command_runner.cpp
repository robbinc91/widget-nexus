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
            &pi);

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

    const DWORD waitResult = (waitMs == INFINITE) ? WaitForSingleObject(pi.hProcess, INFINITE) : WaitForSingleObject(pi.hProcess, waitMs);
    if (waitResult == WAIT_TIMEOUT) {
        TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        CloseHandle(hRead);
        output = "Command timed out.";
        return false;
    }

    char buffer[4096];
    DWORD bytesRead = 0;
    output.clear();
    while (ReadFile(hRead, buffer, sizeof(buffer) - 1, &bytesRead, nullptr) && bytesRead > 0) {
        buffer[bytesRead] = '\0';
        output += buffer;
        if (output.size() > 256 * 1024) break;
    }

    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    CloseHandle(hRead);
    return exitCode == 0;
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
