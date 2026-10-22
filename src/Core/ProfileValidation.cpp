#include "Core/ProfileValidation.h"

#include "Core/OrdinalText.h"

#include <algorithm>
#include <array>
#include <cwctype>
#include <stdexcept>

namespace ClaudeSwitcher::Core
{
    namespace
    {
        // Leaves room for "-profile" or a "-999" suffix within MaxFolderNameLength.
        constexpr std::size_t MaxSlugBaseLength = 32;
        constexpr int MaxFolderSuffix = 999;

        bool IsControlCharacter(wchar_t c) noexcept
        {
            return c < 0x20 || (c >= 0x7F && c <= 0x9F);
        }

        bool IsSlugCharacter(wchar_t c) noexcept
        {
            return (c >= L'a' && c <= L'z') || (c >= L'0' && c <= L'9');
        }

        wchar_t AsciiLower(wchar_t c) noexcept
        {
            return (c >= L'A' && c <= L'Z') ? static_cast<wchar_t>(c - L'A' + L'a') : c;
        }

        int HexDigitValue(wchar_t c) noexcept
        {
            if (c >= L'0' && c <= L'9')
            {
                return c - L'0';
            }
            if (c >= L'a' && c <= L'f')
            {
                return c - L'a' + 10;
            }
            if (c >= L'A' && c <= L'F')
            {
                return c - L'A' + 10;
            }
            return -1;
        }

        // Windows refuses these as file or folder names.
        bool IsReservedDeviceName(std::wstring_view name) noexcept
        {
            static constexpr std::array<std::wstring_view, 4> fixedNames{ L"con", L"prn", L"aux", L"nul" };
            for (auto const reserved : fixedNames)
            {
                if (EqualsOrdinalIgnoreCase(name, reserved))
                {
                    return true;
                }
            }

            if (name.size() != 4 || name[3] < L'1' || name[3] > L'9')
            {
                return false;
            }
            auto const stem = name.substr(0, 3);
            return EqualsOrdinalIgnoreCase(stem, L"com") || EqualsOrdinalIgnoreCase(stem, L"lpt");
        }
    }

    std::wstring TrimWhitespace(std::wstring_view text)
    {
        auto const isSpace = [](wchar_t c) { return std::iswspace(c) != 0; };
        auto const first = std::find_if_not(text.begin(), text.end(), isSpace);
        auto const last = std::find_if_not(text.rbegin(), text.rend(), isSpace).base();
        return first < last ? std::wstring{ first, last } : std::wstring{};
    }

    NameProblem CheckProfileName(std::wstring_view name) noexcept
    {
        if (name.empty())
        {
            return NameProblem::Empty;
        }
        if (name.size() > MaxProfileNameLength)
        {
            return NameProblem::TooLong;
        }
        if (std::any_of(name.begin(), name.end(), IsControlCharacter))
        {
            return NameProblem::InvalidCharacters;
        }
        return NameProblem::None;
    }

    bool IsValidProfileNote(std::wstring_view note) noexcept
    {
        return note.size() <= MaxProfileNoteLength && std::none_of(note.begin(), note.end(), IsControlCharacter);
    }

    bool IsValidAccentColor(std::wstring_view color) noexcept
    {
        return ParseAccentColor(color).has_value();
    }

    std::optional<std::uint32_t> ParseAccentColor(std::wstring_view color) noexcept
    {
        if (color.size() != 7 || color[0] != L'#')
        {
            return std::nullopt;
        }

        std::uint32_t value = 0;
        for (auto const c : color.substr(1))
        {
            auto const digit = HexDigitValue(c);
            if (digit < 0)
            {
                return std::nullopt;
            }
            value = (value << 4) | static_cast<std::uint32_t>(digit);
        }
        return value;
    }

    bool IsValidFolderName(std::wstring_view folderName) noexcept
    {
        if (folderName.empty() || folderName.size() > MaxFolderNameLength)
        {
            return false;
        }
        if (folderName.front() == L'-' || folderName.back() == L'-')
        {
            return false;
        }
        auto const allowed = [](wchar_t c) { return IsSlugCharacter(c) || c == L'-'; };
        return std::all_of(folderName.begin(), folderName.end(), allowed) && !IsReservedDeviceName(folderName);
    }

    std::wstring MakeUniqueFolderName(std::wstring_view displayName, std::vector<std::wstring> const& takenNames)
    {
        std::wstring base;
        auto pendingDash = false;
        for (auto const c : displayName)
        {
            auto const lower = AsciiLower(c);
            if (!IsSlugCharacter(lower))
            {
                pendingDash = true;
                continue;
            }
            if (base.size() + (pendingDash ? 2 : 1) > MaxSlugBaseLength)
            {
                break;
            }
            if (pendingDash && !base.empty())
            {
                base.push_back(L'-');
            }
            pendingDash = false;
            base.push_back(lower);
        }

        if (base.empty())
        {
            base = L"profile";
        }
        if (IsReservedDeviceName(base))
        {
            base += L"-profile";
        }

        auto const isTaken = [&takenNames](std::wstring_view candidate)
        {
            return std::any_of(
                takenNames.begin(),
                takenNames.end(),
                [candidate](std::wstring const& taken) { return EqualsOrdinalIgnoreCase(taken, candidate); });
        };

        if (!isTaken(base))
        {
            return base;
        }
        for (auto suffix = 2; suffix <= MaxFolderSuffix; ++suffix)
        {
            auto candidate = base + L"-" + std::to_wstring(suffix);
            if (!isTaken(candidate))
            {
                return candidate;
            }
        }
        throw std::runtime_error("No free profile folder name is left for this profile name.");
    }
}
