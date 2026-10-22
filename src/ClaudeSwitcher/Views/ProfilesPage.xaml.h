#pragma once

#include "ProfilesPage.g.h"

#include <winrt/Microsoft.UI.Xaml.Controls.h>

namespace winrt::ClaudeSwitcher::implementation
{
    // View-only behaviour: dialogs and forwarding clicks to the view model.
    struct ProfilesPage : ProfilesPageT<ProfilesPage>
    {
        explicit ProfilesPage(winrt::ClaudeSwitcher::ProfilesViewModel viewModel);

        [[nodiscard]] winrt::ClaudeSwitcher::ProfilesViewModel ViewModel() const;

        void OnPrimaryClick(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnAddClick(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnEditClick(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnShortcutClick(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnOpenFolderClick(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnDeleteClick(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnMessageClosed(
            Microsoft::UI::Xaml::Controls::InfoBar const& sender,
            Microsoft::UI::Xaml::Controls::InfoBarClosedEventArgs const& args);
        void OnSignInHintClosed(
            Microsoft::UI::Xaml::Controls::InfoBar const& sender,
            Microsoft::UI::Xaml::Controls::InfoBarClosedEventArgs const& args);

    private:
        winrt::fire_and_forget ShowEditDialog(winrt::hstring id);
        winrt::fire_and_forget ConfirmDelete(winrt::hstring id);
        [[nodiscard]] winrt::ClaudeSwitcher::ProfileCardViewModel FindCard(winrt::hstring const& id) const;

        winrt::ClaudeSwitcher::ProfilesViewModel m_viewModel;
        // WinUI allows one ContentDialog at a time per XamlRoot.
        bool m_dialogOpen = false;
    };
}

namespace winrt::ClaudeSwitcher::factory_implementation
{
    struct ProfilesPage : ProfilesPageT<ProfilesPage, implementation::ProfilesPage>
    {
    };
}
