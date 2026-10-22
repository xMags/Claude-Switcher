#include "pch.h"
#include "Shell/SingleInstanceGuard.h"

namespace ClaudeSwitcher::Shell
{
    namespace
    {
        // Local\ scopes it to this Windows sign-in session.
        constexpr wchar_t MutexName[] = L"Local\\ClaudeSwitcher.SingleInstance.{6E0C8F4B-2F7A-4C1E-9B3D-5A8E1D2C7F40}";
        // Every unpackaged WinUI 3 window uses this class.
        constexpr wchar_t WinUIWindowClass[] = L"WinUIDesktopWin32WindowClass";
    }

    bool SingleInstanceGuard::TryAcquire()
    {
        m_mutex.reset(CreateMutexW(nullptr, FALSE, MutexName));
        if (!m_mutex)
        {
            // Without the mutex there is no way to tell; running beats refusing to start.
            return true;
        }
        return GetLastError() != ERROR_ALREADY_EXISTS;
    }

    void ActivateExistingInstance() noexcept
    {
        auto const window = FindWindowW(WinUIWindowClass, MainWindowTitle);
        if (window == nullptr)
        {
            return;
        }
        if (IsIconic(window))
        {
            ShowWindow(window, SW_RESTORE);
        }
        // Allowed because this process was just started by the user and so
        // still holds the foreground right it passes on here.
        SetForegroundWindow(window);
    }
}
