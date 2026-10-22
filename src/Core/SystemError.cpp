#include "Core/SystemError.h"

#include <windows.h>

#include <system_error>

namespace ClaudeSwitcher::Core
{
    std::wstring DescribeSystemError(std::int32_t code)
    {
        wchar_t* buffer = nullptr;
        auto const length = FormatMessageW(
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            nullptr,
            static_cast<DWORD>(code),
            0,
            reinterpret_cast<wchar_t*>(&buffer), // FORMAT_MESSAGE_ALLOCATE_BUFFER takes the address of the pointer.
            0,
            nullptr);
        if (length == 0 || buffer == nullptr)
        {
            wchar_t fallback[32]{};
            swprintf_s(fallback, L"Error 0x%08X", static_cast<unsigned int>(code));
            return fallback;
        }

        std::wstring message{ buffer, length };
        LocalFree(buffer);
        while (!message.empty() && (message.back() == L'\r' || message.back() == L'\n' || message.back() == L' '))
        {
            message.pop_back();
        }
        return message;
    }

    void ThrowWin32Error(std::uint32_t error, char const* operation)
    {
        throw std::system_error(static_cast<int>(error), std::system_category(), operation);
    }

    void ThrowLastWin32Error(char const* operation)
    {
        ThrowWin32Error(GetLastError(), operation);
    }

    void ThrowIfFailed(std::int32_t hresult, char const* operation)
    {
        if (hresult < 0)
        {
            throw std::system_error(hresult, std::system_category(), operation);
        }
    }
}
