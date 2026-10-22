#include "Core/CommandLineParsing.h"

#include "Core/OrdinalText.h"
#include "Core/SystemError.h"

#include <windows.h>
#include <shellapi.h>

#include <wil/resource.h>

#include <algorithm>
#include <array>

#pragma comment(lib, "shell32.lib")

namespace ClaudeSwitcher::Core
{
    namespace
    {
        using unique_argv = wil::unique_any<LPWSTR*, decltype(&::LocalFree), ::LocalFree>;

        // Order matters: "--" must be tried before "-".
        constexpr std::array<std::wstring_view, 3> SwitchPrefixes{ L"--", L"-", L"/" };

        std::optional<std::wstring_view> StripSwitchPrefix(std::wstring_view argument) noexcept
        {
            for (auto const prefix : SwitchPrefixes)
            {
                if (argument.size() > prefix.size() && argument.substr(0, prefix.size()) == prefix)
                {
                    return argument.substr(prefix.size());
                }
            }
            return std::nullopt;
        }
    }

    std::vector<std::wstring> SplitCommandLine(std::wstring_view commandLine)
    {
        // CommandLineToArgvW returns the current executable's path for an
        // empty string, which would be wrong for another process.
        if (commandLine.find_first_not_of(L" \t") == std::wstring_view::npos)
        {
            return {};
        }

        std::wstring const terminated{ commandLine };
        int count = 0;
        unique_argv argv{ CommandLineToArgvW(terminated.c_str(), &count) };
        if (!argv)
        {
            ThrowLastWin32Error("Splitting a command line");
        }

        std::vector<std::wstring> arguments;
        arguments.reserve(static_cast<std::size_t>(count));
        for (auto i = 0; i < count; ++i)
        {
            arguments.emplace_back(argv.get()[i]);
        }
        return arguments;
    }

    ChromiumSwitches ParseChromiumSwitches(std::vector<std::wstring> const& arguments)
    {
        ChromiumSwitches switches;
        for (std::size_t i = 1; i < arguments.size(); ++i)
        {
            std::wstring_view const argument = arguments[i];
            if (argument == L"--")
            {
                break;
            }

            auto const body = StripSwitchPrefix(argument);
            if (!body)
            {
                continue;
            }

            auto const equals = body->find(L'=');
            auto const name = body->substr(0, equals);
            auto const value = equals == std::wstring_view::npos ? std::wstring_view{} : body->substr(equals + 1);

            if (EqualsOrdinalIgnoreCase(name, L"type"))
            {
                switches.HasProcessType = true;
            }
            else if (EqualsOrdinalIgnoreCase(name, L"user-data-dir"))
            {
                // An empty value makes Chromium fall back to its default folder.
                switches.UserDataDir = value.empty() ? std::nullopt : std::optional<std::wstring>{ std::wstring{ value } };
            }
        }
        return switches;
    }

    std::wstring QuoteCommandLineArgument(std::wstring_view argument)
    {
        if (!argument.empty() && argument.find_first_of(L" \t\n\v\"") == std::wstring_view::npos)
        {
            return std::wstring{ argument };
        }

        // Backslashes are literal except directly before a quote, where each
        // one must be doubled; the closing quote counts as such a quote.
        std::wstring quoted{ L"\"" };
        std::size_t pendingBackslashes = 0;
        for (auto const c : argument)
        {
            if (c == L'\\')
            {
                ++pendingBackslashes;
                continue;
            }
            if (c == L'"')
            {
                quoted.append(pendingBackslashes * 2 + 1, L'\\');
            }
            else
            {
                quoted.append(pendingBackslashes, L'\\');
            }
            pendingBackslashes = 0;
            quoted.push_back(c);
        }
        quoted.append(pendingBackslashes * 2, L'\\');
        quoted.push_back(L'"');
        return quoted;
    }

    std::wstring NormalizePathForComparison(std::wstring_view path)
    {
        std::wstring normalized{ path };
        std::replace(normalized.begin(), normalized.end(), L'/', L'\\');

        constexpr std::wstring_view LongPathPrefix = L"\\\\?\\";
        constexpr std::wstring_view LongUncPrefix = L"\\\\?\\UNC\\";
        if (StartsWithOrdinalIgnoreCase(normalized, LongUncPrefix))
        {
            normalized = L"\\\\" + normalized.substr(LongUncPrefix.size());
        }
        else if (StartsWithOrdinalIgnoreCase(normalized, LongPathPrefix))
        {
            normalized.erase(0, LongPathPrefix.size());
        }

        // Keep the separator of a drive root ("C:\") so it stays absolute.
        while (normalized.size() > 3 && normalized.back() == L'\\')
        {
            normalized.pop_back();
        }
        return normalized;
    }

    bool PathsReferToSameLocation(std::wstring_view left, std::wstring_view right)
    {
        return EqualsOrdinalIgnoreCase(NormalizePathForComparison(left), NormalizePathForComparison(right));
    }
}
