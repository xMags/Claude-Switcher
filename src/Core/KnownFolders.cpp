#include "Core/KnownFolders.h"

#include "Core/SystemError.h"

#include <windows.h>
#include <shlobj_core.h>

#include <wil/resource.h>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

namespace ClaudeSwitcher::Core
{
    namespace
    {
        KNOWNFOLDERID const& ToFolderId(KnownFolder folder) noexcept
        {
            switch (folder)
            {
            case KnownFolder::LocalAppData:
                return FOLDERID_LocalAppData;
            case KnownFolder::LocalAppDataLow:
                return FOLDERID_LocalAppDataLow;
            case KnownFolder::RoamingAppData:
                return FOLDERID_RoamingAppData;
            case KnownFolder::UserProfile:
                return FOLDERID_Profile;
            case KnownFolder::Desktop:
                return FOLDERID_Desktop;
            }
            return FOLDERID_LocalAppData;
        }
    }

    std::filesystem::path GetKnownFolderPath(KnownFolder folder)
    {
        wil::unique_cotaskmem_string path;
        ThrowIfFailed(SHGetKnownFolderPath(ToFolderId(folder), KF_FLAG_DEFAULT, nullptr, &path), "Resolving a known folder");
        return std::filesystem::path{ path.get() };
    }
}
