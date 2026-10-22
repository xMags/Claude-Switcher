#pragma once

#include <windows.h>

#include <wil/resource.h>

namespace ClaudeSwitcher::Shell
{
    // Keeps a second copy of the switcher from running, so two windows can
    // never save conflicting versions of the profile list. Hold the guard for
    // the life of the process.
    class SingleInstanceGuard
    {
    public:
        // False when another instance in this sign-in session already holds it.
        [[nodiscard]] bool TryAcquire();

    private:
        wil::unique_handle m_mutex;
    };

    // Brings the window of the instance that holds the guard to the front.
    void ActivateExistingInstance() noexcept;

    // The window title the existing instance is found by.
    inline constexpr wchar_t MainWindowTitle[] = L"Claude Switcher";
}
