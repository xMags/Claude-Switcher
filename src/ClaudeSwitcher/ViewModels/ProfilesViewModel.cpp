#include "pch.h"
#include "ViewModels/ProfilesViewModel.h"
#if __has_include("ProfilesViewModel.g.cpp")
#include "ProfilesViewModel.g.cpp"
#endif

#include "Core/ClaudeLauncher.h"
#include "Core/CommandLineParsing.h"
#include "Core/KnownFolders.h"
#include "Core/ProfileFolders.h"
#include "Core/ShortcutWriter.h"
#include "Core/SystemError.h"
#include "Shell/AppPaths.h"
#include "ViewModels/ProfileCardViewModel.h"

#include <shellapi.h>

#include <chrono>
#include <system_error>
#include <utility>

namespace winrt::ClaudeSwitcher::implementation
{
    namespace
    {
        namespace core = ::ClaudeSwitcher::Core;
        using Microsoft::UI::Xaml::Controls::InfoBarSeverity;

        constexpr wchar_t DefaultCardId[] = L"default";
        // Reserved, so no profile can share it (or its shortcut file name).
        constexpr wchar_t DefaultCardName[] = L"Default";
        constexpr wchar_t DefaultCardColor[] = L"#D97757";
        constexpr std::chrono::seconds MessageLifetime{ 6 };

        winrt::hstring DescribeError(std::system_error const& error)
        {
            return winrt::hstring{ core::DescribeSystemError(error.code().value()) };
        }

        winrt::hstring DescribeRootProblem(core::RootProblem problem)
        {
            switch (problem)
            {
            case core::RootProblem::NotAbsolute:
                return L"its location is not a full path.";
            case core::RootProblem::NetworkPath:
                return L"it is on a network drive, which Claude does not support for its data.";
            case core::RootProblem::InsideAppData:
                return L"it is inside AppData, where Windows would redirect Claude's files somewhere else.";
            case core::RootProblem::DriveUnavailable:
                return L"its drive is not available.";
            case core::RootProblem::None:
                break;
            }
            return L"it cannot be used.";
        }

        winrt::hstring DescribeEditError(core::ProfileEditError error, core::RootProblem rootProblem)
        {
            switch (error)
            {
            case core::ProfileEditError::None:
                return {};
            case core::ProfileEditError::NameEmpty:
                return L"Enter a name for the profile.";
            case core::ProfileEditError::NameTooLong:
                return L"Use a name of 40 characters or fewer.";
            case core::ProfileEditError::NameInvalidCharacters:
                return L"The name contains characters that cannot be used.";
            case core::ProfileEditError::NameTaken:
                return L"That name is already in use. Pick another one.";
            case core::ProfileEditError::NoteInvalid:
                return L"Use a note of 80 characters or fewer.";
            case core::ProfileEditError::ColorInvalid:
                return L"Pick a colour for the profile.";
            case core::ProfileEditError::ReadOnly:
                return L"Profiles can't be changed right now. See the message at the top of the window.";
            case core::ProfileEditError::ProfilesRootUnusable:
                return L"New profiles can't be created because the profiles folder can't be used: " + DescribeRootProblem(rootProblem);
            case core::ProfileEditError::NotFound:
                return L"That profile no longer exists.";
            }
            return L"The profile could not be saved.";
        }
    }

    ProfilesViewModel::ProfilesViewModel(
        std::shared_ptr<core::ProfileCatalog> catalog,
        Microsoft::UI::Dispatching::DispatcherQueue queue)
        : m_catalog(std::move(catalog)),
          m_queue(std::move(queue)),
          m_cards(winrt::single_threaded_observable_vector<winrt::ClaudeSwitcher::ProfileCardViewModel>())
    {
        LoadCatalog();
        static_cast<void>(RefreshInstallation(true));
        RebuildCards();
        UpdateStatusText();
    }

