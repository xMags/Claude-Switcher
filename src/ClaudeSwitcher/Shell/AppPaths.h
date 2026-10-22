#pragma once

#include <filesystem>

namespace ClaudeSwitcher::Shell
{
    // Full path of ClaudeSwitcher.exe; empty if Windows cannot report it.
    [[nodiscard]] std::filesystem::path ExecutablePath();

    // Folder holding ClaudeSwitcher.exe; empty if Windows cannot report it.
    [[nodiscard]] std::filesystem::path ExecutableDirectory();

    // The bundled app icon next to the executable.
    [[nodiscard]] std::filesystem::path BundledAppIcon();

    // A copy of the app icon at a fixed place for shortcuts to point at, so
    // they keep their icon when the switcher folder moves. Refreshed on each
    // call; empty when it cannot be written, in which case shortcuts simply
    // get the shell's default icon.
    [[nodiscard]] std::filesystem::path EnsureShortcutIcon() noexcept;
}
