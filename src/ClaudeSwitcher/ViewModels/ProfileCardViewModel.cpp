#include "pch.h"
#include "ViewModels/ProfileCardViewModel.h"
#if __has_include("ProfileCardViewModel.g.cpp")
#include "ProfileCardViewModel.g.cpp"
#endif

#include "Core/ProfileValidation.h"

namespace winrt::ClaudeSwitcher::implementation
{
    namespace
    {
        // Claude's own orange, used when a stored colour cannot be read.
        constexpr std::uint32_t FallbackAccent = 0xD97757;

        winrt::Windows::UI::Color ToColor(winrt::hstring const& hex) noexcept
        {
            auto const rgb = ::ClaudeSwitcher::Core::ParseAccentColor(hex).value_or(FallbackAccent);
            return winrt::Windows::UI::Color{
                0xFF,
                static_cast<std::uint8_t>((rgb >> 16) & 0xFF),
                static_cast<std::uint8_t>((rgb >> 8) & 0xFF),
                static_cast<std::uint8_t>(rgb & 0xFF),
            };
        }
    }

    ProfileCardViewModel::ProfileCardViewModel(
        winrt::hstring id,
        winrt::hstring name,
        winrt::hstring note,
        winrt::hstring accentColorHex,
        bool isDefault)
        : m_id(std::move(id)),
          m_name(std::move(name)),
          m_note(std::move(note)),
          m_accentColorHex(std::move(accentColorHex)),
          m_accentColor(ToColor(m_accentColorHex)),
          m_isDefault(isDefault)
    {
    }

    winrt::hstring ProfileCardViewModel::Id() const { return m_id; }
    winrt::hstring ProfileCardViewModel::Name() const { return m_name; }
    winrt::hstring ProfileCardViewModel::Note() const { return m_note; }
    winrt::hstring ProfileCardViewModel::AccentColorHex() const { return m_accentColorHex; }
    winrt::Windows::UI::Color ProfileCardViewModel::AccentColor() const noexcept { return m_accentColor; }
    bool ProfileCardViewModel::IsDefault() const noexcept { return m_isDefault; }
    bool ProfileCardViewModel::IsRunning() const noexcept { return m_isRunning; }
    bool ProfileCardViewModel::CanLaunch() const noexcept { return m_canLaunch; }

    winrt::hstring ProfileCardViewModel::PrimaryActionText() const
    {
        return m_isRunning ? L"Focus" : L"Open";
    }

    Microsoft::UI::Xaml::Visibility ProfileCardViewModel::RunningVisibility() const noexcept
    {
        return m_isRunning ? Microsoft::UI::Xaml::Visibility::Visible : Microsoft::UI::Xaml::Visibility::Collapsed;
    }

    Microsoft::UI::Xaml::Visibility ProfileCardViewModel::ManageVisibility() const noexcept
    {
        return m_isDefault ? Microsoft::UI::Xaml::Visibility::Collapsed : Microsoft::UI::Xaml::Visibility::Visible;
    }

    winrt::event_token ProfileCardViewModel::PropertyChanged(Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler)
    {
        return m_propertyChanged.add(handler);
    }

    void ProfileCardViewModel::PropertyChanged(winrt::event_token const& token) noexcept
    {
        m_propertyChanged.remove(token);
    }

    void ProfileCardViewModel::SetRunning(bool running)
    {
        if (m_isRunning == running)
        {
            return;
        }
        m_isRunning = running;
        Raise(L"IsRunning");
        Raise(L"PrimaryActionText");
        Raise(L"RunningVisibility");
    }

    void ProfileCardViewModel::SetCanLaunch(bool canLaunch)
    {
        if (m_canLaunch == canLaunch)
        {
            return;
        }
        m_canLaunch = canLaunch;
        Raise(L"CanLaunch");
    }

    void ProfileCardViewModel::Raise(wchar_t const* propertyName)
    {
        m_propertyChanged(*this, Microsoft::UI::Xaml::Data::PropertyChangedEventArgs{ propertyName });
    }
}
