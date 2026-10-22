#include "Core/ProfileCatalog.h"

#include "Core/OrdinalText.h"
#include "Core/ProfileValidation.h"
#include "Core/SystemError.h"

#include <windows.h>
#include <combaseapi.h>

#include <algorithm>
#include <chrono>
#include <cwctype>
#include <system_error>
#include <utility>

#pragma comment(lib, "ole32.lib")

namespace ClaudeSwitcher::Core
{
    namespace
    {
        // The built-in card for Claude's own data uses this name.
        constexpr std::wstring_view ReservedName = L"Default";

        std::wstring NewProfileId()
        {
            GUID guid{};
            ThrowIfFailed(CoCreateGuid(&guid), "Creating a profile id");
            wchar_t text[40]{};
            if (StringFromGUID2(guid, text, static_cast<int>(std::size(text))) == 0)
            {
                ThrowWin32Error(ERROR_INSUFFICIENT_BUFFER, "Creating a profile id");
            }
            return text;
        }

        std::int64_t UnixSecondsNow()
        {
            auto const now = std::chrono::system_clock::now().time_since_epoch();
            return std::chrono::duration_cast<std::chrono::seconds>(now).count();
        }
    }

    ProfileCatalog::ProfileCatalog(ProfileStore store, std::filesystem::path defaultProfilesRoot, RootChecker rootChecker)
        : m_store(std::move(store)),
          m_defaultProfilesRoot(std::move(defaultProfilesRoot)),
          m_rootChecker(std::move(rootChecker))
    {
        ResolveRoot();
    }

    LoadResult ProfileCatalog::Load()
    {
        m_readOnly = true;
        auto result = m_store.Load();
        if (result.Status == LoadStatus::NewerSchema)
        {
            m_document = {};
            ResolveRoot();
            return result;
        }

        m_document = result.Document;
        m_readOnly = false;
        ResolveRoot();
        return result;
    }

    bool ProfileCatalog::IsReadOnly() const noexcept
    {
        return m_readOnly;
    }

    std::vector<Profile> const& ProfileCatalog::Profiles() const noexcept
    {
        return m_document.Profiles;
    }

    Profile const* ProfileCatalog::Find(std::wstring_view id) const noexcept
    {
        auto const found = std::find_if(
            m_document.Profiles.begin(), m_document.Profiles.end(), [id](Profile const& profile) { return profile.Id == id; });
        return found == m_document.Profiles.end() ? nullptr : &*found;
    }

    std::filesystem::path const& ProfileCatalog::ProfilesRoot() const noexcept
    {
        return m_profilesRoot;
    }

    RootProblem ProfileCatalog::ProfilesRootProblem() const noexcept
    {
        return m_rootProblem;
    }

    std::filesystem::path ProfileCatalog::DataDirectoryFor(Profile const& profile) const
    {
        // Folder names are validated on load and on creation, so this always
        // stays one level below the root.
        return m_profilesRoot / profile.FolderName;
    }

    bool ProfileCatalog::SignInHintDismissed() const noexcept
    {
        return m_document.SignInHintDismissed;
    }

    void ProfileCatalog::DismissSignInHint()
    {
        if (m_readOnly || m_document.SignInHintDismissed)
        {
            return;
        }
        auto document = m_document;
        document.SignInHintDismissed = true;
        Commit(std::move(document));
    }

    ProfileEditError ProfileCatalog::Validate(ProfileInput& input, std::wstring_view excludingId) const
    {
        if (m_readOnly)
        {
            return ProfileEditError::ReadOnly;
        }

        input.Name = TrimWhitespace(input.Name);
        input.Note = TrimWhitespace(input.Note);
        std::transform(input.AccentColor.begin(), input.AccentColor.end(), input.AccentColor.begin(), [](wchar_t c) {
            return static_cast<wchar_t>(std::towupper(c));
        });

        switch (CheckProfileName(input.Name))
        {
        case NameProblem::Empty:
            return ProfileEditError::NameEmpty;
        case NameProblem::TooLong:
            return ProfileEditError::NameTooLong;
        case NameProblem::InvalidCharacters:
            return ProfileEditError::NameInvalidCharacters;
        case NameProblem::None:
            break;
        }
        if (!IsValidProfileNote(input.Note))
        {
            return ProfileEditError::NoteInvalid;
        }
        if (!IsValidAccentColor(input.AccentColor))
        {
            return ProfileEditError::ColorInvalid;
        }

        auto const taken = EqualsOrdinalIgnoreCase(input.Name, ReservedName) ||
                           std::any_of(m_document.Profiles.begin(), m_document.Profiles.end(), [&](Profile const& profile) {
                               return profile.Id != excludingId && EqualsOrdinalIgnoreCase(profile.Name, input.Name);
                           });
        return taken ? ProfileEditError::NameTaken : ProfileEditError::None;
    }

