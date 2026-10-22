#include "Core/ProfileStore.h"

#include "Core/OrdinalText.h"
#include "Core/ProfileValidation.h"
#include "Core/SystemError.h"

#include <windows.h>

#include <wil/resource.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>

#include <cmath>
#include <limits>
#include <optional>
#include <system_error>
#include <utility>

namespace ClaudeSwitcher::Core
{
    namespace
    {
        using winrt::Windows::Data::Json::JsonArray;
        using winrt::Windows::Data::Json::JsonObject;
        using winrt::Windows::Data::Json::JsonValue;
        using winrt::Windows::Data::Json::JsonValueType;

        // The list holds a handful of short records; anything near this size
        // is not a file this app wrote.
        constexpr DWORD MaxFileBytes = 1024 * 1024;
        constexpr std::size_t MaxProfiles = 100;
        constexpr std::size_t MaxIdLength = 64;
        constexpr std::size_t MaxRootLength = 1024;
        constexpr int MaxQuarantineAttempts = 100;

        std::optional<std::wstring> ReadString(JsonObject const& object, wchar_t const* key)
        {
            auto const value = object.TryLookup(key);
            if (!value || value.ValueType() != JsonValueType::String)
            {
                return std::nullopt;
            }
            return std::wstring{ value.GetString() };
        }

        std::optional<std::int64_t> ReadInteger(JsonObject const& object, wchar_t const* key)
        {
            auto const value = object.TryLookup(key);
            if (!value || value.ValueType() != JsonValueType::Number)
            {
                return std::nullopt;
            }
            auto const number = value.GetNumber();
            // Doubles hold integers exactly up to 2^53, far beyond any timestamp or version here.
            constexpr double Limit = 9007199254740992.0;
            if (!std::isfinite(number) || std::trunc(number) != number || number < -Limit || number > Limit)
            {
                return std::nullopt;
            }
            return static_cast<std::int64_t>(number);
        }

        bool HasControlCharacters(std::wstring_view text) noexcept
        {
            for (auto const c : text)
            {
                if (c < 0x20 || c == 0x7F)
                {
                    return true;
                }
            }
            return false;
        }

        std::optional<Profile> ParseProfile(JsonObject const& object)
        {
            auto id = ReadString(object, L"id");
            auto name = ReadString(object, L"name");
            auto note = ReadString(object, L"note");
            auto accent = ReadString(object, L"accentColor");
            auto folder = ReadString(object, L"folderName");
            auto const createdAt = ReadInteger(object, L"createdAt");
            if (!id || !name || !note || !accent || !folder || !createdAt)
            {
                return std::nullopt;
            }
            if (id->empty() || id->size() > MaxIdLength || HasControlCharacters(*id))
            {
                return std::nullopt;
            }
            // Stored names are already trimmed; a name that changes under
            // trimming was not written by this app.
            if (CheckProfileName(*name) != NameProblem::None || TrimWhitespace(*name) != *name)
            {
                return std::nullopt;
            }
            if (!IsValidProfileNote(*note) || !IsValidAccentColor(*accent) || !IsValidFolderName(*folder) || *createdAt < 0)
            {
                return std::nullopt;
            }

            Profile profile;
            profile.Id = std::move(*id);
            profile.Name = std::move(*name);
            profile.Note = std::move(*note);
            profile.AccentColor = std::move(*accent);
            profile.FolderName = std::move(*folder);
            profile.CreatedAtUnixSeconds = *createdAt;
            return profile;
        }

        bool HasDuplicates(std::vector<Profile> const& profiles)
        {
            for (std::size_t i = 0; i < profiles.size(); ++i)
            {
                for (std::size_t j = i + 1; j < profiles.size(); ++j)
                {
                    if (profiles[i].Id == profiles[j].Id ||
                        EqualsOrdinalIgnoreCase(profiles[i].Name, profiles[j].Name) ||
                        EqualsOrdinalIgnoreCase(profiles[i].FolderName, profiles[j].FolderName))
                    {
                        return true;
                    }
                }
            }
            return false;
        }

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

            auto const inputLength = static_cast<int>(bytes.size()); // Bounded by MaxFileBytes.
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
                ThrowWin32Error(ERROR_ARITHMETIC_OVERFLOW, "Encoding the profile list");
            }

