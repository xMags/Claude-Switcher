#include "pch.h"
#include "Shell/MainWindow.xaml.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

#include "Core/AppSettings.h"
#include "Shell/AppPaths.h"
#include "ViewModels/ProfilesViewModel.h"
#include "ViewModels/SettingsViewModel.h"

#include <microsoft.ui.xaml.window.h>
#include <winrt/ClaudeSwitcher.h>
#include <winrt/Microsoft.UI.Windowing.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>

#include <filesystem>
#include <string_view>

namespace winrt::ClaudeSwitcher::implementation
{
    namespace
    {
        using Microsoft::UI::Windowing::OverlappedPresenter;
        using Microsoft::UI::Windowing::OverlappedPresenterState;

        // In effective pixels; scaled by the monitor's DPI.
        constexpr double InitialWidth = 960;
        constexpr double InitialHeight = 680;
    }

    MainWindow::MainWindow(winrt::ClaudeSwitcher::ProfilesViewModel profiles, winrt::ClaudeSwitcher::SettingsViewModel settings)
        : m_profiles(std::move(profiles)),
          m_settings(std::move(settings))
    {
    }

    void MainWindow::InitializeComponent()
    {
        MainWindowT::InitializeComponent();

        SystemBackdrop(Microsoft::UI::Xaml::Media::MicaBackdrop{});
        ExtendsContentIntoTitleBar(true);
        SetTitleBar(AppTitleBar());

        // Both pages are kept for the window's lifetime, so switching keeps
        // their state (scroll position, open messages).
        m_homePage = winrt::ClaudeSwitcher::ProfilesPage{ m_profiles };
        m_settingsPage = winrt::ClaudeSwitcher::SettingsPage{ m_settings };
        Navigation().SelectedItem(HomeItem());
        PageHost().Child(m_homePage);

        auto const window = WindowHandle();
        winrt::get_self<ProfilesViewModel>(m_profiles)->AttachOwnerWindow(window);
        ApplyInitialSize(window);
        // Before the first paint, so a dark preference never flashes light.
        ApplyTheme();

        auto const icon = ::ClaudeSwitcher::Shell::BundledAppIcon();
        if (std::filesystem::exists(icon))
        {
            AppWindow().SetIcon(icon.wstring());
        }

        // Tray callbacks run inside the tray window's message handler, so
        // anything that may tear the window down is posted, not run inline.
        auto const queue = DispatcherQueue();
        m_tray = std::make_unique<::ClaudeSwitcher::Shell::TrayIcon>(::ClaudeSwitcher::Shell::TrayIcon::Callbacks{
            [weak = get_weak(), queue]
            {
                queue.TryEnqueue([weak] {
                    if (auto const self = weak.get())
                    {
                        self->RestoreWindow();
                    }
                });
            },
            [weak = get_weak(), queue]
            {
                queue.TryEnqueue([weak] {
                    if (auto const self = weak.get())
                    {
                        self->Quit();
                    }
                });
            },
        });
        m_tray->SetVisible(winrt::get_self<SettingsViewModel>(m_settings)->Current().CloseToTray);

        m_settingsChangedToken = m_settings.PropertyChanged(
            [weak = get_weak()]([[maybe_unused]] auto const& sender, Microsoft::UI::Xaml::Data::PropertyChangedEventArgs const& args)
            {
                auto const self = weak.get();
                if (!self)
                {
                    return;
                }
                std::wstring_view const name = args.PropertyName();
                if (name == L"ThemeIndex")
                {
                    self->ApplyTheme();
                }
                else if (name == L"CloseToTray")
                {
                    self->ApplyTrayPreference();
                }
            });

        m_activatedToken = Activated(
            [weak = get_weak()]([[maybe_unused]] auto const& sender, Microsoft::UI::Xaml::WindowActivatedEventArgs const& args)
            {
                if (args.WindowActivationState() == Microsoft::UI::Xaml::WindowActivationState::Deactivated)
                {
                    return;
                }
                // Coming back to the window is when a stale dot would be
                // noticed, for example right after quitting Claude.
                if (auto const self = weak.get())
                {
                    self->m_profiles.RefreshRunningState();
                }
            });

        m_appWindowChangedToken = AppWindow().Changed(
            [weak = get_weak()]([[maybe_unused]] auto const& sender, [[maybe_unused]] auto const& args)
            {
                if (auto const self = weak.get())
                {
                    self->OnWindowStateChanged();
                }
            });

        m_closingToken = AppWindow().Closing(
            [weak = get_weak()]([[maybe_unused]] auto const& sender, Microsoft::UI::Windowing::AppWindowClosingEventArgs const& args)
            {
                if (auto const self = weak.get())
                {
                    self->OnClosing(args);
                }
            });

        m_closedToken = Closed(
            [weak = get_weak()]([[maybe_unused]] auto const& sender, [[maybe_unused]] auto const& args)
            {
                if (auto const self = weak.get())
                {
                    self->m_profiles.SetStatusUpdatesPaused(true);
                    self->AppWindow().Changed(self->m_appWindowChangedToken);
                    self->AppWindow().Closing(self->m_closingToken);
                    self->m_settings.PropertyChanged(self->m_settingsChangedToken);
                    self->m_tray.reset();
                    // The window object outlives the XAML framework (the app
                    // keeps it until exit), so it must not keep pages alive:
                    // releasing XAML elements after shutdown crashes.
                    self->PageHost().Child(nullptr);
                    self->m_homePage = nullptr;
                    self->m_settingsPage = nullptr;
                }
            });
    }

