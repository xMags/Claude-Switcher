#pragma once

#include "Core/ClaudeInstallLocator.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace ClaudeSwitcher::Core
{
    // The arguments for one Claude session: none for the default session,
    // otherwise a single --user-data-dir switch for the profile's folder.
    [[nodiscard]] std::wstring BuildClaudeArguments(std::optional<std::filesystem::path> const& userDataDir);

    // Starts Claude for a session. When that session is already running,
    // Claude's single-instance lock hands the start to the running process,
    // which brings its own window forward; runningProcessId lets that process
    // take the foreground. Throws std::system_error when Windows cannot start
    // the alias.
    void LaunchClaude(
        ClaudeInstallation const& installation,
        std::optional<std::filesystem::path> const& userDataDir,
        std::optional<std::uint32_t> runningProcessId);
}
