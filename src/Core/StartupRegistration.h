#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace ClaudeSwitcher::Core
{
    // The switch the app is started with at sign-in.
    inline constexpr std::wstring_view StartupSwitch = L"--startup";

    // "<quoted exe> --startup"
    [[nodiscard]] std::wstring BuildStartupCommand(std::filesystem::path const& executable);

    // The per-user "run at sign-in" entry in HKEY_CURRENT_USER. The registry
    // is the single source of truth, so the toggle always matches what
    // Windows will do, including when the user switches the app off in
    // Settings > Apps > Startup (recorded under StartupApproved).
    class StartupRegistration
    {
    public:
        // Both key paths are relative to HKEY_CURRENT_USER.
        StartupRegistration(std::wstring runKey, std::wstring approvedKey, std::wstring valueName);

        // HKCU\Software\Microsoft\Windows\CurrentVersion\Run, value "ClaudeSwitcher".
        [[nodiscard]] static StartupRegistration ForCurrentUser();

        // Registered and not switched off in Windows' Startup apps page.
        [[nodiscard]] bool IsEnabled() const;
        [[nodiscard]] std::optional<std::wstring> RegisteredCommand() const;

        // Writes the entry and clears a "switched off" mark Windows may hold
        // from an earlier time. Throws std::system_error.
        void Enable(std::wstring const& command) const;
        // Removes the entry. Throws std::system_error.
        void Disable() const;

    private:
        std::wstring m_runKey;
        std::wstring m_approvedKey;
        std::wstring m_valueName;
    };
}
