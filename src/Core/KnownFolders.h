#pragma once

#include <filesystem>

namespace ClaudeSwitcher::Core
{
    enum class KnownFolder
    {
        LocalAppData,
        LocalAppDataLow,
        RoamingAppData,
        UserProfile,
        Desktop,
        // The current user's Start Menu\Programs.
        StartMenuPrograms,
    };

    // Throws std::system_error when the shell cannot resolve the folder.
    [[nodiscard]] std::filesystem::path GetKnownFolderPath(KnownFolder folder);
}
