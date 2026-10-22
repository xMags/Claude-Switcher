#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace ClaudeSwitcher::Core
{
    // Package family of the Claude Desktop MSIX build.
    inline constexpr std::wstring_view ClaudePackageFamilyName = L"Claude_pzs8sxrjxfjjc";

    struct ClaudeInstallation
    {
        std::wstring PackageFullName;
        std::wstring Version;
        std::filesystem::path InstallDirectory;
        // The "claude-desktop.exe" app execution alias. Starting Claude
        // through it keeps the package identity Claude relies on, which a
        // direct start of the executable inside WindowsApps would lose.
        std::filesystem::path AliasPath;
    };

    enum class InstallStatus
    {
        Found,
        NotInstalled,
        // Installed, but the user turned the execution alias off in
        // Settings > Apps > Advanced app settings > App execution aliases.
        AliasMissing,
    };

    struct InstallLookup
    {
        InstallStatus Status = InstallStatus::NotInstalled;
        std::optional<ClaudeInstallation> Installation;
    };

    // Picks the highest installed version for the current user. Cheap enough
    // to call before every launch, so an update between launches is picked up.
    [[nodiscard]] InstallLookup LocateClaudeInstallation();

    // The executable inside an installed package, as named in Claude's manifest.
    [[nodiscard]] std::filesystem::path ClaudeExecutableIn(std::filesystem::path const& installDirectory);

    // Where Claude keeps the data of its default (no --user-data-dir) session.
    [[nodiscard]] std::filesystem::path DefaultClaudeDataDirectory();
}
