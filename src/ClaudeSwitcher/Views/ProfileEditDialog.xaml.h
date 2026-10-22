#pragma once

#include "ProfileEditDialog.g.h"

#include <winrt/Microsoft.UI.Xaml.Controls.h>

#include <vector>

namespace winrt::ClaudeSwitcher::implementation
{
    // Collects a profile's name, note and colour. Validation and saving stay
    // with the caller, which reports problems back through ShowError.
    struct ProfileEditDialog : ProfileEditDialogT<ProfileEditDialog>
    {
        ProfileEditDialog(winrt::hstring title, winrt::hstring name, winrt::hstring note, winrt::hstring accentColor);

        void InitializeComponent();

        [[nodiscard]] winrt::hstring ProfileName();
        [[nodiscard]] winrt::hstring ProfileNote();
        [[nodiscard]] winrt::hstring AccentColor();
        void ShowError(winrt::hstring const& message);

        void OnNameChanged(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::Controls::TextChangedEventArgs const& args);

    private:
        void UpdatePrimaryButton();

        winrt::hstring m_title;
        winrt::hstring m_initialName;
        winrt::hstring m_initialNote;
        winrt::hstring m_initialColor;
        // Hex values in the same order as the swatches in ColorGrid.
        std::vector<winrt::hstring> m_colors;
    };
}

namespace winrt::ClaudeSwitcher::factory_implementation
{
    struct ProfileEditDialog : ProfileEditDialogT<ProfileEditDialog, implementation::ProfileEditDialog>
    {
    };
}
