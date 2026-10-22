#pragma once

#include "ProfileCardViewModel.g.h"

#include <winrt/Microsoft.UI.Xaml.Data.h>
#include <winrt/Windows.UI.h>

#include <string>

namespace winrt::ClaudeSwitcher::implementation
{
    // Display state of one card. UI-thread only; ProfilesViewModel owns the
    // instances and pushes changes through the setters below.
    struct ProfileCardViewModel : ProfileCardViewModelT<ProfileCardViewModel>
    {
        ProfileCardViewModel(
            winrt::hstring id,
            winrt::hstring name,
            winrt::hstring note,
            winrt::hstring accentColorHex,
            bool isDefault);

        [[nodiscard]] winrt::hstring Id() const;
        [[nodiscard]] winrt::hstring Name() const;
        [[nodiscard]] winrt::hstring Note() const;
        [[nodiscard]] winrt::hstring AccentColorHex() const;
        [[nodiscard]] winrt::Windows::UI::Color AccentColor() const noexcept;
        [[nodiscard]] bool IsDefault() const noexcept;
        [[nodiscard]] bool IsRunning() const noexcept;
        [[nodiscard]] bool CanLaunch() const noexcept;
        [[nodiscard]] winrt::hstring PrimaryActionText() const;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility RunningVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility ManageVisibility() const noexcept;

        winrt::event_token PropertyChanged(Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler);
        void PropertyChanged(winrt::event_token const& token) noexcept;

        void SetRunning(bool running);
        void SetCanLaunch(bool canLaunch);

    private:
        void Raise(wchar_t const* propertyName);

        winrt::hstring m_id;
        winrt::hstring m_name;
        winrt::hstring m_note;
        winrt::hstring m_accentColorHex;
        winrt::Windows::UI::Color m_accentColor{};
        bool m_isDefault = false;
        bool m_isRunning = false;
        bool m_canLaunch = true;
        winrt::event<Microsoft::UI::Xaml::Data::PropertyChangedEventHandler> m_propertyChanged;
    };
}
