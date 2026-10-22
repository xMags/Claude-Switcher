#include "pch.h"
#include "ViewModels/SettingsViewModel.h"
#if __has_include("SettingsViewModel.g.cpp")
#include "SettingsViewModel.g.cpp"
#endif

#include "Core/SystemError.h"

#include <system_error>
#include <utility>

namespace winrt::ClaudeSwitcher::implementation
{
    namespace
    {
        namespace core = ::ClaudeSwitcher::Core;

        winrt::hstring Describe(std::system_error const& error)
        {
            return winrt::hstring{ core::DescribeSystemError(error.code().value()) };
        }
    }

    SettingsViewModel::SettingsViewModel(
        core::AppSettingsStore store,
        core::StartupRegistration startup,
        std::filesystem::path executable)
        : m_store(std::move(store)),
          m_startup(std::move(startup)),
          m_executable(std::move(executable)),
          m_settings(m_store.Load())
    {
        try
        {
            m_startWithWindows = m_startup.IsEnabled();
            // Re-point the entry if the app was moved since it was registered,
            // so sign-in does not try to start a file that is gone.
            auto const expected = core::BuildStartupCommand(m_executable);
            if (m_startWithWindows && m_startup.RegisteredCommand() != expected)
            {
                m_startup.Enable(expected);
            }
        }
        catch (std::system_error const& error)
        {
            ShowError(L"The startup setting couldn't be read: " + Describe(error));
        }
    }

    std::int32_t SettingsViewModel::ThemeIndex() const noexcept
    {
        return static_cast<std::int32_t>(m_settings.Theme);
    }

    void SettingsViewModel::ThemeIndex(std::int32_t value)
    {
        if (value < 0 || value > static_cast<std::int32_t>(core::ThemePreference::Dark) || value == ThemeIndex())
        {
            return;
        }
        m_settings.Theme = static_cast<core::ThemePreference>(value);
        Persist();
        Raise(L"ThemeIndex");
    }

    bool SettingsViewModel::CloseToTray() const noexcept
    {
        return m_settings.CloseToTray;
    }

    void SettingsViewModel::CloseToTray(bool value)
    {
        if (value == m_settings.CloseToTray)
        {
            return;
        }
        m_settings.CloseToTray = value;
        Persist();
        Raise(L"CloseToTray");
    }

    bool SettingsViewModel::StartWithWindows() const noexcept
    {
        return m_startWithWindows;
    }

    void SettingsViewModel::StartWithWindows(bool value)
    {
        if (value == m_startWithWindows)
        {
            return;
        }
        try
        {
            if (value)
            {
                m_startup.Enable(core::BuildStartupCommand(m_executable));
            }
            else
            {
                m_startup.Disable();
            }
            m_startWithWindows = value;
        }
        catch (std::system_error const& error)
        {
            ShowError(L"The startup setting couldn't be changed: " + Describe(error));
        }
        // Raised either way, so a failed change flips the switch back.
        Raise(L"StartWithWindows");
    }

    bool SettingsViewModel::IsErrorOpen() const noexcept { return m_errorOpen; }
    winrt::hstring SettingsViewModel::ErrorText() const { return m_errorText; }

    void SettingsViewModel::DismissError()
    {
        if (m_errorOpen)
        {
            m_errorOpen = false;
            Raise(L"IsErrorOpen");
        }
    }

    winrt::event_token SettingsViewModel::PropertyChanged(Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler)
    {
        return m_propertyChanged.add(handler);
    }

    void SettingsViewModel::PropertyChanged(winrt::event_token const& token) noexcept
    {
        m_propertyChanged.remove(token);
    }

    core::AppSettings const& SettingsViewModel::Current() const noexcept
    {
        return m_settings;
    }

    void SettingsViewModel::Persist()
    {
        try
        {
            m_store.Save(m_settings);
        }
        catch (std::system_error const& error)
        {
            // The change still applies for this session.
            ShowError(L"Settings couldn't be saved, so this change lasts only until the app closes: " + Describe(error));
        }
    }

    void SettingsViewModel::ShowError(winrt::hstring const& text)
    {
        m_errorText = text;
        m_errorOpen = true;
        Raise(L"ErrorText");
        Raise(L"IsErrorOpen");
    }

    void SettingsViewModel::Raise(wchar_t const* propertyName)
    {
        m_propertyChanged(*this, Microsoft::UI::Xaml::Data::PropertyChangedEventArgs{ propertyName });
    }
}
