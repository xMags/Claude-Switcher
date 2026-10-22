#include "Core/Utf8File.h"

#include "Core/SystemError.h"

#include <windows.h>

#include <wil/resource.h>

#include <limits>
#include <optional>
#include <system_error>

namespace ClaudeSwitcher::Core
{
    namespace
    {
        std::optional<std::wstring> DecodeUtf8(std::string_view bytes)
        {
            constexpr std::string_view Bom = "\xEF\xBB\xBF";
            if (bytes.substr(0, Bom.size()) == Bom)
            {
                bytes.remove_prefix(Bom.size());
            }
            if (bytes.empty())
            {
                return std::wstring{};
            }
            if (bytes.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
            {
                return std::nullopt;
            }

            auto const inputLength = static_cast<int>(bytes.size());
            auto const required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), inputLength, nullptr, 0);
            if (required <= 0)
            {
                return std::nullopt;
            }
            std::wstring text(static_cast<std::size_t>(required), L'\0');
            if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), inputLength, text.data(), required) != required)
            {
                return std::nullopt;
            }
            return text;
        }

        std::string EncodeUtf8(std::wstring_view text)
        {
            if (text.empty())
            {
                return {};
            }
            if (text.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
            {
                ThrowWin32Error(ERROR_ARITHMETIC_OVERFLOW, "Encoding a settings file");
            }

            auto const inputLength = static_cast<int>(text.size());
            auto const required = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), inputLength, nullptr, 0, nullptr, nullptr);
            if (required <= 0)
            {
                ThrowLastWin32Error("Encoding a settings file");
            }
            std::string bytes(static_cast<std::size_t>(required), '\0');
            if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), inputLength, bytes.data(), required, nullptr, nullptr) != required)
            {
                ThrowLastWin32Error("Encoding a settings file");
            }
            return bytes;
        }
    }

    Utf8ReadResult ReadUtf8File(std::filesystem::path const& path, std::uint32_t maxBytes)
    {
        Utf8ReadResult result;
        wil::unique_hfile file{ CreateFileW(
            path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr) };
        if (!file)
        {
            auto const error = GetLastError();
            if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
            {
                result.Status = Utf8ReadStatus::Missing;
                return result;
            }
            ThrowWin32Error(error, "Opening a settings file");
        }

        LARGE_INTEGER size{};
        if (!GetFileSizeEx(file.get(), &size))
        {
            ThrowLastWin32Error("Reading a settings file's size");
        }
        if (size.QuadPart > maxBytes)
        {
            result.Status = Utf8ReadStatus::Unreadable;
            return result;
        }

        std::string bytes(static_cast<std::size_t>(size.QuadPart), '\0');
        DWORD totalRead = 0;
        while (totalRead < bytes.size())
        {
            DWORD read = 0;
            if (!ReadFile(file.get(), bytes.data() + totalRead, static_cast<DWORD>(bytes.size()) - totalRead, &read, nullptr))
            {
                ThrowLastWin32Error("Reading a settings file");
            }
            if (read == 0)
            {
                break;
            }
            totalRead += read;
        }
        bytes.resize(totalRead);

        auto text = DecodeUtf8(bytes);
        if (!text)
        {
            result.Status = Utf8ReadStatus::Unreadable;
            return result;
        }
        result.Status = Utf8ReadStatus::Read;
        result.Text = std::move(*text);
        return result;
    }

    void WriteUtf8FileAtomically(std::filesystem::path const& path, std::wstring_view text, std::uint32_t maxBytes)
    {
        auto const bytes = EncodeUtf8(text);
        if (bytes.size() > maxBytes)
        {
            ThrowWin32Error(ERROR_FILE_TOO_LARGE, "Saving a settings file");
        }

        std::error_code directoryError;
        std::filesystem::create_directories(path.parent_path(), directoryError);
        if (directoryError)
        {
            throw std::system_error(directoryError, "Creating the settings folder");
        }

        auto temporary = path;
        temporary += L".tmp";

        // Declared before the handle so that on failure the handle closes
        // first and the half-written file can actually be deleted.
        auto removeTemporary = wil::scope_exit([&temporary] { DeleteFileW(temporary.c_str()); });
        wil::unique_hfile file{ CreateFileW(
            temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr) };
        if (!file)
        {
            ThrowLastWin32Error("Creating a settings file");
        }

        if (!bytes.empty())
        {
            DWORD written = 0;
            if (!WriteFile(file.get(), bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr))
            {
                ThrowLastWin32Error("Writing a settings file");
            }
            if (written != bytes.size())
            {
                ThrowWin32Error(ERROR_WRITE_FAULT, "Writing a settings file");
            }
        }
        if (!FlushFileBuffers(file.get()))
        {
            ThrowLastWin32Error("Flushing a settings file");
        }
        file.reset();

        if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            ThrowLastWin32Error("Replacing a settings file");
        }
        removeTemporary.release();
    }
}
