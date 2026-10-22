#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ClaudeSwitcher::Core
{
    inline constexpr std::size_t MaxProfileNameLength = 40;
    inline constexpr std::size_t MaxProfileNoteLength = 80;
    inline constexpr std::size_t MaxFolderNameLength = 48;

    enum class NameProblem
    {
        None,
        Empty,
        TooLong,
        InvalidCharacters,
    };

    [[nodiscard]] std::wstring TrimWhitespace(std::wstring_view text);

    // Expects an already trimmed name.
    [[nodiscard]] NameProblem CheckProfileName(std::wstring_view name) noexcept;
    [[nodiscard]] bool IsValidProfileNote(std::wstring_view note) noexcept;

    // Accent colours are stored as "#RRGGBB".
    [[nodiscard]] bool IsValidAccentColor(std::wstring_view color) noexcept;
    // Returns 0xRRGGBB.
    [[nodiscard]] std::optional<std::uint32_t> ParseAccentColor(std::wstring_view color) noexcept;

    // A profile folder name: 1 to MaxFolderNameLength characters of a-z, 0-9
    // and inner dashes, never a reserved Windows device name.
    [[nodiscard]] bool IsValidFolderName(std::wstring_view folderName) noexcept;

    // Derives a folder name from a display name and makes it unique against
    // takenNames (compared case-insensitively, as the file system would).
    // Throws std::runtime_error if every numbered variant is taken.
    [[nodiscard]] std::wstring MakeUniqueFolderName(
        std::wstring_view displayName,
        std::vector<std::wstring> const& takenNames);
}