    winrt::Windows::Foundation::Collections::IObservableVector<winrt::ClaudeSwitcher::ProfileCardViewModel> ProfilesViewModel::Cards() const
    {
        return m_cards;
    }

    winrt::hstring ProfilesViewModel::StatusText() const { return m_statusText; }

    bool ProfilesViewModel::CanAddProfiles() const noexcept
    {
        return !m_catalog->IsReadOnly() && m_catalog->ProfilesRootProblem() == core::RootProblem::None;
    }

    Microsoft::UI::Xaml::Visibility ProfilesViewModel::EmptyHintVisibility() const noexcept
    {
        return m_catalog->Profiles().empty() ? Microsoft::UI::Xaml::Visibility::Visible : Microsoft::UI::Xaml::Visibility::Collapsed;
    }

    bool ProfilesViewModel::IsMessageOpen() const noexcept { return m_messageOpen; }
    winrt::hstring ProfilesViewModel::MessageTitle() const { return m_messageTitle; }
    winrt::hstring ProfilesViewModel::MessageText() const { return m_messageText; }
    InfoBarSeverity ProfilesViewModel::MessageSeverity() const noexcept { return m_messageSeverity; }

    void ProfilesViewModel::DismissMessage()
    {
        if (!m_messageOpen)
        {
            return;
        }
        m_messageOpen = false;
        Raise(L"IsMessageOpen");
    }

    bool ProfilesViewModel::IsSignInHintOpen() const noexcept
    {
        return !m_catalog->IsReadOnly() && !m_catalog->SignInHintDismissed();
    }

    void ProfilesViewModel::DismissSignInHint()
    {
        try
        {
            m_catalog->DismissSignInHint();
        }
        catch (std::system_error const& error)
        {
            ShowMessage(InfoBarSeverity::Warning, L"Couldn't save that setting", DescribeError(error));
        }
        Raise(L"IsSignInHintOpen");
    }

    void ProfilesViewModel::Launch(winrt::hstring const& id)
    {
        auto const installation = RefreshInstallation(true);
        if (!installation)
        {
            return;
        }

        std::optional<std::filesystem::path> dataDirectory;
        if (id != DefaultCardId)
        {
            auto const profile = FindProfile(id);
            if (profile == nullptr)
            {
                ShowMessage(InfoBarSeverity::Error, L"Couldn't open Claude", L"That profile no longer exists.");
                return;
            }
            if (m_catalog->ProfilesRootProblem() != core::RootProblem::None)
            {
                ShowMessage(
                    InfoBarSeverity::Error,
                    L"Couldn't open Claude",
                    L"The profiles folder can't be used: " + DescribeRootProblem(m_catalog->ProfilesRootProblem()));
                return;
            }
            dataDirectory = m_catalog->DataDirectoryFor(*profile);
        }

        try
        {
            if (dataDirectory)
            {
                // Recreated if someone removed it by hand; Claude would
                // otherwise create it itself, but without the checks above.
                std::error_code folderError;
                std::filesystem::create_directories(*dataDirectory, folderError);
                if (folderError)
                {
                    throw std::system_error(folderError, "Creating the profile folder");
                }
            }
            core::LaunchClaude(*installation, dataDirectory, RunningProcessFor(id));
        }
        catch (std::system_error const& error)
        {
            ShowMessage(InfoBarSeverity::Error, L"Couldn't open Claude", DescribeError(error));
        }
    }

    winrt::hstring ProfilesViewModel::AddProfile(winrt::hstring const& name, winrt::hstring const& note, winrt::hstring const& accentColor)
    {
        try
        {
            auto const result = m_catalog->Add({ std::wstring{ name }, std::wstring{ note }, std::wstring{ accentColor } });
            if (result.Error != core::ProfileEditError::None)
            {
                return DescribeEditError(result.Error, m_catalog->ProfilesRootProblem());
            }
        }
        catch (std::system_error const& error)
        {
            return L"The profile could not be saved: " + DescribeError(error);
        }
        catch (std::exception const&)
        {
            return L"The profile could not be saved.";
        }

        RebuildCards();
        Raise(L"EmptyHintVisibility");
        return {};
    }

