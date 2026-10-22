#include "Core/StartupRegistration.h"

#include "Core/CommandLineParsing.h"
#include "Core/SystemError.h"

#include <windows.h>

#include <utility>
#include <vector>

#pragma comment(lib, "advapi32.lib")

namespace ClaudeSwitcher::Core
{
    namespace
    {
        // Explorer writes 12 bytes; an odd first byte means "disabled".
        constexpr BYTE DisabledFlag = 0x01;
        constexpr DWORD MaxCommandBytes = 4096;

        void DeleteValueIfPresent(std::wstring const& key, std::wstring const& valueName, char const* operation)
        {
            auto const status = RegDeleteKeyValueW(HKEY_CURRENT_USER, key.c_str(), valueName.c_str());
            if (status != ERROR_SUCCESS && status != ERROR_FILE_NOT_FOUND && status != ERROR_PATH_NOT_FOUND)
            {
                ThrowWin32Error(static_cast<std::uint32_t>(status), operation);
            }
        }
    }

    std::wstring BuildStartupCommand(std::filesystem::path const& executable)
    {
        return QuoteCommandLineArgument(executable.wstring()) + L" " + std::wstring{ StartupSwitch };
    }

    StartupRegistration::StartupRegistration(std::wstring runKey, std::wstring approvedKey, std::wstring valueName)
        : m_runKey(std::move(runKey)),
          m_approvedKey(std::move(approvedKey)),
          m_valueName(std::move(valueName))
    {
    }

    StartupRegistration StartupRegistration::ForCurrentUser()
    {
        return StartupRegistration{
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run",
            L"ClaudeSwitcher",
        };
    }

    std::optional<std::wstring> StartupRegistration::RegisteredCommand() const
    {
        DWORD size = 0;
        auto status = RegGetValueW(HKEY_CURRENT_USER, m_runKey.c_str(), m_valueName.c_str(), RRF_RT_REG_SZ, nullptr, nullptr, &size);
        if (status != ERROR_SUCCESS || size == 0 || size > MaxCommandBytes)
        {
            return std::nullopt;
        }
        std::wstring command(size / sizeof(wchar_t), L'\0');
        status = RegGetValueW(HKEY_CURRENT_USER, m_runKey.c_str(), m_valueName.c_str(), RRF_RT_REG_SZ, nullptr, command.data(), &size);
        if (status != ERROR_SUCCESS)
        {
            return std::nullopt;
        }
        command.resize(wcsnlen(command.c_str(), command.size()));
        return command;
    }

    bool StartupRegistration::IsEnabled() const
    {
        if (!RegisteredCommand())
        {
            return false;
        }

        BYTE approved[16]{};
        DWORD size = sizeof(approved);
        auto const status = RegGetValueW(
            HKEY_CURRENT_USER, m_approvedKey.c_str(), m_valueName.c_str(), RRF_RT_REG_BINARY, nullptr, approved, &size);
        if (status != ERROR_SUCCESS || size == 0)
        {
            // No record means Windows has never been told to skip it.
            return true;
        }
        return (approved[0] & DisabledFlag) == 0;
    }

    void StartupRegistration::Enable(std::wstring const& command) const
    {
        auto const bytes = static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t));
        auto const status = RegSetKeyValueW(
            HKEY_CURRENT_USER, m_runKey.c_str(), m_valueName.c_str(), REG_SZ, command.c_str(), bytes);
        if (status != ERROR_SUCCESS)
        {
            ThrowWin32Error(static_cast<std::uint32_t>(status), "Registering Claude Switcher to start with Windows");
        }
        // A leftover "switched off" mark would keep Windows from running it.
        DeleteValueIfPresent(m_approvedKey, m_valueName, "Clearing the old startup setting");
    }

    void StartupRegistration::Disable() const
    {
        DeleteValueIfPresent(m_runKey, m_valueName, "Removing Claude Switcher from startup");
        DeleteValueIfPresent(m_approvedKey, m_valueName, "Clearing the startup setting");
    }
}
