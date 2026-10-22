#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ClaudeSwitcher::Core
{
    struct ClaudeInstance
    {
        std::uint32_t ProcessId = 0;
        // Absent for the default session.
        std::optional<std::wstring> UserDataDir;
    };

    // Lists the running Claude Desktop sessions, one entry per main process.
    // A process counts only when it runs the package's own Claude.exe with
    // the package identity: that excludes Chromium helpers (--type=), the
    // Claude Code CLI (also named claude.exe, sometimes started by Claude
    // Desktop itself and so carrying its identity) and anything else that
    // merely shares the name. Processes that cannot be inspected are skipped.
    // Safe to call from any thread.
    [[nodiscard]] std::vector<ClaudeInstance> ScanClaudeInstances(std::wstring_view packageFamilyName);
}
