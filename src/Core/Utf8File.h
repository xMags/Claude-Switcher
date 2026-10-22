#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace ClaudeSwitcher::Core
{
    enum class Utf8ReadStatus
    {
        Read,
        Missing,
        // Larger than the caller's limit, or not valid UTF-8.
        Unreadable,
    };

    struct Utf8ReadResult
    {
        Utf8ReadStatus Status = Utf8ReadStatus::Missing;
        std::wstring Text;
    };

    // Reads a small UTF-8 text file (a leading byte order mark is skipped).
    // Throws std::system_error when the file exists but cannot be opened or read.
    [[nodiscard]] Utf8ReadResult ReadUtf8File(std::filesystem::path const& path, std::uint32_t maxBytes);

    // Writes text as UTF-8 to a temporary file and atomically replaces path
    // with it, creating the parent folder if needed. A crash mid-write leaves
    // either the old or the new file, never half of one. Throws std::system_error.
    void WriteUtf8FileAtomically(std::filesystem::path const& path, std::wstring_view text, std::uint32_t maxBytes);
}
