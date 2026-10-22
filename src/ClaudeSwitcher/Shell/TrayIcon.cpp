#include "pch.h"
#include "Shell/TrayIcon.h"

#include <shellapi.h>

#include <stdexcept>

namespace ClaudeSwitcher::Shell
{
    namespace
    {
        constexpr UINT CallbackMessage = WM_APP + 1;
        constexpr UINT IconId = 1;
        constexpr UINT MenuOpen = 1;
        constexpr UINT MenuQuit = 2;
        // The executable's icon group (see ClaudeSwitcher.rc).
        constexpr WORD AppIconResource = 1;

        void RegisterWindowClassOnce(WNDPROC procedure)
        {
            WNDCLASSEXW windowClass{};
            windowClass.cbSize = sizeof(windowClass);
            windowClass.lpfnWndProc = procedure;
            windowClass.hInstance = GetModuleHandleW(nullptr);
            windowClass.lpszClassName = TrayIcon::WindowClass;
            if (RegisterClassExW(&windowClass) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            {
                throw std::runtime_error("The tray window class could not be registered.");
            }
        }
    }

    TrayIcon::TrayIcon(Callbacks callbacks)
        : m_callbacks(std::move(callbacks))
    {
        RegisterWindowClassOnce(&TrayIcon::WindowProc);
        m_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");

        // A hidden top-level window rather than a message-only one: only
        // top-level windows hear "TaskbarCreated" after Explorer restarts.
        m_window.reset(CreateWindowExW(
            WS_EX_TOOLWINDOW, WindowClass, L"", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, GetModuleHandleW(nullptr), this));
        if (!m_window)
        {
            throw std::runtime_error("The tray window could not be created.");
        }

        auto const dpi = GetDpiForSystem();
        m_icon.reset(static_cast<HICON>(LoadImageW(
            GetModuleHandleW(nullptr),
            MAKEINTRESOURCEW(AppIconResource),
            IMAGE_ICON,
            GetSystemMetricsForDpi(SM_CXSMICON, dpi),
            GetSystemMetricsForDpi(SM_CYSMICON, dpi),
            LR_DEFAULTCOLOR)));
    }

    TrayIcon::~TrayIcon()
    {
        RemoveIcon();
        // Detach before the window goes, so no message reaches a dead object.
        SetWindowLongPtrW(m_window.get(), GWLP_USERDATA, 0);
    }

    UINT TrayIcon::RestoreMessage() noexcept
    {
        static UINT const message = RegisterWindowMessageW(L"ClaudeSwitcher.Restore");
        return message;
    }

    void TrayIcon::SetVisible(bool visible)
    {
        if (visible == m_visible)
        {
            return;
        }
        m_visible = visible;
        if (visible)
        {
            AddIcon();
        }
        else
        {
            RemoveIcon();
        }
    }

    void TrayIcon::AddIcon()
    {
        NOTIFYICONDATAW data{};
        data.cbSize = sizeof(data);
        data.hWnd = m_window.get();
        data.uID = IconId;
        data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
        data.uCallbackMessage = CallbackMessage;
        data.hIcon = m_icon.get();
        wcscpy_s(data.szTip, L"Claude Switcher");
        if (!Shell_NotifyIconW(NIM_ADD, &data))
        {
            // Explorer may not be running yet at sign-in; TaskbarCreated retries.
            return;
        }
        data.uVersion = NOTIFYICON_VERSION_4;
        static_cast<void>(Shell_NotifyIconW(NIM_SETVERSION, &data));
    }

    void TrayIcon::RemoveIcon() noexcept
    {
        NOTIFYICONDATAW data{};
        data.cbSize = sizeof(data);
        data.hWnd = m_window.get();
        data.uID = IconId;
        static_cast<void>(Shell_NotifyIconW(NIM_DELETE, &data));
    }

    void TrayIcon::ShowMenu()
    {
        wil::unique_hmenu menu{ CreatePopupMenu() };
        if (!menu)
        {
            return;
        }
        AppendMenuW(menu.get(), MF_STRING, MenuOpen, L"Open Claude Switcher");
        AppendMenuW(menu.get(), MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu.get(), MF_STRING, MenuQuit, L"Quit");
        SetMenuDefaultItem(menu.get(), MenuOpen, FALSE);

        POINT cursor{};
        GetCursorPos(&cursor);
        // Without this the menu does not close when the user clicks elsewhere.
        SetForegroundWindow(m_window.get());
        auto const command = TrackPopupMenuEx(
            menu.get(), TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, cursor.x, cursor.y, m_window.get(), nullptr);
        PostMessageW(m_window.get(), WM_NULL, 0, 0);

        if (command == MenuOpen && m_callbacks.Open)
        {
            m_callbacks.Open();
        }
        else if (command == MenuQuit && m_callbacks.Quit)
        {
            m_callbacks.Quit();
        }
    }

    LRESULT TrayIcon::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam)
    {
        if (message == CallbackMessage)
        {
            // With NOTIFYICON_VERSION_4 the event is in the low word.
            switch (LOWORD(lParam))
            {
            case NIN_SELECT:
            case NIN_KEYSELECT:
                if (m_callbacks.Open)
                {
                    m_callbacks.Open();
                }
                return 0;
            case WM_CONTEXTMENU:
                ShowMenu();
                return 0;
            default:
                return 0;
            }
        }
        if (message == RestoreMessage())
        {
            if (m_callbacks.Open)
            {
                m_callbacks.Open();
            }
            return 0;
        }
        if (message == m_taskbarCreated && m_taskbarCreated != 0)
        {
            if (m_visible)
            {
                AddIcon();
            }
            return 0;
        }
        return DefWindowProcW(m_window.get(), message, wParam, lParam);
    }

    LRESULT CALLBACK TrayIcon::WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        if (message == WM_NCCREATE)
        {
            auto const create = reinterpret_cast<CREATESTRUCTW const*>(lParam); // Documented layout of lParam for WM_NCCREATE.
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        }

        auto const self = reinterpret_cast<TrayIcon*>(GetWindowLongPtrW(window, GWLP_USERDATA)); // Set above, cleared in the destructor.
        if (self == nullptr || !self->m_window)
        {
            return DefWindowProcW(window, message, wParam, lParam);
        }
        try
        {
            return self->HandleMessage(message, wParam, lParam);
        }
        catch (...)
        {
            // Exceptions must not cross back into Windows.
            return 0;
        }
    }
}
