#pragma once

#include "App.xaml.g.h"

#include "Shell/SingleInstanceGuard.h"

namespace winrt::ClaudeSwitcher::implementation
{
    // Composition root: builds the services and the window, nothing else.
    struct App : AppT<App>
    {
        App();

        void OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const& args);

    private:
        [[nodiscard]] static bool LaunchedAtSignIn();

        ::ClaudeSwitcher::Shell::SingleInstanceGuard m_singleInstance;
        winrt::Microsoft::UI::Xaml::Window m_window{ nullptr };
    };
}