            auto const inputLength = static_cast<int>(text.size());
            auto const required = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), inputLength, nullptr, 0, nullptr, nullptr);
            if (required <= 0)
            {
                ThrowLastWin32Error("Encoding the profile list");
            }
            std::string bytes(static_cast<std::size_t>(required), '\0');
            if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), inputLength, bytes.data(), required, nullptr, nullptr) != required)
            {
                ThrowLastWin32Error("Encoding the profile list");
            }
            return bytes;
        }

        enum class ReadOutcome
        {
            Read,
            Missing,
            TooLarge,
        };

        ReadOutcome ReadFileBytes(std::filesystem::path const& path, std::string& bytes)
        {
            wil::unique_hfile file{ CreateFileW(
                path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr) };
            if (!file)
            {
                auto const error = GetLastError();
                if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
                {
                    return ReadOutcome::Missing;
                }
                ThrowWin32Error(error, "Opening the profile list");
            }

            LARGE_INTEGER size{};
            if (!GetFileSizeEx(file.get(), &size))
            {
                ThrowLastWin32Error("Reading the profile list size");
            }
            if (size.QuadPart > MaxFileBytes)
            {
                return ReadOutcome::TooLarge;
            }

            bytes.assign(static_cast<std::size_t>(size.QuadPart), '\0');
            DWORD totalRead = 0;
            while (totalRead < bytes.size())
            {
                DWORD read = 0;
                if (!ReadFile(file.get(), bytes.data() + totalRead, static_cast<DWORD>(bytes.size()) - totalRead, &read, nullptr))
                {
                    ThrowLastWin32Error("Reading the profile list");
                }
                if (read == 0)
                {
                    break;
                }
                totalRead += read;
            }
            bytes.resize(totalRead);
            return ReadOutcome::Read;
        }

        std::filesystem::path QuarantinePathFor(std::filesystem::path const& file)
        {
            SYSTEMTIME now{};
            GetLocalTime(&now);
            wchar_t suffix[64]{};
            swprintf_s(
                suffix,
                L".corrupt-%04u%02u%02u-%02u%02u%02u-%03u",
                now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond, now.wMilliseconds);
            auto quarantine = file;
            quarantine += suffix;
            return quarantine;
        }
    }

    ParseResult ParseProfileDocument(std::wstring_view json)
    {
        ParseResult result;
        JsonObject root{ nullptr };
        if (!JsonObject::TryParse(winrt::hstring{ json }, root) || !root)
        {
            return result;
        }

        auto const version = ReadInteger(root, L"schemaVersion");
        if (!version || *version < 1)
        {
            return result;
        }
        if (*version > CurrentSchemaVersion)
        {
            result.Status = ParseStatus::NewerSchema;
            return result;
        }

        ProfileDocument document;
        if (auto const rootValue = root.TryLookup(L"profilesRoot"))
        {
            if (rootValue.ValueType() != JsonValueType::String)
            {
                return result;
            }
            document.ProfilesRoot = rootValue.GetString();
            if (document.ProfilesRoot.size() > MaxRootLength || HasControlCharacters(document.ProfilesRoot))
            {
                return result;
            }
        }
        if (auto const hintValue = root.TryLookup(L"signInHintDismissed"))
        {
            if (hintValue.ValueType() != JsonValueType::Boolean)
            {
                return result;
            }
            document.SignInHintDismissed = hintValue.GetBoolean();
        }

        auto const profilesValue = root.TryLookup(L"profiles");
        if (!profilesValue || profilesValue.ValueType() != JsonValueType::Array)
        {
            return result;
        }
        auto const profiles = profilesValue.GetArray();
        if (profiles.Size() > MaxProfiles)
        {
            return result;
        }
        for (auto const& entry : profiles)
        {
            if (entry.ValueType() != JsonValueType::Object)
            {
                return result;
            }
            auto profile = ParseProfile(entry.GetObject());
            if (!profile)
            {
                return result;
            }
            document.Profiles.push_back(std::move(*profile));
        }
        if (HasDuplicates(document.Profiles))
        {
            return result;
        }

        result.Status = ParseStatus::Parsed;
        result.Document = std::move(document);
        return result;
    }

    std::wstring SerializeProfileDocument(ProfileDocument const& document)
    {
        JsonArray profiles;
        for (auto const& profile : document.Profiles)
        {
            JsonObject entry;
            entry.Insert(L"id", JsonValue::CreateStringValue(profile.Id));
            entry.Insert(L"name", JsonValue::CreateStringValue(profile.Name));
            entry.Insert(L"note", JsonValue::CreateStringValue(profile.Note));
            entry.Insert(L"accentColor", JsonValue::CreateStringValue(profile.AccentColor));
            entry.Insert(L"folderName", JsonValue::CreateStringValue(profile.FolderName));
            entry.Insert(L"createdAt", JsonValue::CreateNumberValue(static_cast<double>(profile.CreatedAtUnixSeconds)));
            profiles.Append(entry);
        }

        JsonObject root;
        root.Insert(L"schemaVersion", JsonValue::CreateNumberValue(static_cast<double>(CurrentSchemaVersion)));
        root.Insert(L"profilesRoot", JsonValue::CreateStringValue(document.ProfilesRoot));
        root.Insert(L"signInHintDismissed", JsonValue::CreateBooleanValue(document.SignInHintDismissed));
        root.Insert(L"profiles", profiles);
        return std::wstring{ root.Stringify() };
    }

    ProfileStore::ProfileStore(std::filesystem::path filePath)
        : m_filePath(std::move(filePath))
    {
    }

    std::filesystem::path const& ProfileStore::FilePath() const noexcept
    {
        return m_filePath;
    }

    LoadResult ProfileStore::Load() const
    {
        LoadResult result;
        std::string bytes;
        auto const outcome = ReadFileBytes(m_filePath, bytes);
        if (outcome == ReadOutcome::Missing)
        {
            result.Status = LoadStatus::Missing;
            return result;
        }

        if (outcome == ReadOutcome::Read)
        {
            if (auto const text = DecodeUtf8(bytes))
            {
                auto parsed = ParseProfileDocument(*text);
                if (parsed.Status == ParseStatus::Parsed)
                {
                    result.Status = LoadStatus::Loaded;
                    result.Document = std::move(parsed.Document);
                    return result;
                }
                if (parsed.Status == ParseStatus::NewerSchema)
                {
                    result.Status = LoadStatus::NewerSchema;
                    return result;
                }
            }
        }

        // Moved aside rather than deleted, so nothing the user had is lost
        // and the next save cannot overwrite it. Never replaces an earlier
        // quarantined copy, even one made in the same millisecond.
        auto const stamped = QuarantinePathFor(m_filePath);
        auto quarantine = stamped;
        for (auto attempt = 1;; ++attempt)
        {
            if (MoveFileExW(m_filePath.c_str(), quarantine.c_str(), MOVEFILE_WRITE_THROUGH))
            {
                break;
            }
            auto const error = GetLastError();
            if (error != ERROR_ALREADY_EXISTS || attempt >= MaxQuarantineAttempts)
            {
                ThrowWin32Error(error, "Moving the unreadable profile list aside");
            }
            quarantine = stamped;
            quarantine += L"-" + std::to_wstring(attempt);
        }
        result.Status = LoadStatus::Quarantined;
        result.QuarantinedTo = std::move(quarantine);
        return result;
    }

    void ProfileStore::Save(ProfileDocument const& document) const
    {
        auto const bytes = EncodeUtf8(SerializeProfileDocument(document));
        if (bytes.size() > MaxFileBytes)
        {
            ThrowWin32Error(ERROR_FILE_TOO_LARGE, "Saving the profile list");
        }

        std::error_code directoryError;
        std::filesystem::create_directories(m_filePath.parent_path(), directoryError);
        if (directoryError)
        {
            throw std::system_error(directoryError, "Creating the settings folder");
        }

        auto temporary = m_filePath;
        temporary += L".tmp";

        // Declared before the handle so that on failure the handle closes
        // first and the half-written file can actually be deleted.
        auto removeTemporary = wil::scope_exit([&temporary] { DeleteFileW(temporary.c_str()); });
        wil::unique_hfile file{ CreateFileW(
            temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr) };
        if (!file)
        {
            ThrowLastWin32Error("Creating the profile list");
        }

        if (!bytes.empty())
        {
            DWORD written = 0;
            if (!WriteFile(file.get(), bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr))
            {
                ThrowLastWin32Error("Writing the profile list");
            }
            if (written != bytes.size())
            {
                ThrowWin32Error(ERROR_WRITE_FAULT, "Writing the profile list");
            }
        }
        if (!FlushFileBuffers(file.get()))
        {
            ThrowLastWin32Error("Flushing the profile list");
        }
        file.reset();

        if (!MoveFileExW(temporary.c_str(), m_filePath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            ThrowLastWin32Error("Replacing the profile list");
        }
        removeTemporary.release();
    }
}