    winrt::hstring ProfilesViewModel::UpdateProfile(
        winrt::hstring const& id,
        winrt::hstring const& name,
        winrt::hstring const& note,
        winrt::hstring const& accentColor)
    {
        auto const existing = FindProfile(id);
        if (existing == nullptr)
        {
            return DescribeEditError(core::ProfileEditError::NotFound, m_catalog->ProfilesRootProblem());
        }
        auto const before = *existing;

        try
        {
            auto const error = m_catalog->Update(id, { std::wstring{ name }, std::wstring{ note }, std::wstring{ accentColor } });
            if (error != core::ProfileEditError::None)
            {
                return DescribeEditError(error, m_catalog->ProfilesRootProblem());
            }
        }
        catch (std::system_error const& error)
        {
            return L"The profile could not be saved: " + DescribeError(error);
        }

        if (auto const after = FindProfile(id); after != nullptr && after->Name != before.Name)
        {
            RenameShortcuts(before, *after);
        }
        RebuildCards();
        return {};
    }

    bool ProfilesViewModel::CanDeleteProfile(winrt::hstring const& id)
    {
        auto const profile = FindProfile(id);
        if (profile == nullptr || m_catalog->IsReadOnly())
        {
            return false;
        }
        if (IsProfileRunningNow(id))
        {
            ShowMessage(
                InfoBarSeverity::Warning,
                L"Quit Claude for " + winrt::hstring{ profile->Name } + L" first",
                L"Its Claude window is still running. Quit it from Claude's tray icon or File menu, then delete the profile.");
            return false;
        }
        return true;
    }

    void ProfilesViewModel::DeleteProfile(winrt::hstring const& id)
    {
        // Checked again: Claude may have been started from a shortcut while
        // the confirmation was open.
        if (!CanDeleteProfile(id))
        {
            return;
        }

        auto const profile = *FindProfile(id);
        auto const folder = m_catalog->DataDirectoryFor(profile);
        if (!core::IsDirectChildOf(folder, m_catalog->ProfilesRoot()))
        {
            ShowMessage(InfoBarSeverity::Error, L"Couldn't delete the profile", L"Its data folder is not inside the profiles folder.");
            return;
        }

        try
        {
            if (core::RecycleFolder(folder, m_owner) == core::RecycleOutcome::Cancelled)
            {
                return;
            }
            if (m_catalog->Remove(id) != core::ProfileEditError::None)
            {
                return;
            }
        }
        catch (std::system_error const& error)
        {
            ShowMessage(InfoBarSeverity::Error, L"Couldn't delete the profile", DescribeError(error));
            return;
        }

        // A shortcut left behind would quietly recreate the folder.
        RemoveShortcuts(profile);

        RebuildCards();
        Raise(L"EmptyHintVisibility");
        ShowMessage(
            InfoBarSeverity::Success,
            winrt::hstring{ profile.Name } + L" was deleted",
            L"Its Claude data folder is in the Recycle Bin. Your Claude account and chats are not affected.");
    }

    void ProfilesViewModel::CreateDesktopShortcut(winrt::hstring const& id)
    {
        CreateShortcut(id, core::ShortcutLocation::Desktop);
    }

    void ProfilesViewModel::CreateStartMenuShortcut(winrt::hstring const& id)
    {
        CreateShortcut(id, core::ShortcutLocation::StartMenu);
    }

