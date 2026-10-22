#pragma once

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
    };

    // Replaces characters Windows does not allow in file names, and trailing
    // dots and spaces, with underscores.
    [[nodiscard]] std::wstring SanitizeFileName(std::wstring_view name);

    // Desktop\Claude (<profile name>).lnk
    [[nodiscard]] std::filesystem::path DesktopShortcutPathFor(std::wstring_view profileName);

    // Creates or replaces a .lnk file. Needs COM initialized on the calling
    // thread. Throws std::system_error.
    void WriteShortcut(ShortcutSpec const& spec);

    // The arguments stored in a .lnk file, or nothing when the file is
    // missing or not a readable shortcut. Needs COM on the calling thread.
    [[nodiscard]] std::optional<std::wstring> ReadShortcutArguments(std::filesystem::path const& location);
}
