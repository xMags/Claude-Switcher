#pragma once

#include "SettingsViewModel.g.h"

#include "Core/AppSettings.h"
#include "Core/StartupRegistration.h"

#include <winrt/Microsoft.UI.Xaml.Data.h>

#include <filesystem>

namespace winrt::ClaudeSwitcher::implementation
{
    // The settings page's state. Every change is saved straight away; the
    // window listens to PropertyChanged to apply theme and tray changes.
    // UI-thread only.
    struct SettingsViewModel : SettingsViewModelT<SettingsViewModel>
    {
        SettingsViewModel(
            ::ClaudeSwitcher::Core::AppSettingsStore store,
            ::ClaudeSwitcher::Core::StartupRegistration startup,
            std::filesystem::path executable);

        [[nodiscard]] std::int32_t ThemeIndex() const noexcept;
        void ThemeIndex(std::int32_t value);
        [[nodiscard]] bool MinimizeToTray() const noexcept;
        void MinimizeToTray(bool value);
        [[nodiscard]] bool StartWithWindows() const noexcept;
        void StartWithWindows(bool value);

        [[nodiscard]] bool IsErrorOpen() const noexcept;
        [[nodiscard]] winrt::hstring ErrorText() const;
        void DismissError();

        winrt::event_token PropertyChanged(Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler);
        void PropertyChanged(winrt::event_token const& token) noexcept;

        [[nodiscard]] ::ClaudeSwitcher::Core::AppSettings const& Current() const noexcept;

    private:
        void Persist();
        void ShowError(winrt::hstring const& text);
        void Raise(wchar_t const* propertyName);

        ::ClaudeSwitcher::Core::AppSettingsStore m_store;
        ::ClaudeSwitcher::Core::StartupRegistration m_startup;
        std::filesystem::path m_executable;
        ::ClaudeSwitcher::Core::AppSettings m_settings;
        bool m_startWithWindows = false;
        bool m_errorOpen = false;
        winrt::hstring m_errorText;
        winrt::event<Microsoft::UI::Xaml::Data::PropertyChangedEventHandler> m_propertyChanged;
    };
}
