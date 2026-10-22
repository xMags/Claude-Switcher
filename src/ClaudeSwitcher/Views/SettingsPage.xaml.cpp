#include "pch.h"
#include "Views/SettingsPage.xaml.h"
#if __has_include("SettingsPage.g.cpp")
#include "SettingsPage.g.cpp"
#endif

namespace winrt::ClaudeSwitcher::implementation
{
    SettingsPage::SettingsPage(winrt::ClaudeSwitcher::SettingsViewModel viewModel)
        : m_viewModel(std::move(viewModel))
    {
    }

    winrt::ClaudeSwitcher::SettingsViewModel SettingsPage::ViewModel() const
    {
        return m_viewModel;
    }

    void SettingsPage::OnErrorClosed(
        [[maybe_unused]] Microsoft::UI::Xaml::Controls::InfoBar const& sender,
        [[maybe_unused]] Microsoft::UI::Xaml::Controls::InfoBarClosedEventArgs const& args)
    {
        m_viewModel.DismissError();
    }
}