    void ProfilesViewModel::CreateShortcut(winrt::hstring const& id, core::ShortcutLocation location)
    {
        // The Default card's shortcut opens Claude with no profile folder,
        // that is, with the user's existing account.
        std::wstring name{ DefaultCardName };
        std::optional<std::filesystem::path> dataDirectory;
        if (id != DefaultCardId)
        {
            auto const profile = FindProfile(id);
            if (profile == nullptr)
            {
                return;
            }
            name = profile->Name;
            dataDirectory = m_catalog->DataDirectoryFor(*profile);
        }
        auto const installation = RefreshInstallation(true);
        if (!installation)
        {
            return;
        }

        try
        {
            auto const spec = MakeShortcutSpec(name, dataDirectory, *installation, location);
            core::WriteShortcut(spec);
            auto const shortcutName = winrt::hstring{ spec.Location.stem().wstring() };
            if (location == core::ShortcutLocation::StartMenu)
            {
                ShowMessage(
                    InfoBarSeverity::Success,
                    L"Added to Start",
                    shortcutName + L" is in Start under All apps > Claude Profiles. Right-click it there to pin it.");
            }
            else
            {
                ShowMessage(InfoBarSeverity::Success, L"Shortcut added", shortcutName + L" is on your desktop.");
            }
        }
        catch (std::system_error const& error)
        {
            ShowMessage(InfoBarSeverity::Error, L"Couldn't create the shortcut", DescribeError(error));
        }
    }

    core::ShortcutSpec ProfilesViewModel::MakeShortcutSpec(
        std::wstring const& name,
        std::optional<std::filesystem::path> const& dataDirectory,
        core::ClaudeInstallation const& installation,
        core::ShortcutLocation location) const
    {
        core::ShortcutSpec spec;
        spec.Location = core::ShortcutPathFor(location, name);
        spec.Target = installation.AliasPath;
        spec.Arguments = core::BuildClaudeArguments(dataDirectory);
        spec.Description = dataDirectory ? L"Open Claude with the " + name + L" profile" : L"Open Claude with your existing account";
        spec.WorkingDirectory = core::GetKnownFolderPath(core::KnownFolder::UserProfile);
        spec.IconPath = ::ClaudeSwitcher::Shell::EnsureShortcutIcon();
        return spec;
    }

    bool ProfilesViewModel::IsShortcutFor(std::filesystem::path const& shortcut, core::Profile const& profile) const
    {
        // Matching on the exact folder argument means only shortcuts this
        // app made for this profile are ever moved or removed.
        return core::ReadShortcutArguments(shortcut) == core::BuildClaudeArguments(m_catalog->DataDirectoryFor(profile));
    }

    void ProfilesViewModel::RemoveShortcuts(core::Profile const& profile)
    {
        for (auto const location : core::AllShortcutLocations)
        {
            try
            {
                auto const shortcut = core::ShortcutPathFor(location, profile.Name);
                if (IsShortcutFor(shortcut, profile))
                {
                    static_cast<void>(DeleteFileW(shortcut.c_str()));
                }
                if (location == core::ShortcutLocation::StartMenu)
                {
                    // Succeeds only once the folder is empty.
                    static_cast<void>(RemoveDirectoryW(core::ShortcutFolderFor(location).c_str()));
                }
            }
            catch (std::system_error const&)
            {
                // The profile is gone either way; a stale shortcut is harmless to leave.
            }
        }
    }

    void ProfilesViewModel::RenameShortcuts(core::Profile const& before, core::Profile const& after)
    {
        auto const installation = RefreshInstallation(false);
        for (auto const location : core::AllShortcutLocations)
        {
            try
            {
                auto const oldPath = core::ShortcutPathFor(location, before.Name);
                if (!IsShortcutFor(oldPath, before))
                {
                    continue;
                }
                auto const newPath = core::ShortcutPathFor(location, after.Name);
                auto const samePath = core::PathsReferToSameLocation(oldPath.wstring(), newPath.wstring());
                // Never replace a different file that already has the new name.
                if (!samePath && GetFileAttributesW(newPath.c_str()) != INVALID_FILE_ATTRIBUTES && !IsShortcutFor(newPath, after))
                {
                    continue;
                }

                if (installation)
                {
                    core::WriteShortcut(MakeShortcutSpec(after.Name, m_catalog->DataDirectoryFor(after), *installation, location));
                    if (!samePath)
                    {
                        static_cast<void>(DeleteFileW(oldPath.c_str()));
                    }
                }
                else if (!samePath)
                {
                    // Claude can't be found right now; keep the shortcut as it is, under the new name.
                    static_cast<void>(MoveFileExW(oldPath.c_str(), newPath.c_str(), MOVEFILE_REPLACE_EXISTING));
                }
            }
            catch (std::system_error const&)
            {
                // The old shortcut still works; it just keeps the old name.
            }
        }
    }

