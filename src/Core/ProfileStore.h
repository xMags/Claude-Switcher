#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace ClaudeSwitcher::Core
{
    struct Profile
    {
        std::wstring Id;
        std::wstring Name;
        std::wstring Note;
        std::wstring AccentColor;
        // Name of the profile's data folder inside the profiles root. Fixed at
        // creation so renaming a profile never moves a folder Claude may be using.
        std::wstring FolderName;
        std::int64_t CreatedAtUnixSeconds = 0;
    };

    struct ProfileDocument
    {
        std::vector<Profile> Profiles;
        // Empty means the default root.
        std::wstring ProfilesRoot;
        bool SignInHintDismissed = false;
    };

    inline constexpr std::int64_t CurrentSchemaVersion = 1;

    enum class ParseStatus
    {
        Parsed,
        Invalid,
        // Written by a newer build of the switcher. Never overwritten, so a
        // downgrade cannot destroy data it does not understand.
        NewerSchema,
    };

    struct ParseResult
    {
        ParseStatus Status = ParseStatus::Invalid;
        ProfileDocument Document;
    };

    [[nodiscard]] ParseResult ParseProfileDocument(std::wstring_view json);
    [[nodiscard]] std::wstring SerializeProfileDocument(ProfileDocument const& document);

    enum class LoadStatus
    {
        Loaded,
        Missing,
        // The file was unreadable as a profile document and has been renamed
        // aside (QuarantinedTo) so it can be inspected; loading starts empty.
        Quarantined,
        NewerSchema,
    };

    struct LoadResult
    {
        LoadStatus Status = LoadStatus::Missing;
        ProfileDocument Document;
        std::filesystem::path QuarantinedTo;
    };

    // Reads and writes the switcher's profile list. Not thread-safe: the
    // owner serializes access (the app touches it only from the UI thread).
    class ProfileStore
    {
    public:
        explicit ProfileStore(std::filesystem::path filePath);

        [[nodiscard]] std::filesystem::path const& FilePath() const noexcept;

        // Throws std::system_error when the file exists but cannot be read,
        // or when a corrupt file cannot be moved aside.
        [[nodiscard]] LoadResult Load() const;

        // Writes a temporary file and atomically replaces the old one, so a
        // crash mid-save leaves either the old or the new list, never half of
        // one. Throws std::system_error.
        void Save(ProfileDocument const& document) const;

    private:
        std::filesystem::path m_filePath;
    };
}
