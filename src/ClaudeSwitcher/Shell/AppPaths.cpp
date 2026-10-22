#include "pch.h"
#include "Shell/AppPaths.h"

#include "Core/KnownFolders.h"

#include <string>
#include <system_error>

namespace ClaudeSwitcher::Shell
{
    std::filesystem::path ExecutableDirectory()
    {
        std::wstring path(MAX_PATH, L'\0');
        for (;;)
        {
            auto const length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
            if (length == 0)
            {
                return {};
            }
            if (length < path.size())
            {
                path.resize(length);
                return std::filesystem::path{ path }.parent_path();
            }
            path.resize(path.size() * 2);
        }
    }

    std::filesystem::path BundledAppIcon()
    {
        return ExecutableDirectory() / L"Assets" / L"AppIcon.ico";
    }

    std::filesystem::path EnsureShortcutIcon() noexcept
    {
        try
        {
            auto const source = BundledAppIcon();
            auto const target = ::ClaudeSwitcher::Core::GetKnownFolderPath(::ClaudeSwitcher::Core::KnownFolder::LocalAppData) /
                                L"ClaudeSwitcher" / L"ShortcutIcon.ico";
            std::error_code error;
            std::filesystem::create_directories(target.parent_path(), error);
            if (!error)
            {
                std::filesystem::copy_file(source, target, std::filesystem::copy_options::overwrite_existing, error);
            }
            // An earlier copy still serves if this refresh failed.
            return std::filesystem::exists(target, error) ? target : std::filesystem::path{};
        }
        catch (...)
        {
            return {};
        }
    }
}
