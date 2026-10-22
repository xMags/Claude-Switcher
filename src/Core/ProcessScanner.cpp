#include "Core/ProcessScanner.h"

#include "Core/ClaudeInstallLocator.h"
#include "Core/CommandLineParsing.h"
#include "Core/OrdinalText.h"
#include "Core/SystemError.h"

#include <windows.h>
#include <appmodel.h>
#include <tlhelp32.h>
#include <winternl.h>

#include <wil/resource.h>

#include <vector>

#pragma comment(lib, "ntdll.lib")

namespace ClaudeSwitcher::Core
{
    namespace
    {
        // PROCESSINFOCLASS value for the command line; available since
        // Windows 8.1 with only PROCESS_QUERY_LIMITED_INFORMATION access.
        constexpr auto ProcessCommandLineInformation = static_cast<PROCESSINFOCLASS>(60);
        constexpr NTSTATUS StatusInfoLengthMismatch = static_cast<NTSTATUS>(0xC0000004L);
        constexpr ULONG MaxCommandLineBytes = 256 * 1024;

        std::optional<std::wstring> ReadPackageString(
            HANDLE process,
            LONG(WINAPI* query)(HANDLE, UINT32*, PWSTR))
        {
            UINT32 length = 0;
            if (query(process, &length, nullptr) != ERROR_INSUFFICIENT_BUFFER || length == 0)
            {
                return std::nullopt;
            }
            std::wstring value(length, L'\0');
            if (query(process, &length, value.data()) != ERROR_SUCCESS)
            {
                return std::nullopt;
            }
            value.resize(wcsnlen(value.c_str(), value.size()));
            return value;
        }

        std::optional<std::wstring> ReadImagePath(HANDLE process)
        {
            std::wstring path(MAX_PATH, L'\0');
            for (auto attempt = 0; attempt < 4; ++attempt)
            {
                auto size = static_cast<DWORD>(path.size());
                if (QueryFullProcessImageNameW(process, 0, path.data(), &size))
                {
                    path.resize(size);
                    return path;
                }
                if (GetLastError() != ERROR_INSUFFICIENT_BUFFER)
                {
                    return std::nullopt;
                }
                path.resize(path.size() * 2);
            }
            return std::nullopt;
        }

        std::optional<std::wstring> ReadCommandLine(HANDLE process)
        {
            ULONG size = 4096;
            for (auto attempt = 0; attempt < 3; ++attempt)
            {
                // The result is a UNICODE_STRING header followed by its text,
                // so the buffer needs the header's alignment.
                std::vector<UNICODE_STRING> buffer((size + sizeof(UNICODE_STRING) - 1) / sizeof(UNICODE_STRING));
                ULONG returned = 0;
                auto const status = NtQueryInformationProcess(
                    process,
                    ProcessCommandLineInformation,
                    buffer.data(),
                    static_cast<ULONG>(buffer.size() * sizeof(UNICODE_STRING)),
                    &returned);
                if (status == StatusInfoLengthMismatch && returned > size && returned <= MaxCommandLineBytes)
                {
                    size = returned;
                    continue;
                }
                if (status < 0)
                {
                    return std::nullopt;
                }

                auto const& text = buffer.front();
                if (text.Buffer == nullptr || text.Length == 0)
                {
                    return std::wstring{};
                }
                return std::wstring{ text.Buffer, text.Length / sizeof(wchar_t) };
            }
            return std::nullopt;
        }

        std::optional<ClaudeInstance> InspectProcess(DWORD processId, std::wstring_view packageFamilyName)
        {
            wil::unique_handle process{ OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId) };
            if (!process)
            {
                return std::nullopt;
            }

            auto const family = ReadPackageString(process.get(), &GetPackageFamilyName);
            if (!family || !EqualsOrdinalIgnoreCase(*family, packageFamilyName))
            {
                return std::nullopt;
            }

            auto const fullName = ReadPackageString(process.get(), &GetPackageFullName);
            auto const imagePath = ReadImagePath(process.get());
            if (!fullName || !imagePath)
            {
                return std::nullopt;
            }

            UINT32 length = 0;
            if (GetPackagePathByFullName(fullName->c_str(), &length, nullptr) != ERROR_INSUFFICIENT_BUFFER || length == 0)
            {
                return std::nullopt;
            }
            std::wstring installDirectory(length, L'\0');
            if (GetPackagePathByFullName(fullName->c_str(), &length, installDirectory.data()) != ERROR_SUCCESS)
            {
                return std::nullopt;
            }
            installDirectory.resize(wcsnlen(installDirectory.c_str(), installDirectory.size()));
            if (!PathsReferToSameLocation(*imagePath, ClaudeExecutableIn(installDirectory).wstring()))
            {
                return std::nullopt;
            }

            auto const commandLine = ReadCommandLine(process.get());
            if (!commandLine)
            {
                return std::nullopt;
            }
            auto const switches = ParseChromiumSwitches(SplitCommandLine(*commandLine));
            if (switches.HasProcessType)
            {
                return std::nullopt;
            }

            ClaudeInstance instance;
            instance.ProcessId = processId;
            instance.UserDataDir = switches.UserDataDir;
            return instance;
        }
    }

    std::vector<ClaudeInstance> ScanClaudeInstances(std::wstring_view packageFamilyName)
    {
        // unique_hfile, because the failure value is INVALID_HANDLE_VALUE.
        wil::unique_hfile snapshot{ CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0) };
        if (!snapshot)
        {
            ThrowLastWin32Error("Listing running processes");
        }

        std::vector<ClaudeInstance> instances;
        PROCESSENTRY32W entry{};
        entry.dwSize = sizeof(entry);
        for (auto more = Process32FirstW(snapshot.get(), &entry); more; more = Process32NextW(snapshot.get(), &entry))
        {
            // Cheap filter before opening anything.
            if (!EqualsOrdinalIgnoreCase(entry.szExeFile, L"claude.exe"))
            {
                continue;
            }
            try
            {
                if (auto instance = InspectProcess(entry.th32ProcessID, packageFamilyName))
                {
                    instances.push_back(std::move(*instance));
                }
            }
            catch (std::exception const&)
            {
                // A process that exits mid-inspection is not an error.
            }
        }
        return instances;
    }
}
