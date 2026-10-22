#include "pch.h"
#include "Views/ProfileEditDialog.xaml.h"
#if __has_include("ProfileEditDialog.g.cpp")
#include "ProfileEditDialog.g.cpp"
#endif

#include "Core/OrdinalText.h"
#include "Core/ProfileValidation.h"

#include <winrt/Microsoft.UI.Xaml.Automation.h>

#include <array>
#include <string_view>

namespace winrt::ClaudeSwitcher::implementation
{
    namespace
    {
        struct Swatch
        {
            wchar_t const* Hex;
            wchar_t const* Name;
        };

        // Mid-tone colours that read on both the light and the dark theme.
        constexpr std::array<Swatch, 8> Palette{ {
            { L"#D97757", L"Clay" },
            { L"#3B82F6", L"Blue" },
            { L"#10B981", L"Green" },
            { L"#8B5CF6", L"Violet" },
            { L"#EC4899", L"Pink" },
            { L"#F59E0B", L"Amber" },
            { L"#14B8A6", L"Teal" },
            { L"#64748B", L"Slate" },
        } };

        // The name goes on the list item itself, which is what keyboard,
        // screen readers and UI Automation select.
        Microsoft::UI::Xaml::Controls::GridViewItem MakeSwatch(winrt::hstring const& hex, winrt::hstring const& name)
        {
            auto const rgb = ::ClaudeSwitcher::Core::ParseAccentColor(hex).value_or(0xD97757);
            Microsoft::UI::Xaml::Controls::Border swatch;
            swatch.Width(28);
            swatch.Height(28);
            swatch.CornerRadius({ 14, 14, 14, 14 });
            swatch.Background(Microsoft::UI::Xaml::Media::SolidColorBrush{ winrt::Windows::UI::Color{
                0xFF,
                static_cast<std::uint8_t>((rgb >> 16) & 0xFF),
                static_cast<std::uint8_t>((rgb >> 8) & 0xFF),
                static_cast<std::uint8_t>(rgb & 0xFF),
            } });

            Microsoft::UI::Xaml::Controls::GridViewItem item;
            item.Content(swatch);
            Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(item, name);
            Microsoft::UI::Xaml::Controls::ToolTipService::SetToolTip(item, winrt::box_value(name));
            return item;
        }
    }

    ProfileEditDialog::ProfileEditDialog(winrt::hstring title, winrt::hstring name, winrt::hstring note, winrt::hstring accentColor)
        : m_title(std::move(title)),
          m_initialName(std::move(name)),
          m_initialNote(std::move(note)),
          m_initialColor(std::move(accentColor))
    {
    }

    void ProfileEditDialog::InitializeComponent()
    {
        ProfileEditDialogT::InitializeComponent();

        Title(winrt::box_value(m_title));
        NameBox().Text(m_initialName);
        NoteBox().Text(m_initialNote);

        auto selected = 0;
        for (auto const& swatch : Palette)
        {
            m_colors.emplace_back(swatch.Hex);
            ColorGrid().Items().Append(MakeSwatch(swatch.Hex, swatch.Name));
            if (::ClaudeSwitcher::Core::EqualsOrdinalIgnoreCase(swatch.Hex, m_initialColor))
            {
                selected = static_cast<int>(m_colors.size()) - 1;
            }
        }
        // A colour from outside the palette (the list file was edited by
        // hand) stays selectable rather than being silently replaced.
        if (::ClaudeSwitcher::Core::IsValidAccentColor(m_initialColor) && selected == 0 &&
            !::ClaudeSwitcher::Core::EqualsOrdinalIgnoreCase(Palette[0].Hex, m_initialColor))
        {
            m_colors.push_back(m_initialColor);
            ColorGrid().Items().Append(MakeSwatch(m_initialColor, L"Current colour"));
            selected = static_cast<int>(m_colors.size()) - 1;
        }
        ColorGrid().SelectedIndex(selected);

        UpdatePrimaryButton();
    }

    winrt::hstring ProfileEditDialog::ProfileName()
    {
        return NameBox().Text();
    }

    winrt::hstring ProfileEditDialog::ProfileNote()
    {
        return NoteBox().Text();
    }

    winrt::hstring ProfileEditDialog::AccentColor()
    {
        auto const index = ColorGrid().SelectedIndex();
        if (index < 0 || static_cast<std::size_t>(index) >= m_colors.size())
        {
            return m_colors.empty() ? winrt::hstring{} : m_colors.front();
        }
        return m_colors[static_cast<std::size_t>(index)];
    }

    void ProfileEditDialog::ShowError(winrt::hstring const& message)
    {
        ErrorText().Text(message);
        ErrorText().Visibility(Microsoft::UI::Xaml::Visibility::Visible);
    }

    void ProfileEditDialog::OnNameChanged(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender,
        [[maybe_unused]] Microsoft::UI::Xaml::Controls::TextChangedEventArgs const& args)
    {
        ErrorText().Visibility(Microsoft::UI::Xaml::Visibility::Collapsed);
        UpdatePrimaryButton();
    }

    void ProfileEditDialog::UpdatePrimaryButton()
    {
        IsPrimaryButtonEnabled(!::ClaudeSwitcher::Core::TrimWhitespace(NameBox().Text()).empty());
    }
}
