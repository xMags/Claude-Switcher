#include "pch.h"
#include "Views/ProfilesPage.xaml.h"
#if __has_include("ProfilesPage.g.cpp")
#include "ProfilesPage.g.cpp"
#endif

#include <winrt/ClaudeSwitcher.h>

#include <wil/resource.h>

namespace winrt::ClaudeSwitcher::implementation
{
    namespace
    {
        using Microsoft::UI::Xaml::Controls::ContentDialog;
        using Microsoft::UI::Xaml::Controls::ContentDialogButton;
        using Microsoft::UI::Xaml::Controls::ContentDialogButtonClickEventArgs;
        using Microsoft::UI::Xaml::Controls::ContentDialogResult;

        // Card buttons and menu items carry their card's id in Tag.
        winrt::hstring IdFromSender(winrt::Windows::Foundation::IInspectable const& sender)
        {
            auto const element = sender.try_as<Microsoft::UI::Xaml::FrameworkElement>();
            return element ? winrt::unbox_value_or<winrt::hstring>(element.Tag(), winrt::hstring{}) : winrt::hstring{};
        }
    }

    ProfilesPage::ProfilesPage(winrt::ClaudeSwitcher::ProfilesViewModel viewModel)
        : m_viewModel(std::move(viewModel))
    {
    }

    winrt::ClaudeSwitcher::ProfilesViewModel ProfilesPage::ViewModel() const
    {
        return m_viewModel;
    }

    void ProfilesPage::OnPrimaryClick(winrt::Windows::Foundation::IInspectable const& sender, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        if (auto const id = IdFromSender(sender); !id.empty())
        {
            m_viewModel.Launch(id);
        }
    }

    void ProfilesPage::OnAddClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        ShowEditDialog({});
    }

    void ProfilesPage::OnEditClick(winrt::Windows::Foundation::IInspectable const& sender, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        if (auto const id = IdFromSender(sender); !id.empty())
        {
            ShowEditDialog(id);
        }
    }

    void ProfilesPage::OnShortcutClick(winrt::Windows::Foundation::IInspectable const& sender, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        if (auto const id = IdFromSender(sender); !id.empty())
        {
            m_viewModel.CreateDesktopShortcut(id);
        }
    }

    void ProfilesPage::OnOpenFolderClick(winrt::Windows::Foundation::IInspectable const& sender, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        if (auto const id = IdFromSender(sender); !id.empty())
        {
            m_viewModel.OpenDataFolder(id);
        }
    }

    void ProfilesPage::OnDeleteClick(winrt::Windows::Foundation::IInspectable const& sender, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        if (auto const id = IdFromSender(sender); !id.empty())
        {
            ConfirmDelete(id);
        }
    }

    void ProfilesPage::OnMessageClosed(
        [[maybe_unused]] Microsoft::UI::Xaml::Controls::InfoBar const& sender,
        [[maybe_unused]] Microsoft::UI::Xaml::Controls::InfoBarClosedEventArgs const& args)
    {
        m_viewModel.DismissMessage();
    }

    void ProfilesPage::OnSignInHintClosed(
        [[maybe_unused]] Microsoft::UI::Xaml::Controls::InfoBar const& sender,
        [[maybe_unused]] Microsoft::UI::Xaml::Controls::InfoBarClosedEventArgs const& args)
    {
        m_viewModel.DismissSignInHint();
    }

    winrt::fire_and_forget ProfilesPage::ShowEditDialog(winrt::hstring id)
    {
        if (m_dialogOpen)
        {
            co_return;
        }
        auto const lifetime = get_strong();

        winrt::hstring name;
        winrt::hstring note;
        winrt::hstring accentColor;
        if (!id.empty())
        {
            auto const card = FindCard(id);
            if (!card)
            {
                co_return;
            }
            name = card.Name();
            note = card.Note();
            accentColor = card.AccentColorHex();
        }

        m_dialogOpen = true;
        auto const reset = wil::scope_exit([this] { m_dialogOpen = false; });

        winrt::ClaudeSwitcher::ProfileEditDialog dialog{ id.empty() ? L"Add profile" : L"Edit profile", name, note, accentColor };
        dialog.XamlRoot(XamlRoot());
        // The dialog is reached through sender, not captured, so the handler
        // does not keep its own dialog alive.
        dialog.PrimaryButtonClick(
            [viewModel = m_viewModel, id](ContentDialog const& sender, ContentDialogButtonClickEventArgs const& args)
            {
                auto const editor = sender.as<winrt::ClaudeSwitcher::ProfileEditDialog>();
                auto const error = id.empty()
                                       ? viewModel.AddProfile(editor.ProfileName(), editor.ProfileNote(), editor.AccentColor())
                                       : viewModel.UpdateProfile(id, editor.ProfileName(), editor.ProfileNote(), editor.AccentColor());
                if (!error.empty())
                {
                    args.Cancel(true);
                    editor.ShowError(error);
                }
            });

        try
        {
            co_await dialog.ShowAsync();
        }
        catch (winrt::hresult_error const&)
        {
            // The window closed while the dialog was open.
        }
    }

    winrt::fire_and_forget ProfilesPage::ConfirmDelete(winrt::hstring id)
    {
        if (m_dialogOpen)
        {
            co_return;
        }
        auto const lifetime = get_strong();

        auto const card = FindCard(id);
        if (!card || !m_viewModel.CanDeleteProfile(id))
        {
            co_return;
        }

        m_dialogOpen = true;
        auto const reset = wil::scope_exit([this] { m_dialogOpen = false; });

        Microsoft::UI::Xaml::Controls::TextBlock body;
        body.TextWrapping(Microsoft::UI::Xaml::TextWrapping::Wrap);
        body.Text(
            L"Claude Switcher will forget this profile and move its Claude data folder (sign-in, settings and cached files) "
            L"to the Recycle Bin. Your Claude account and your chats are not affected.");

        ContentDialog confirm;
        confirm.XamlRoot(XamlRoot());
        confirm.Style(Microsoft::UI::Xaml::Application::Current().Resources().Lookup(winrt::box_value(L"DefaultContentDialogStyle"))
                          .as<Microsoft::UI::Xaml::Style>());
        confirm.Title(winrt::box_value(L"Delete " + card.Name() + L"?"));
        confirm.Content(body);
        confirm.PrimaryButtonText(L"Delete");
        confirm.CloseButtonText(L"Cancel");
        confirm.DefaultButton(ContentDialogButton::Close);

        auto result = ContentDialogResult::None;
        try
        {
            result = co_await confirm.ShowAsync();
        }
        catch (winrt::hresult_error const&)
        {
            co_return;
        }
        if (result == ContentDialogResult::Primary)
        {
            m_viewModel.DeleteProfile(id);
        }
    }

    winrt::ClaudeSwitcher::ProfileCardViewModel ProfilesPage::FindCard(winrt::hstring const& id) const
    {
        for (auto const& card : m_viewModel.Cards())
        {
            if (card.Id() == id)
            {
                return card;
            }
        }
        return nullptr;
    }
}
