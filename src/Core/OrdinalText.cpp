#include "Core/OrdinalText.h"

#include <windows.h>

#include <limits>

namespace ClaudeSwitcher::Core
{
    bool EqualsOrdinalIgnoreCase(std::wstring_view left, std::wstring_view right) noexcept
    {
        if (left.size() != right.size())
        {
            return false;
        }
        if (left.empty())
        {
            return true;
        }
        if (left.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        {
            return false;
        }

        auto const length = static_cast<int>(left.size());
        return CompareStringOrdinal(left.data(), length, right.data(), length, TRUE) == CSTR_EQUAL;
    }

    bool StartsWithOrdinalIgnoreCase(std::wstring_view text, std::wstring_view prefix) noexcept
    {
        return text.size() >= prefix.size() && EqualsOrdinalIgnoreCase(text.substr(0, prefix.size()), prefix);
    }
}