    void MainWindow::Start(bool launchedAtSignIn)
    {
        if (!launchedAtSignIn)
        {
            Activate();
            UpdatePolling();
            return;
        }

        auto const& settings = winrt::get_self<SettingsViewModel>(m_settings)->Current();
        if (!settings.CloseToTray)
        {
            // Appears on the taskbar without taking focus from whatever the
            // user opened first after signing in.
            if (auto const presenter = AppWindow().Presenter().try_as<OverlappedPresenter>())
            {
                presenter.Minimize();
            }
        }
        // With the tray on, the window is simply never shown until asked for.
        UpdatePolling();
    }

    void MainWindow::OnNavigationSelectionChanged(
        [[maybe_unused]] Microsoft::UI::Xaml::Controls::NavigationView const& sender,
        Microsoft::UI::Xaml::Controls::NavigationViewSelectionChangedEventArgs const& args)
    {
        if (args.IsSettingsSelected())
        {
            PageHost().Child(m_settingsPage);
        }
        else
        {
            PageHost().Child(m_homePage);
        }
    }

    HWND MainWindow::WindowHandle() const
    {
        auto const native = this->try_as<::IWindowNative>();
        HWND window = nullptr;
        if (native)
        {
            winrt::check_hresult(native->get_WindowHandle(&window));
        }
        return window;
    }

    void MainWindow::ApplyInitialSize(HWND window)
    {
        auto const dpi = window != nullptr ? GetDpiForWindow(window) : USER_DEFAULT_SCREEN_DPI;
        auto const scale = static_cast<double>(dpi) / USER_DEFAULT_SCREEN_DPI;
        AppWindow().Resize({ static_cast<std::int32_t>(InitialWidth * scale), static_cast<std::int32_t>(InitialHeight * scale) });
    }

    void MainWindow::ApplyTheme()
    {
        using Microsoft::UI::Windowing::TitleBarTheme;
        using Microsoft::UI::Xaml::ElementTheme;

        auto elementTheme = ElementTheme::Default;
        auto titleBarTheme = TitleBarTheme::UseDefaultAppMode;
        switch (winrt::get_self<SettingsViewModel>(m_settings)->Current().Theme)
        {
        case ::ClaudeSwitcher::Core::ThemePreference::Light:
            elementTheme = ElementTheme::Light;
            titleBarTheme = TitleBarTheme::Light;
            break;
        case ::ClaudeSwitcher::Core::ThemePreference::Dark:
            elementTheme = ElementTheme::Dark;
            titleBarTheme = TitleBarTheme::Dark;
            break;
        case ::ClaudeSwitcher::Core::ThemePreference::System:
            break;
        }
        RootGrid().RequestedTheme(elementTheme);
        // The minimize, maximize and close buttons are drawn by Windows, not XAML.
        AppWindow().TitleBar().PreferredTheme(titleBarTheme);
    }

    void MainWindow::ApplyTrayPreference()
    {
        auto const enabled = winrt::get_self<SettingsViewModel>(m_settings)->Current().CloseToTray;
        if (m_tray)
        {
            m_tray->SetVisible(enabled);
        }
        // Only reached from a settings change. Turning the tray off while
        // hidden would leave no way back, so the window comes back first.
        if (!enabled && !AppWindow().IsVisible())
        {
            RestoreWindow();
        }
    }

    void MainWindow::OnWindowStateChanged()
    {
        UpdatePolling();
    }

    void MainWindow::OnClosing(Microsoft::UI::Windowing::AppWindowClosingEventArgs const& args)
    {
        if (m_quitting || !winrt::get_self<SettingsViewModel>(m_settings)->Current().CloseToTray)
        {
            return;
        }
        // The close button only hides the window; Quit in the tray menu exits.
        // Hiding also removes the taskbar button.
        args.Cancel(true);
        AppWindow().Hide();
        UpdatePolling();
    }

    void MainWindow::UpdatePolling()
    {
        // Nobody can see the cards while the window is minimized or hidden.
        auto const presenter = AppWindow().Presenter().try_as<OverlappedPresenter>();
        auto const minimized = presenter && presenter.State() == OverlappedPresenterState::Minimized;
        m_profiles.SetStatusUpdatesPaused(minimized || !AppWindow().IsVisible());
    }

    void MainWindow::RestoreWindow()
    {
        AppWindow().Show();
        if (auto const presenter = AppWindow().Presenter().try_as<OverlappedPresenter>();
            presenter && presenter.State() == OverlappedPresenterState::Minimized)
        {
            presenter.Restore();
        }
        Activate();
        if (auto const window = WindowHandle())
        {
            SetForegroundWindow(window);
        }
        UpdatePolling();
    }

    void MainWindow::Quit()
    {
        m_quitting = true;
        Close();
    }
}
