#include "pch.h"
#include "Shell/MainWindow.xaml.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

#include "Shell/AppPaths.h"
#include "ViewModels/ProfilesViewModel.h"

#include <microsoft.ui.xaml.window.h>
#include <winrt/ClaudeSwitcher.h>
#include <winrt/Microsoft.UI.Windowing.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>

#include <filesystem>
#include <string>

namespace winrt::ClaudeSwitcher::implementation
{
    namespace
    {
        // In effective pixels; scaled by the monitor's DPI.
        constexpr double InitialWidth = 940;
        constexpr double InitialHeight = 660;
    }

    MainWindow::MainWindow(winrt::ClaudeSwitcher::ProfilesViewModel viewModel)
        : m_viewModel(std::move(viewModel))
    {
    }

    void MainWindow::InitializeComponent()
    {
        MainWindowT::InitializeComponent();

        SystemBackdrop(Microsoft::UI::Xaml::Media::MicaBackdrop{});
        ExtendsContentIntoTitleBar(true);
        SetTitleBar(AppTitleBar());

        PageHost().Child(winrt::ClaudeSwitcher::ProfilesPage{ m_viewModel });

        auto const window = WindowHandle();
        winrt::get_self<ProfilesViewModel>(m_viewModel)->AttachOwnerWindow(window);
        ApplyInitialSize(window);

        auto const icon = ::ClaudeSwitcher::Shell::BundledAppIcon();
        if (std::filesystem::exists(icon))
        {
            AppWindow().SetIcon(icon.wstring());
        }

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
                    self->m_viewModel.RefreshRunningState();
                }
            });

        m_appWindowChangedToken = AppWindow().Changed(
            [weak = get_weak()]([[maybe_unused]] auto const& sender, [[maybe_unused]] auto const& args)
            {
                if (auto const self = weak.get())
                {
                    self->UpdatePollingForWindowState();
                }
            });

        m_closedToken = Closed(
            [weak = get_weak()]([[maybe_unused]] auto const& sender, [[maybe_unused]] auto const& args)
            {
                if (auto const self = weak.get())
                {
                    self->m_viewModel.SetStatusUpdatesPaused(true);
                    self->AppWindow().Changed(self->m_appWindowChangedToken);
                }
            });

        m_viewModel.SetStatusUpdatesPaused(false);
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

    void MainWindow::UpdatePollingForWindowState()
    {
        // Nobody can see the cards while the window is minimized.
        auto const presenter = AppWindow().Presenter().try_as<Microsoft::UI::Windowing::OverlappedPresenter>();
        auto const minimized = presenter && presenter.State() == Microsoft::UI::Windowing::OverlappedPresenterState::Minimized;
        m_viewModel.SetStatusUpdatesPaused(minimized);
    }
}
