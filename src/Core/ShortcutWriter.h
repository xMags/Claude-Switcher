#pragma once

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace ClaudeSwitcher::Core
{
    struct ShortcutSpec
    {
        std::filesystem::path Location;
        std::filesystem::path Target;
        std::wstring Arguments;
        std::wstring Description;
        std::filesystem::path WorkingDirectory;
        // Optional .ico. Without one the shell shows the target's icon, which
        // for an app execution alias is a generic blank window.
        std::filesystem::path IconPath;
    };

    enum class ShortcutLocation
    {
        Desktop,
        // Start Menu\Programs\Claude Profiles, so every profile sits in one
        // folder under All apps.
        StartMenu,
    };

    inline constexpr std::array<ShortcutLocation, 2> AllShortcutLocations{
        ShortcutLocation::Desktop,
        ShortcutLocation::StartMenu,
    };

    // Replaces characters Windows does not allow in file names, and trailing
    // dots and spaces, with underscores.
    [[nodiscard]] std::wstring SanitizeFileName(std::wstring_view name);

    // "Claude (<profile name>).lnk"
    [[nodiscard]] std::wstring ShortcutFileNameFor(std::wstring_view profileName);

    // The folder shortcuts of a location go in. Throws std::system_error.
    [[nodiscard]] std::filesystem::path ShortcutFolderFor(ShortcutLocation location);

    // ShortcutFolderFor(location) / ShortcutFileNameFor(profileName).
    [[nodiscard]] std::filesystem::path ShortcutPathFor(ShortcutLocation location, std::wstring_view profileName);

    // Creates or replaces a .lnk file, creating its folder if needed. Needs
    // COM initialized on the calling thread. Throws std::system_error.
    void WriteShortcut(ShortcutSpec const& spec);

    // The arguments stored in a .lnk file, or nothing when the file is
    // missing or not a readable shortcut. Needs COM on the calling thread.
    [[nodiscard]] std::optional<std::wstring> ReadShortcutArguments(std::filesystem::path const& location);
}