    void ProfilesViewModel::OpenDataFolder(winrt::hstring const& id)
    {
        std::filesystem::path folder;
        try
        {
            if (id == DefaultCardId)
            {
                folder = core::DefaultClaudeDataDirectory();
            }
            else if (auto const profile = FindProfile(id))
            {
                folder = m_catalog->DataDirectoryFor(*profile);
            }
            else
            {
                return;
            }
        }
        catch (std::system_error const& error)
        {
            ShowMessage(InfoBarSeverity::Error, L"Couldn't open the folder", DescribeError(error));
            return;
        }

        if (GetFileAttributesW(folder.c_str()) == INVALID_FILE_ATTRIBUTES)
        {
            ShowMessage(InfoBarSeverity::Informational, L"No data yet", L"Claude creates this folder the first time it opens with this profile.");
            return;
        }

        // ShellExecuteW reports success as a value above 32.
        auto const result = reinterpret_cast<INT_PTR>(ShellExecuteW(m_owner, L"open", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
        if (result <= 32)
        {
            ShowMessage(InfoBarSeverity::Error, L"Couldn't open the folder", winrt::hstring{ folder.wstring() });
        }
    }

    void ProfilesViewModel::RefreshRunningState()
    {
        EnsurePoller();
        m_poller->RefreshNow();
    }

    void ProfilesViewModel::SetStatusUpdatesPaused(bool paused)
    {
        EnsurePoller();
        if (paused)
        {
            m_poller->Stop();
        }
        else
        {
            m_poller->Start();
        }
    }

    winrt::event_token ProfilesViewModel::PropertyChanged(Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler)
    {
        return m_propertyChanged.add(handler);
    }

    void ProfilesViewModel::PropertyChanged(winrt::event_token const& token) noexcept
    {
        m_propertyChanged.remove(token);
    }

    void ProfilesViewModel::AttachOwnerWindow(HWND owner) noexcept
    {
        m_owner = owner;
    }

    void ProfilesViewModel::LoadCatalog()
    {
        try
        {
            auto const result = m_catalog->Load();
            switch (result.Status)
            {
            case core::LoadStatus::Quarantined:
                ShowMessage(
                    InfoBarSeverity::Warning,
                    L"Your profile list couldn't be read",
                    L"It was kept as " + winrt::hstring{ result.QuarantinedTo.filename().wstring() } +
                        L" and a new list was started. Profile data folders were not touched.");
                break;
            case core::LoadStatus::NewerSchema:
                ShowMessage(
                    InfoBarSeverity::Error,
                    L"Profiles are read-only",
                    L"Your profile list was saved by a newer version of Claude Switcher. Update the app to change profiles.");
                break;
            case core::LoadStatus::Loaded:
            case core::LoadStatus::Missing:
                break;
            }
        }
        catch (std::system_error const& error)
        {
            ShowMessage(InfoBarSeverity::Error, L"Profiles couldn't be loaded", DescribeError(error));
        }

        if (!m_catalog->IsReadOnly() && m_catalog->ProfilesRootProblem() != core::RootProblem::None)
        {
            ShowMessage(
                InfoBarSeverity::Error,
                L"The profiles folder can't be used",
                winrt::hstring{ m_catalog->ProfilesRoot().wstring() } + L" can't hold profiles because " +
                    DescribeRootProblem(m_catalog->ProfilesRootProblem()));
        }
    }

    std::optional<core::ClaudeInstallation> ProfilesViewModel::RefreshInstallation(bool reportProblems)
    {
        try
        {
            m_install = core::LocateClaudeInstallation();
        }
        catch (std::system_error const& error)
        {
            m_install = {};
            if (reportProblems)
            {
                ShowMessage(InfoBarSeverity::Error, L"Couldn't find Claude Desktop", DescribeError(error));
            }
            return std::nullopt;
        }

        auto const canLaunch = m_install.Status == core::InstallStatus::Found;
        for (auto const& card : m_cards)
        {
            winrt::get_self<ProfileCardViewModel>(card)->SetCanLaunch(canLaunch);
        }
        UpdateStatusText();

        if (canLaunch)
        {
            return m_install.Installation;
        }
        if (reportProblems && m_install.Status == core::InstallStatus::NotInstalled)
        {
            ShowMessage(
                InfoBarSeverity::Error,
                L"Claude Desktop isn't installed",
                L"Install Claude Desktop from claude.ai/download, then open it once before using Claude Switcher.");
        }
        else if (reportProblems)
        {
            ShowMessage(
                InfoBarSeverity::Error,
                L"Claude Desktop can't be started",
                L"Turn on the claude-desktop.exe alias in Settings > Apps > Advanced app settings > App execution aliases.");
        }
        return std::nullopt;
    }

    void ProfilesViewModel::RebuildCards()
    {
        auto const canLaunch = m_install.Status == core::InstallStatus::Found;
        std::vector<winrt::ClaudeSwitcher::ProfileCardViewModel> cards;
        cards.push_back(winrt::make<ProfileCardViewModel>(
            winrt::hstring{ DefaultCardId }, winrt::hstring{ DefaultCardName }, L"Your existing Claude account", winrt::hstring{ DefaultCardColor }, true));
        for (auto const& profile : m_catalog->Profiles())
        {
            cards.push_back(winrt::make<ProfileCardViewModel>(
                winrt::hstring{ profile.Id },
                winrt::hstring{ profile.Name },
                winrt::hstring{ profile.Note },
                winrt::hstring{ profile.AccentColor },
                false));
        }
        for (auto const& card : cards)
        {
            winrt::get_self<ProfileCardViewModel>(card)->SetCanLaunch(canLaunch);
        }
        m_cards.ReplaceAll(cards);

        ApplyInstances(m_lastInstances);
    }

    void ProfilesViewModel::ApplyInstances(std::vector<core::ClaudeInstance> instances)
    {
        m_lastInstances = std::move(instances);
        m_runningProcesses.clear();
        for (auto const& instance : m_lastInstances)
        {
            if (auto const cardId = CardIdForSession(instance.UserDataDir))
            {
                m_runningProcesses.emplace(*cardId, instance.ProcessId);
            }
        }

        for (auto const& card : m_cards)
        {
            auto const running = m_runningProcesses.contains(std::wstring{ card.Id() });
            winrt::get_self<ProfileCardViewModel>(card)->SetRunning(running);
        }
        UpdateStatusText();
    }

    void ProfilesViewModel::UpdateStatusText()
    {
        winrt::hstring text;
        if (m_install.Status == core::InstallStatus::NotInstalled || !m_install.Installation)
        {
            text = L"Claude Desktop not found";
        }
        else
        {
            text = L"Claude Desktop " + winrt::hstring{ m_install.Installation->Version };
            auto const open = m_runningProcesses.size();
            text = text + L"  ·  " + winrt::to_hstring(open) + (open == 1 ? L" session open" : L" sessions open");
        }

        if (text != m_statusText)
        {
            m_statusText = text;
            Raise(L"StatusText");
        }
    }

    void ProfilesViewModel::ShowMessage(InfoBarSeverity severity, winrt::hstring const& title, winrt::hstring const& text)
    {
        m_messageSeverity = severity;
        m_messageTitle = title;
        m_messageText = text;
        Raise(L"MessageSeverity");
        Raise(L"MessageTitle");
        Raise(L"MessageText");
        // Re-raised even when already open, so a message closed in the
        // InfoBar itself reappears.
        m_messageOpen = true;
        Raise(L"IsMessageOpen");

        // Confirmations fade out by themselves; warnings and errors stay
        // until closed, because they usually ask the user to do something.
        ++m_messageGeneration;
        if (m_messageTimer)
        {
            m_messageTimer.Stop();
        }
        if (severity != InfoBarSeverity::Success && severity != InfoBarSeverity::Informational)
        {
            return;
        }
        if (!m_messageTimer)
        {
            m_messageTimer = m_queue.CreateTimer();
            m_messageTimer.IsRepeating(false);
            m_messageTimer.Interval(MessageLifetime);
            // The timer is stopped in the destructor, so this cannot outlive it.
            m_messageTimer.Tick([this](auto&&, auto&&) {
                // A newer message restarted the timer; only the latest one closes.
                if (m_messageGeneration == m_timedMessageGeneration)
                {
                    DismissMessage();
                }
            });
        }
        m_timedMessageGeneration = m_messageGeneration;
        m_messageTimer.Start();
    }

    ProfilesViewModel::~ProfilesViewModel()
    {
        if (m_messageTimer)
        {
            m_messageTimer.Stop();
        }
    }

    void ProfilesViewModel::EnsurePoller()
    {
        if (m_poller)
        {
            return;
        }
        // Created on first use rather than in the constructor, where a weak
        // reference to this object cannot be taken yet.
        m_poller = std::make_unique<::ClaudeSwitcher::ViewModels::StatusPoller>(
            m_queue,
            std::wstring{ core::ClaudePackageFamilyName },
            [weak = get_weak()](std::vector<core::ClaudeInstance> instances)
            {
                if (auto const self = weak.get())
                {
                    self->ApplyInstances(std::move(instances));
                }
            });
    }

    core::Profile const* ProfilesViewModel::FindProfile(winrt::hstring const& id) const
    {
        return m_catalog->Find(id);
    }

    std::optional<std::wstring> ProfilesViewModel::CardIdForSession(std::optional<std::wstring> const& userDataDir) const
    {
        if (!userDataDir)
        {
            return std::wstring{ DefaultCardId };
        }
        for (auto const& profile : m_catalog->Profiles())
        {
            if (core::PathsReferToSameLocation(*userDataDir, m_catalog->DataDirectoryFor(profile).wstring()))
            {
                return profile.Id;
            }
        }
        // A Claude started with some other folder; not one of ours.
        return std::nullopt;
    }

    std::optional<std::uint32_t> ProfilesViewModel::RunningProcessFor(winrt::hstring const& id) const
    {
        auto const found = m_runningProcesses.find(std::wstring{ id });
        return found == m_runningProcesses.end() ? std::nullopt : std::optional<std::uint32_t>{ found->second };
    }

    bool ProfilesViewModel::IsProfileRunningNow(winrt::hstring const& id)
    {
        // A fresh scan rather than the last poll, which may be two seconds
        // old. It takes milliseconds, so it is fine on the UI thread.
        try
        {
            ApplyInstances(core::ScanClaudeInstances(core::ClaudePackageFamilyName));
        }
        catch (std::system_error const&)
        {
            // Fall back to the last known state.
        }
        return RunningProcessFor(id).has_value();
    }

    void ProfilesViewModel::Raise(wchar_t const* propertyName)
    {
        m_propertyChanged(*this, Microsoft::UI::Xaml::Data::PropertyChangedEventArgs{ propertyName });
    }
}
