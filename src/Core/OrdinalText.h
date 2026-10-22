#pragma once

#include <string_view>

namespace ClaudeSwitcher::Core
{
    // Case-insensitive comparison using the file system's ordinal rules, which
    // is what Windows itself uses to decide whether two names collide.
    [[nodiscard]] bool EqualsOrdinalIgnoreCase(std::wstring_view left, std::wstring_view right) noexcept;
    [[nodiscard]] bool StartsWithOrdinalIgnoreCase(std::wstring_view text, std::wstring_view prefix) noexcept;
}
