#include "Core/ClaudeLauncher.h"

#include "Core/CommandLineParsing.h"
#include "Core/KnownFolders.h"
#include "Core/SystemError.h"

#include <windows.h>

#include <wil/resource.h>

#pragma comment(lib, "user32.lib")

namespace ClaudeSwitcher::Core
{
    std::wstring BuildClaudeArguments(std::optional<std::filesystem::path> const& userDataDir)
    {
        if (!userDataDir)
        {
            return {};
        }
        return QuoteCommandLineArgument(L"--user-data-dir=" + userDataDir->wstring());
    }

    void LaunchClaude(
        ClaudeInstallation const& installation,
        std::optional<std::filesystem::path> const& userDataDir,
        std::optional<std::uint32_t> runningProcessId)
    {
        if (runningProcessId)
        {
            // Best effort: without it Windows may only flash the taskbar
            // button instead of raising the window.
            static_cast<void>(AllowSetForegroundWindow(*runningProcessId));
        }

        auto commandLine = QuoteCommandLineArgument(installation.AliasPath.wstring());
        auto const arguments = BuildClaudeArguments(userDataDir);
        if (!arguments.empty())
        {
            commandLine += L' ';
            commandLine += arguments;
        }

        // Claude inherits this as its current directory. The user profile is
        // used rather than the profile folder, which a process's current
        // directory would keep from being deleted.
        auto const workingDirectory = GetKnownFolderPath(KnownFolder::UserProfile);

        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        if (!CreateProcessW(
                installation.AliasPath.c_str(),
                commandLine.data(),
                nullptr,
                nullptr,
                FALSE,
                0,
                nullptr,
                workingDirectory.c_str(),
                &startup,
                &process))
        {
            ThrowLastWin32Error("Starting Claude Desktop");
        }
        wil::unique_handle processHandle{ process.hProcess };
        wil::unique_handle threadHandle{ process.hThread };
    }
}
