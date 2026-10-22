#pragma once

#include "SettingsPage.g.h"

#include <winrt/Microsoft.UI.Xaml.Controls.h>

namespace winrt::ClaudeSwitcher::implementation
{
    // View only; all state lives in SettingsViewModel.
    struct SettingsPage : SettingsPageT<SettingsPage>
    {
        explicit SettingsPage(winrt::ClaudeSwitcher::SettingsViewModel viewModel);

        [[nodiscard]] winrt::ClaudeSwitcher::SettingsViewModel ViewModel() const;

        void OnErrorClosed(
            Microsoft::UI::Xaml::Controls::InfoBar const& sender,
            Microsoft::UI::Xaml::Controls::InfoBarClosedEventArgs const& args);

    private:
        winrt::ClaudeSwitcher::SettingsViewModel m_viewModel;
    };
}

namespace winrt::ClaudeSwitcher::factory_implementation
{
    struct SettingsPage : SettingsPageT<SettingsPage, implementation::SettingsPage>
    {
    };
}
