#pragma once

#include "Core/ProfileFolders.h"
#include "Core/ProfileStore.h"

#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace ClaudeSwitcher::Core
{
    struct ProfileInput
    {
        std::wstring Name;
        std::wstring Note;
        std::wstring AccentColor;
    };

    enum class ProfileEditError
    {
        None,
        NameEmpty,
        NameTooLong,
        NameInvalidCharacters,
        NameTaken,
        NoteInvalid,
        ColorInvalid,
        ReadOnly,
        ProfilesRootUnusable,
        NotFound,
    };

    // The profile list and the rules for changing it. Every change is saved
    // before it becomes visible, so memory and disk never disagree. Not
    // thread-safe: the app uses it only from the UI thread.
    class ProfileCatalog
    {
    public:
        using RootChecker = std::function<RootProblem(std::filesystem::path const&)>;

        ProfileCatalog(ProfileStore store, std::filesystem::path defaultProfilesRoot, RootChecker rootChecker);

        // Throws std::system_error when the settings file cannot be read.
        // After a throw or a NewerSchema result the catalog stays read-only.
        [[nodiscard]] LoadResult Load();

        [[nodiscard]] bool IsReadOnly() const noexcept;
        [[nodiscard]] std::vector<Profile> const& Profiles() const noexcept;
        [[nodiscard]] Profile const* Find(std::wstring_view id) const noexcept;
        [[nodiscard]] std::filesystem::path const& ProfilesRoot() const noexcept;
        [[nodiscard]] RootProblem ProfilesRootProblem() const noexcept;
        [[nodiscard]] std::filesystem::path DataDirectoryFor(Profile const& profile) const;
        [[nodiscard]] bool SignInHintDismissed() const noexcept;

        // Throws std::system_error when saving fails.
        void DismissSignInHint();

        // Normalizes (trims names and notes, upper-cases colours) and checks
        // an input. excludingId skips that profile in the duplicate-name check.
        [[nodiscard]] ProfileEditError Validate(ProfileInput& input, std::wstring_view excludingId) const;

        struct AddResult
        {
            ProfileEditError Error = ProfileEditError::None;
            Profile Created;
        };

        // Creates the profile's data folder and saves. Throws std::system_error
        // when the folder or the list cannot be written; nothing is left behind.
        [[nodiscard]] AddResult Add(ProfileInput input);

        // Throws std::system_error when saving fails.
        [[nodiscard]] ProfileEditError Update(std::wstring_view id, ProfileInput input);

        // Forgets a profile. The data folder is the caller's to remove first.
        // Throws std::system_error when saving fails.
        [[nodiscard]] ProfileEditError Remove(std::wstring_view id);

    private:
        void Commit(ProfileDocument document);
        void ResolveRoot();

        ProfileStore m_store;
        std::filesystem::path m_defaultProfilesRoot;
        RootChecker m_rootChecker;
        ProfileDocument m_document;
        std::filesystem::path m_profilesRoot;
        RootProblem m_rootProblem = RootProblem::None;
        bool m_readOnly = true;
    };
}
