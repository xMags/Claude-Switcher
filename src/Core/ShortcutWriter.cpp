#include "Core/ShortcutWriter.h"

#include "Core/KnownFolders.h"
#include "Core/SystemError.h"

#include <windows.h>
#include <shobjidl_core.h>
#include <shlguid.h>

#include <wil/com.h>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uuid.lib")

namespace ClaudeSwitcher::Core
{
    std::wstring SanitizeFileName(std::wstring_view name)
    {
        constexpr std::wstring_view Forbidden = L"<>:\"/\\|?*";
        std::wstring sanitized;
        sanitized.reserve(name.size());
        for (auto const c : name)
        {
            auto const replace = c < 0x20 || Forbidden.find(c) != std::wstring_view::npos;
            sanitized.push_back(replace ? L'_' : c);
        }
        for (auto i = sanitized.size(); i > 0 && (sanitized[i - 1] == L'.' || sanitized[i - 1] == L' '); --i)
        {
            sanitized[i - 1] = L'_';
        }
        return sanitized.empty() ? std::wstring{ L"_" } : sanitized;
    }

    std::filesystem::path DesktopShortcutPathFor(std::wstring_view profileName)
    {
        auto const fileName = L"Claude (" + SanitizeFileName(profileName) + L").lnk";
        return GetKnownFolderPath(KnownFolder::Desktop) / fileName;
    }

    void WriteShortcut(ShortcutSpec const& spec)
    {
        wil::com_ptr_nothrow<IShellLinkW> link;
        ThrowIfFailed(
            CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link)),
            "Creating the shortcut");
        ThrowIfFailed(link->SetPath(spec.Target.c_str()), "Setting the shortcut target");
        ThrowIfFailed(link->SetArguments(spec.Arguments.c_str()), "Setting the shortcut arguments");
        ThrowIfFailed(link->SetDescription(spec.Description.c_str()), "Setting the shortcut description");
        ThrowIfFailed(link->SetWorkingDirectory(spec.WorkingDirectory.c_str()), "Setting the shortcut folder");

        auto const file = link.try_query<IPersistFile>();
        if (!file)
        {
            ThrowIfFailed(E_NOINTERFACE, "Saving the shortcut");
        }
        ThrowIfFailed(file->Save(spec.Location.c_str(), TRUE), "Saving the shortcut");
    }

    std::optional<std::wstring> ReadShortcutArguments(std::filesystem::path const& location)
    {
        wil::com_ptr_nothrow<IShellLinkW> link;
        if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link))))
        {
            return std::nullopt;
        }
        auto const file = link.try_query<IPersistFile>();
        if (!file || FAILED(file->Load(location.c_str(), STGM_READ)))
        {
            return std::nullopt;
        }

        // INFOTIPSIZE (1024) is the documented upper bound for shortcut arguments.
        constexpr std::size_t MaxArguments = 1024;
        std::wstring arguments(MaxArguments, L'\0');
        if (FAILED(link->GetArguments(arguments.data(), static_cast<int>(arguments.size()))))
        {
            return std::nullopt;
        }
        arguments.resize(wcsnlen(arguments.c_str(), arguments.size()));
        return arguments;
    }
}
