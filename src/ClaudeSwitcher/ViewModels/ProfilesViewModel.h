#pragma once

#include "ProfilesViewModel.g.h"

#include "Core/ClaudeInstallLocator.h"
#include "Core/ProcessScanner.h"
#include "Core/ProfileCatalog.h"
#include "Core/ShortcutWriter.h"
#include "ViewModels/StatusPoller.h"

#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Data.h>
#include <winrt/Windows.Foundation.Collections.h>

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace winrt::ClaudeSwitcher::implementation
{
    // State and actions of the profiles page. UI-thread only: every member,
    // including the status poller's result handler, runs on the thread that
    // owns the dispatcher queue passed in.
    struct ProfilesViewModel : ProfilesViewModelT<ProfilesViewModel>
    {
        ProfilesViewModel(
            std::shared_ptr<::ClaudeSwitcher::Core::ProfileCatalog> catalog,
            Microsoft::UI::Dispatching::DispatcherQueue queue);
        ~ProfilesViewModel();

        ProfilesViewModel(ProfilesViewModel const&) = delete;
        ProfilesViewModel& operator=(ProfilesViewModel const&) = delete;

        [[nodiscard]] winrt::Windows::Foundation::Collections::IObservableVector<winrt::ClaudeSwitcher::ProfileCardViewModel> Cards() const;
        [[nodiscard]] winrt::hstring StatusText() const;
        [[nodiscard]] bool CanAddProfiles() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility EmptyHintVisibility() const noexcept;

        [[nodiscard]] bool IsMessageOpen() const noexcept;
        [[nodiscard]] winrt::hstring MessageTitle() const;
        [[nodiscard]] winrt::hstring MessageText() const;
        [[nodiscard]] Microsoft::UI::Xaml::Controls::InfoBarSeverity MessageSeverity() const noexcept;
        void DismissMessage();

        [[nodiscard]] bool IsSignInHintOpen() const noexcept;
        void DismissSignInHint();

        void Launch(winrt::hstring const& id);
        winrt::hstring AddProfile(winrt::hstring const& name, winrt::hstring const& note, winrt::hstring const& accentColor);
        winrt::hstring UpdateProfile(
            winrt::hstring const& id,
            winrt::hstring const& name,
            winrt::hstring const& note,
            winrt::hstring const& accentColor);
        bool CanDeleteProfile(winrt::hstring const& id);
        void DeleteProfile(winrt::hstring const& id);
        void CreateDesktopShortcut(winrt::hstring const& id);
        void CreateStartMenuShortcut(winrt::hstring const& id);
        void OpenDataFolder(winrt::hstring const& id);

        void RefreshRunningState();
        void SetStatusUpdatesPaused(bool paused);

        winrt::event_token PropertyChanged(Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler);
        void PropertyChanged(winrt::event_token const& token) noexcept;

        // Owner for shell dialogs (Recycle Bin progress and warnings).
        void AttachOwnerWindow(HWND owner) noexcept;

    private:
        void LoadCatalog();
        // Re-reads the installed Claude package; returns it when it can be started.
        std::optional<::ClaudeSwitcher::Core::ClaudeInstallation> RefreshInstallation(bool reportProblems);
        void RebuildCards();
        void ApplyInstances(std::vector<::ClaudeSwitcher::Core::ClaudeInstance> instances);
        void UpdateStatusText();
        void ShowMessage(
            Microsoft::UI::Xaml::Controls::InfoBarSeverity severity,
            winrt::hstring const& title,
            winrt::hstring const& text);
        void EnsurePoller();

        // Shortcuts. Only shortcuts whose arguments name exactly this
        // profile's folder are treated as this app's to move or remove.
        void CreateShortcut(winrt::hstring const& id, ::ClaudeSwitcher::Core::ShortcutLocation location);
        // No data directory means the Default session.
        [[nodiscard]] ::ClaudeSwitcher::Core::ShortcutSpec MakeShortcutSpec(
            std::wstring const& name,
            std::optional<std::filesystem::path> const& dataDirectory,
            ::ClaudeSwitcher::Core::ClaudeInstallation const& installation,
            ::ClaudeSwitcher::Core::ShortcutLocation location) const;
        [[nodiscard]] bool IsShortcutFor(std::filesystem::path const& shortcut, ::ClaudeSwitcher::Core::Profile const& profile) const;
        void RemoveShortcuts(::ClaudeSwitcher::Core::Profile const& profile);
        void RenameShortcuts(::ClaudeSwitcher::Core::Profile const& before, ::ClaudeSwitcher::Core::Profile const& after);

        [[nodiscard]] ::ClaudeSwitcher::Core::Profile const* FindProfile(winrt::hstring const& id) const;
        [[nodiscard]] std::optional<std::wstring> CardIdForSession(std::optional<std::wstring> const& userDataDir) const;
        [[nodiscard]] std::optional<std::uint32_t> RunningProcessFor(winrt::hstring const& id) const;
        [[nodiscard]] bool IsProfileRunningNow(winrt::hstring const& id);
        void Raise(wchar_t const* propertyName);

        std::shared_ptr<::ClaudeSwitcher::Core::ProfileCatalog> m_catalog;
        Microsoft::UI::Dispatching::DispatcherQueue m_queue;
        std::unique_ptr<::ClaudeSwitcher::ViewModels::StatusPoller> m_poller;
        ::ClaudeSwitcher::Core::InstallLookup m_install;
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::ClaudeSwitcher::ProfileCardViewModel> m_cards;
        std::vector<::ClaudeSwitcher::Core::ClaudeInstance> m_lastInstances;
        // Card id to the process id of its running main process.
        std::map<std::wstring, std::uint32_t> m_runningProcesses;
        HWND m_owner = nullptr;

        winrt::hstring m_statusText;
        bool m_messageOpen = false;
        winrt::hstring m_messageTitle;
        winrt::hstring m_messageText;
        Microsoft::UI::Xaml::Controls::InfoBarSeverity m_messageSeverity = Microsoft::UI::Xaml::Controls::InfoBarSeverity::Informational;
        // Closes confirmations after a few seconds; generations stop an older
        // timer from closing a newer message.
        Microsoft::UI::Dispatching::DispatcherQueueTimer m_messageTimer{ nullptr };
        std::uint64_t m_messageGeneration = 0;
        std::uint64_t m_timedMessageGeneration = 0;
        winrt::event<Microsoft::UI::Xaml::Data::PropertyChangedEventHandler> m_propertyChanged;
    };
}