    ProfileCatalog::AddResult ProfileCatalog::Add(ProfileInput input)
    {
        AddResult result;
        result.Error = Validate(input, {});
        if (result.Error != ProfileEditError::None)
        {
            return result;
        }
        if (m_rootProblem != RootProblem::None)
        {
            result.Error = ProfileEditError::ProfilesRootUnusable;
            return result;
        }

        // Folders already on disk count as taken, so a new profile never
        // adopts data left by a deleted one or anything else in the root.
        auto takenFolders = ListFolderNames(m_profilesRoot);
        for (auto const& profile : m_document.Profiles)
        {
            takenFolders.push_back(profile.FolderName);
        }

        Profile profile;
        profile.Id = NewProfileId();
        profile.Name = std::move(input.Name);
        profile.Note = std::move(input.Note);
        profile.AccentColor = std::move(input.AccentColor);
        profile.FolderName = MakeUniqueFolderName(profile.Name, takenFolders);
        profile.CreatedAtUnixSeconds = UnixSecondsNow();

        auto const folder = DataDirectoryFor(profile);
        std::error_code folderError;
        std::filesystem::create_directories(folder, folderError);
        if (folderError)
        {
            throw std::system_error(folderError, "Creating the profile folder");
        }

        auto document = m_document;
        document.Profiles.push_back(profile);
        try
        {
            Commit(std::move(document));
        }
        catch (...)
        {
            // Only removes the folder if it is still empty, which it is: it
            // was created a moment ago and nothing has been started with it.
            std::error_code ignored;
            std::filesystem::remove(folder, ignored);
            throw;
        }

        result.Created = std::move(profile);
        return result;
    }

    ProfileEditError ProfileCatalog::Update(std::wstring_view id, ProfileInput input)
    {
        auto const error = Validate(input, id);
        if (error != ProfileEditError::None)
        {
            return error;
        }

        auto document = m_document;
        auto const found = std::find_if(
            document.Profiles.begin(), document.Profiles.end(), [id](Profile const& profile) { return profile.Id == id; });
        if (found == document.Profiles.end())
        {
            return ProfileEditError::NotFound;
        }
        found->Name = std::move(input.Name);
        found->Note = std::move(input.Note);
        found->AccentColor = std::move(input.AccentColor);
        Commit(std::move(document));
        return ProfileEditError::None;
    }

    ProfileEditError ProfileCatalog::Remove(std::wstring_view id)
    {
        if (m_readOnly)
        {
            return ProfileEditError::ReadOnly;
        }

        auto document = m_document;
        auto const removed = std::erase_if(document.Profiles, [id](Profile const& profile) { return profile.Id == id; });
        if (removed == 0)
        {
            return ProfileEditError::NotFound;
        }
        Commit(std::move(document));
        return ProfileEditError::None;
    }

    void ProfileCatalog::Commit(ProfileDocument document)
    {
        m_store.Save(document);
        m_document = std::move(document);
    }

    void ProfileCatalog::ResolveRoot()
    {
        m_profilesRoot = m_document.ProfilesRoot.empty() ? m_defaultProfilesRoot
                                                         : std::filesystem::path{ m_document.ProfilesRoot };
        try
        {
            m_rootProblem = m_rootChecker(m_profilesRoot);
        }
        catch (std::exception const&)
        {
            m_rootProblem = RootProblem::DriveUnavailable;
        }
    }
}
