#include "pch.h"
#include "App.xaml.h"

#include "Core/ProfileCatalog.h"
#include "Core/ProfileFolders.h"
#include "Core/ProfileStore.h"
#include "Shell/MainWindow.xaml.h"
#include "ViewModels/ProfilesViewModel.h"

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
        auto viewModel = winrt::make<ProfilesViewModel>(
            std::move(catalog),
            Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread());

        m_window = winrt::make<MainWindow>(viewModel);
        m_window.Activate();
    }
}
