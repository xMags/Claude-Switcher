#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace ClaudeSwitcher::Core
{
    enum class ThemePreference
    {
        System,
        Light,
        Dark,
    };

    // The switcher's own preferences. Kept apart from the profile list, so a
    // damaged preferences file can never cost anyone their profiles.
    struct AppSettings
    {
        ThemePreference Theme = ThemePreference::System;
        bool MinimizeToTray = false;
    };

    // Lenient on purpose: these are preferences, not data, so anything
    // missing or unreadable falls back to its default instead of failing.
    [[nodiscard]] AppSettings ParseAppSettings(std::wstring_view json);
    [[nodiscard]] std::wstring SerializeAppSettings(AppSettings const& settings);

    // %LOCALAPPDATA%\ClaudeSwitcher\settings.json
    [[nodiscard]] std::filesystem::path AppSettingsFile();

    // Not thread-safe; the app uses it only from the UI thread.
    class AppSettingsStore
    {
    public:
        explicit AppSettingsStore(std::filesystem::path filePath);

        // Defaults when the file is missing, unreadable or malformed.
        [[nodiscard]] AppSettings Load() const noexcept;
        // Atomic replace. Throws std::system_error.
        void Save(AppSettings const& settings) const;

    private:
        std::filesystem::path m_filePath;
    };
}
