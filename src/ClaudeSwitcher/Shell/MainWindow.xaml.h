#pragma once

#include "MainWindow.g.h"

namespace winrt::ClaudeSwitcher::implementation
{
    // Window lifecycle and shell layout only: hosts the profiles page and
    // tells the view model when status polling is worth doing.
    struct MainWindow : MainWindowT<MainWindow>
    {
        explicit MainWindow(winrt::ClaudeSwitcher::ProfilesViewModel viewModel);

        void InitializeComponent();

    private:
        [[nodiscard]] HWND WindowHandle() const;
        void ApplyInitialSize(HWND window);
        void UpdatePollingForWindowState();

        winrt::ClaudeSwitcher::ProfilesViewModel m_viewModel;
        winrt::event_token m_activatedToken{};
        winrt::event_token m_closedToken{};
        winrt::event_token m_appWindowChangedToken{};
    };
}

namespace winrt::ClaudeSwitcher::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow>
    {
    };
}
