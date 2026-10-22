#include "Core/AppSettings.h"

#include "Core/KnownFolders.h"
#include "Core/Utf8File.h"

#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>

#include <cstdint>
#include <utility>

namespace ClaudeSwitcher::Core
{
    namespace
    {
        using winrt::Windows::Data::Json::JsonObject;
        using winrt::Windows::Data::Json::JsonValue;
        using winrt::Windows::Data::Json::JsonValueType;

        constexpr std::uint32_t MaxFileBytes = 64 * 1024;

        constexpr std::wstring_view ThemeName(ThemePreference theme) noexcept
        {
            switch (theme)
            {
            case ThemePreference::Light:
                return L"light";
            case ThemePreference::Dark:
                return L"dark";
            case ThemePreference::System:
                break;
            }
            return L"system";
        }
    }

    AppSettings ParseAppSettings(std::wstring_view json)
    {
        AppSettings settings;
        JsonObject root{ nullptr };
        if (!JsonObject::TryParse(winrt::hstring{ json }, root) || !root)
        {
            return settings;
        }

        if (auto const theme = root.TryLookup(L"theme"); theme && theme.ValueType() == JsonValueType::String)
        {
            auto const name = theme.GetString();
            if (name == ThemeName(ThemePreference::Light))
            {
                settings.Theme = ThemePreference::Light;
            }
            else if (name == ThemeName(ThemePreference::Dark))
            {
                settings.Theme = ThemePreference::Dark;
            }
        }
        // "minimizeToTray" is the name this setting had in the first build.
        for (auto const key : { L"closeToTray", L"minimizeToTray" })
        {
            if (auto const tray = root.TryLookup(key); tray && tray.ValueType() == JsonValueType::Boolean)
            {
                settings.CloseToTray = tray.GetBoolean();
                break;
            }
        }
        return settings;
    }

    std::wstring SerializeAppSettings(AppSettings const& settings)
    {
        JsonObject root;
        root.Insert(L"schemaVersion", JsonValue::CreateNumberValue(1));
        root.Insert(L"theme", JsonValue::CreateStringValue(ThemeName(settings.Theme)));
        root.Insert(L"closeToTray", JsonValue::CreateBooleanValue(settings.CloseToTray));
        return std::wstring{ root.Stringify() };
    }

    std::filesystem::path AppSettingsFile()
    {
        return GetKnownFolderPath(KnownFolder::LocalAppData) / L"ClaudeSwitcher" / L"settings.json";
    }

    AppSettingsStore::AppSettingsStore(std::filesystem::path filePath)
        : m_filePath(std::move(filePath))
    {
    }

    AppSettings AppSettingsStore::Load() const noexcept
    {
        try
        {
            auto const file = ReadUtf8File(m_filePath, MaxFileBytes);
            return file.Status == Utf8ReadStatus::Read ? ParseAppSettings(file.Text) : AppSettings{};
        }
        catch (...)
        {
            return {};
        }
    }

    void AppSettingsStore::Save(AppSettings const& settings) const
    {
        WriteUtf8FileAtomically(m_filePath, SerializeAppSettings(settings), MaxFileBytes);
    }
}
