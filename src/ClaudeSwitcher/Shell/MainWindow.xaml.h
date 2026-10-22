#pragma once

#include "MainWindow.g.h"

#include "Shell/TrayIcon.h"

#include <winrt/Microsoft.UI.Windowing.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>

#include <memory>

namespace winrt::ClaudeSwitcher::implementation
{
    // Window lifecycle and shell layout: the side panel, theme, tray and
    // minimized state. Page content lives in the pages and their view models.
    struct MainWindow : MainWindowT<MainWindow>
    {
        MainWindow(winrt::ClaudeSwitcher::ProfilesViewModel profiles, winrt::ClaudeSwitcher::SettingsViewModel settings);

        void InitializeComponent();

        // Shows the window. A start at Windows sign-in stays out of the way
        // instead: in the tray when that is on, otherwise minimized.
        void Start(bool launchedAtSignIn);

        void OnNavigationSelectionChanged(
            Microsoft::UI::Xaml::Controls::NavigationView const& sender,
            Microsoft::UI::Xaml::Controls::NavigationViewSelectionChangedEventArgs const& args);

    private:
        [[nodiscard]] HWND WindowHandle() const;
        void ApplyInitialSize(HWND window);
        void ApplyTheme();
        void ApplyTrayPreference();
        void OnWindowStateChanged();
        void OnClosing(Microsoft::UI::Windowing::AppWindowClosingEventArgs const& args);
        void UpdatePolling();
        void RestoreWindow();
        void Quit();

        winrt::ClaudeSwitcher::ProfilesViewModel m_profiles;
        winrt::ClaudeSwitcher::SettingsViewModel m_settings;
        winrt::ClaudeSwitcher::ProfilesPage m_homePage{ nullptr };
        winrt::ClaudeSwitcher::SettingsPage m_settingsPage{ nullptr };
        std::unique_ptr<::ClaudeSwitcher::Shell::TrayIcon> m_tray;
        winrt::event_token m_activatedToken{};
        winrt::event_token m_closedToken{};
        winrt::event_token m_appWindowChangedToken{};
        winrt::event_token m_settingsChangedToken{};
        winrt::event_token m_closingToken{};
        // Set by Quit, so the close that follows is not turned into a hide.
        bool m_quitting = false;
    };
}

namespace winrt::ClaudeSwitcher::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow>
    {
    };
}
