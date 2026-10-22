#pragma once

#include <cstdint>
#include <string>

namespace ClaudeSwitcher::Core
{
    // Core reports Win32 and COM failures as std::system_error whose value is
    // the Win32 error code or HRESULT. DescribeSystemError turns either into
    // the localized text from the system message table.
    [[nodiscard]] std::wstring DescribeSystemError(std::int32_t code);

    [[noreturn]] void ThrowWin32Error(std::uint32_t error, char const* operation);
    [[noreturn]] void ThrowLastWin32Error(char const* operation);
    void ThrowIfFailed(std::int32_t hresult, char const* operation);
}
