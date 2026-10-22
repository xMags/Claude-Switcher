#pragma once

#include <windows.h>

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace ClaudeSwitcher::Core
{
    enum class RootProblem
    {
        None,
        NotAbsolute,
        NetworkPath,
        // Claude's MSIX package redirects its writes under AppData into a
        // private copy, so a profile there would silently live elsewhere.
        InsideAppData,
        DriveUnavailable,
    };

    // %USERPROFILE%\ClaudeProfiles
    [[nodiscard]] std::filesystem::path DefaultProfilesRoot();

    // %LOCALAPPDATA%\ClaudeSwitcher\profiles.json. The switcher itself is not
    // packaged, so its own AppData writes are not redirected.
    [[nodiscard]] std::filesystem::path SwitcherSettingsFile();

    // Lexical checks only, so they can be tested without the file system.
    [[nodiscard]] RootProblem ClassifyProfilesRoot(std::wstring_view root, std::vector<std::wstring> const& appDataFolders);

    // ClassifyProfilesRoot against this user's AppData folders, plus a check
    // that the drive is present and local.
    [[nodiscard]] RootProblem CheckProfilesRoot(std::filesystem::path const& root);

    // True when child is exactly one level below parent (lexically).
    [[nodiscard]] bool IsDirectChildOf(std::filesystem::path const& child, std::filesystem::path const& parent);

    // Names of the folders already present in root; empty if root is missing.
    [[nodiscard]] std::vector<std::wstring> ListFolderNames(std::filesystem::path const& root);

    enum class RecycleOutcome
    {
        Recycled,
        Cancelled,
        AlreadyGone,
    };

    // Moves a folder to the Recycle Bin. When it cannot be recycled (too
    // large, or on a drive without a bin) the shell asks before deleting it
    // for good. Must run on an STA thread; shows progress owned by owner.
    // Throws std::system_error.
    [[nodiscard]] RecycleOutcome RecycleFolder(std::filesystem::path const& folder, HWND owner);
}
