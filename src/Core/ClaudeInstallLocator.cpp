#include "Core/ClaudeInstallLocator.h"

#include "Core/KnownFolders.h"
#include "Core/SystemError.h"

#include <windows.h>
#include <appmodel.h>

#include <cwctype>
#include <tuple>
#include <vector>

namespace ClaudeSwitcher::Core
{
    namespace
    {
        constexpr wchar_t AliasFileName[] = L"claude-desktop.exe";

        struct PackageVersionInfo
        {
            PACKAGE_VERSION Version{};
            std::wstring Text;
        };

        std::optional<PackageVersionInfo> ReadPackageVersion(wchar_t const* fullName)
        {
            UINT32 length = 0;
            auto status = PackageIdFromFullName(fullName, PACKAGE_INFORMATION_BASIC, &length, nullptr);
            if (status != ERROR_INSUFFICIENT_BUFFER || length == 0)
            {
                return std::nullopt;
            }

            // PACKAGE_ID is followed by the strings it points at, so the
            // buffer must be allocated with suitable alignment.
            std::vector<PACKAGE_ID> buffer((length + sizeof(PACKAGE_ID) - 1) / sizeof(PACKAGE_ID));
            status = PackageIdFromFullName(
                fullName, PACKAGE_INFORMATION_BASIC, &length, reinterpret_cast<BYTE*>(buffer.data()));
            if (status != ERROR_SUCCESS)
            {
                return std::nullopt;
            }

            auto const& id = buffer.front();
            PackageVersionInfo info;
            info.Version = id.version;
            info.Text = std::to_wstring(id.version.Major) + L"." + std::to_wstring(id.version.Minor) + L"." +
                        std::to_wstring(id.version.Build) + L"." + std::to_wstring(id.version.Revision);
            return info;
        }

        std::optional<std::filesystem::path> ReadPackagePath(wchar_t const* fullName)
        {
            UINT32 length = 0;
            auto status = GetPackagePathByFullName(fullName, &length, nullptr);
            if (status != ERROR_INSUFFICIENT_BUFFER || length == 0)
            {
                return std::nullopt;
            }
            std::wstring path(length, L'\0');
            status = GetPackagePathByFullName(fullName, &length, path.data());
            if (status != ERROR_SUCCESS)
            {
                return std::nullopt;
            }
            path.resize(wcsnlen(path.c_str(), path.size()));
            return std::filesystem::path{ path };
        }
    }

    InstallLookup LocateClaudeInstallation()
    {
        std::wstring const family{ ClaudePackageFamilyName };
        UINT32 count = 0;
        UINT32 bufferLength = 0;
        auto status = GetPackagesByPackageFamily(family.c_str(), &count, nullptr, &bufferLength, nullptr);
        if (status == ERROR_SUCCESS && count == 0)
        {
            return {};
        }
        if (status != ERROR_INSUFFICIENT_BUFFER)
        {
            ThrowWin32Error(static_cast<std::uint32_t>(status), "Looking up the Claude Desktop package");
        }

        std::vector<PWSTR> names(count);
        std::vector<wchar_t> buffer(bufferLength);
        status = GetPackagesByPackageFamily(family.c_str(), &count, names.data(), &bufferLength, buffer.data());
        if (status != ERROR_SUCCESS)
        {
            ThrowWin32Error(static_cast<std::uint32_t>(status), "Looking up the Claude Desktop package");
        }

        std::optional<ClaudeInstallation> best;
        PACKAGE_VERSION bestVersion{};
        for (UINT32 i = 0; i < count; ++i)
        {
            auto const version = ReadPackageVersion(names[i]);
            auto const path = ReadPackagePath(names[i]);
            if (!version || !path)
            {
                continue;
            }
            auto const key = std::tie(version->Version.Major, version->Version.Minor, version->Version.Build, version->Version.Revision);
            auto const bestKey = std::tie(bestVersion.Major, bestVersion.Minor, bestVersion.Build, bestVersion.Revision);
            if (best && key <= bestKey)
            {
                continue;
            }

            ClaudeInstallation installation;
            installation.PackageFullName = names[i];
            installation.Version = version->Text;
            installation.InstallDirectory = *path;
            best = std::move(installation);
            bestVersion = version->Version;
        }

        if (!best)
        {
            return {};
        }

        InstallLookup lookup;
        best->AliasPath = GetKnownFolderPath(KnownFolder::LocalAppData) / L"Microsoft" / L"WindowsApps" / AliasFileName;
        // The alias is a reparse point; GetFileAttributesW sees it without
        // trying to follow it.
        lookup.Status = GetFileAttributesW(best->AliasPath.c_str()) == INVALID_FILE_ATTRIBUTES
                            ? InstallStatus::AliasMissing
                            : InstallStatus::Found;
        lookup.Installation = std::move(best);
        return lookup;
    }

    std::filesystem::path ClaudeExecutableIn(std::filesystem::path const& installDirectory)
    {
        return installDirectory / L"app" / L"Claude.exe";
    }

    std::filesystem::path DefaultClaudeDataDirectory()
    {
        // Claude names the folder after its package family in lower case.
        std::wstring folder{ ClaudePackageFamilyName };
        for (auto& c : folder)
        {
            c = static_cast<wchar_t>(std::towlower(c));
        }
        return GetKnownFolderPath(KnownFolder::LocalAppData) / L"Claude-Data" / folder;
    }
}
