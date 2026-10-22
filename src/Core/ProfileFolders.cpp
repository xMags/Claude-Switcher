#include "Core/ProfileFolders.h"

#include "Core/CommandLineParsing.h"
#include "Core/KnownFolders.h"
#include "Core/OrdinalText.h"
#include "Core/SystemError.h"

#include <shellapi.h>
#include <shobjidl_core.h>
#include <shlobj_core.h>

#include <wil/com.h>

#include <system_error>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")

namespace ClaudeSwitcher::Core
{
    namespace
    {
        std::wstring NormalizeLexically(std::wstring_view path)
        {
            auto const normal = std::filesystem::path{ NormalizePathForComparison(path) }.lexically_normal();
            return NormalizePathForComparison(normal.wstring());
        }

        bool IsDriveAbsolute(std::wstring_view path) noexcept
        {
            return path.size() >= 3 &&
                   ((path[0] >= L'A' && path[0] <= L'Z') || (path[0] >= L'a' && path[0] <= L'z')) &&
                   path[1] == L':' && path[2] == L'\\';
        }
    }

    std::filesystem::path DefaultProfilesRoot()
    {
        return GetKnownFolderPath(KnownFolder::UserProfile) / L"ClaudeProfiles";
    }

    std::filesystem::path SwitcherSettingsFile()
    {
        return GetKnownFolderPath(KnownFolder::LocalAppData) / L"ClaudeSwitcher" / L"profiles.json";
    }

    RootProblem ClassifyProfilesRoot(std::wstring_view root, std::vector<std::wstring> const& appDataFolders)
    {
        auto const normalized = NormalizeLexically(root);
        if (StartsWithOrdinalIgnoreCase(normalized, L"\\\\"))
        {
            return RootProblem::NetworkPath;
        }
        if (!IsDriveAbsolute(normalized))
        {
            return RootProblem::NotAbsolute;
        }

        for (auto const& appData : appDataFolders)
        {
            auto const folder = NormalizeLexically(appData);
            if (folder.empty())
            {
                continue;
            }
            if (EqualsOrdinalIgnoreCase(normalized, folder) || StartsWithOrdinalIgnoreCase(normalized, folder + L"\\"))
            {
                return RootProblem::InsideAppData;
            }
        }
        return RootProblem::None;
    }

    RootProblem CheckProfilesRoot(std::filesystem::path const& root)
    {
        std::vector<std::wstring> const appDataFolders{
            GetKnownFolderPath(KnownFolder::LocalAppData).wstring(),
            GetKnownFolderPath(KnownFolder::LocalAppDataLow).wstring(),
            GetKnownFolderPath(KnownFolder::RoamingAppData).wstring(),
        };
        auto const lexical = ClassifyProfilesRoot(root.wstring(), appDataFolders);
        if (lexical != RootProblem::None)
        {
            return lexical;
        }

        auto const normalized = NormalizeLexically(root.wstring());
        std::wstring const drive{ normalized.substr(0, 3) };
        switch (GetDriveTypeW(drive.c_str()))
        {
        case DRIVE_REMOTE:
            return RootProblem::NetworkPath;
        case DRIVE_UNKNOWN:
        case DRIVE_NO_ROOT_DIR:
        case DRIVE_CDROM:
            return RootProblem::DriveUnavailable;
        default:
            return RootProblem::None;
        }
    }

    bool IsDirectChildOf(std::filesystem::path const& child, std::filesystem::path const& parent)
    {
        auto const normalizedChild = std::filesystem::path{ NormalizeLexically(child.wstring()) };
        if (!normalizedChild.has_filename() || normalizedChild.filename() == L"." || normalizedChild.filename() == L"..")
        {
            return false;
        }
        return PathsReferToSameLocation(normalizedChild.parent_path().wstring(), NormalizeLexically(parent.wstring()));
    }

    std::vector<std::wstring> ListFolderNames(std::filesystem::path const& root)
    {
        std::vector<std::wstring> names;
        std::error_code error;
        std::filesystem::directory_iterator iterator{ root, error };
        if (error)
        {
            if (error == std::errc::no_such_file_or_directory)
            {
                return names;
            }
            throw std::system_error(error, "Listing the profiles folder");
        }

        for (auto const& entry : iterator)
        {
            std::error_code typeError;
            if (entry.is_directory(typeError))
            {
                names.push_back(entry.path().filename().wstring());
            }
        }
        return names;
    }

    RecycleOutcome RecycleFolder(std::filesystem::path const& folder, HWND owner)
    {
        auto const attributes = GetFileAttributesW(folder.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES)
        {
            auto const error = GetLastError();
            if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
            {
                return RecycleOutcome::AlreadyGone;
            }
            ThrowWin32Error(error, "Checking the profile folder");
        }

        wil::com_ptr_nothrow<IFileOperation> operation;
        ThrowIfFailed(
            CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&operation)),
            "Preparing to move the profile folder");
        if (owner != nullptr)
        {
            ThrowIfFailed(operation->SetOwnerWindow(owner), "Preparing to move the profile folder");
        }
        // The user already confirmed in the app, so no second "are you sure",
        // but FOF_WANTNUKEWARNING still asks before anything is destroyed
        // instead of recycled.
        ThrowIfFailed(
            operation->SetOperationFlags(FOF_ALLOWUNDO | FOFX_RECYCLEONDELETE | FOF_NOCONFIRMATION | FOF_WANTNUKEWARNING),
            "Preparing to move the profile folder");

        wil::com_ptr_nothrow<IShellItem> item;
        ThrowIfFailed(
            SHCreateItemFromParsingName(folder.c_str(), nullptr, IID_PPV_ARGS(&item)),
            "Finding the profile folder");
        ThrowIfFailed(operation->DeleteItem(item.get(), nullptr), "Moving the profile folder to the Recycle Bin");

        auto const result = operation->PerformOperations();
        BOOL aborted = FALSE;
        static_cast<void>(operation->GetAnyOperationsAborted(&aborted));
        if (result == HRESULT_FROM_WIN32(ERROR_CANCELLED) || aborted)
        {
            return RecycleOutcome::Cancelled;
        }
        ThrowIfFailed(result, "Moving the profile folder to the Recycle Bin");
        return RecycleOutcome::Recycled;
    }
}
