#pragma once

#include <windows.h>

#include <wil/resource.h>

#include <functional>

namespace ClaudeSwitcher::Shell
{
    // The notification-area icon, plus a hidden window that receives its
    // clicks and the "show yourself" request from a second app start.
    //
    // Threading: create, use and destroy on the UI thread; the callbacks run
    // there too, from inside the window procedure, so they must not destroy
    // this object directly (post the work instead).
    class TrayIcon
    {
    public:
        struct Callbacks
        {
            std::function<void()> Open;
            std::function<void()> Quit;
        };

        explicit TrayIcon(Callbacks callbacks);
        ~TrayIcon();

        TrayIcon(TrayIcon const&) = delete;
        TrayIcon& operator=(TrayIcon const&) = delete;

        // Adds or removes the icon; the hidden window exists either way.
        void SetVisible(bool visible);

        // Class of the hidden window, found by a second app start.
        static constexpr wchar_t WindowClass[] = L"ClaudeSwitcher.TrayWindow";
        // Posted to that window to bring the main window back.
        [[nodiscard]] static UINT RestoreMessage() noexcept;

    private:
        static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
        LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);
        void AddIcon();
        void RemoveIcon() noexcept;
        void ShowMenu();

        Callbacks m_callbacks;
        wil::unique_hwnd m_window;
        wil::unique_hicon m_icon;
        UINT m_taskbarCreated = 0;
        bool m_visible = false;
    };
}
