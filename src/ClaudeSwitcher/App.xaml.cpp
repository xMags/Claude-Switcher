#include "pch.h"
#include "App.xaml.h"

#include "Core/AppSettings.h"
#include "Core/CommandLineParsing.h"
#include "Core/OrdinalText.h"
#include "Core/ProfileCatalog.h"
#include "Core/ProfileFolders.h"
#include "Core/ProfileStore.h"
#include "Core/StartupRegistration.h"
#include "Shell/AppPaths.h"
#include "Shell/MainWindow.xaml.h"
#include "ViewModels/ProfilesViewModel.h"
#include "ViewModels/SettingsViewModel.h"

#include <shobjidl_core.h>

#include <memory>

namespace winrt::ClaudeSwitcher::implementation
{
    App::App()
    {
        // A fixed identity, so the taskbar groups the window with a pinned
        // shortcut instead of deriving one from wherever the exe lives.
        static_cast<void>(SetCurrentProcessExplicitAppUserModelID(L"ClaudeSwitcher.App"));

#if defined _DEBUG && !defined DISABLE_XAML_GENERATED_BREAK_ON_UNHANDLED_EXCEPTION
        UnhandledException(
            [](winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::UnhandledExceptionEventArgs const& e)
            {
                if (IsDebuggerPresent())
                {
                    auto errorMessage = e.Message();
                    __debugbreak();
                }
            });
#endif
    }

    void App::OnLaunched([[maybe_unused]] Microsoft::UI::Xaml::LaunchActivatedEventArgs const& args)
    {
        if (!m_singleInstance.TryAcquire())
        {
            ::ClaudeSwitcher::Shell::ActivateExistingInstance();
            Exit();
            return;
        }

        namespace core = ::ClaudeSwitcher::Core;
        auto catalog = std::make_shared<core::ProfileCatalog>(
            core::ProfileStore{ core::SwitcherSettingsFile() },
            core::DefaultProfilesRoot(),
            &core::CheckProfilesRoot);
        auto profiles = winrt::make<ProfilesViewModel>(
            std::move(catalog),
            Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread());
        auto settings = winrt::make<SettingsViewModel>(
            core::AppSettingsStore{ core::AppSettingsFile() },
            core::StartupRegistration::ForCurrentUser(),
            ::ClaudeSwitcher::Shell::ExecutablePath());

        auto const window = winrt::make_self<MainWindow>(profiles, settings);
        m_window = *window;
        window->Start(LaunchedAtSignIn());
    }

    bool App::LaunchedAtSignIn()
    {
        // The startup entry adds this switch; nothing else does.
        auto const arguments = ::ClaudeSwitcher::Core::SplitCommandLine(GetCommandLineW());
        for (std::size_t i = 1; i < arguments.size(); ++i)
        {
            if (::ClaudeSwitcher::Core::EqualsOrdinalIgnoreCase(arguments[i], ::ClaudeSwitcher::Core::StartupSwitch))
            {
                return true;
            }
        }
        return false;
    }
}
