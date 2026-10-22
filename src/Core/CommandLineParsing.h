#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ClaudeSwitcher::Core
{
    // Splits a full process command line (program name first) with the same
    // rules the C runtime and Chromium use.
    [[nodiscard]] std::vector<std::wstring> SplitCommandLine(std::wstring_view commandLine);

    struct ChromiumSwitches
    {
        // Chromium marks every helper process (renderer, GPU, crashpad, ...)
        // with --type=; the browser (main) process has none.
        bool HasProcessType = false;
        std::optional<std::wstring> UserDataDir;
    };

    // Reads the switches Claude Desktop's Chromium honours from argv
    // (argv[0] is the program and is skipped). Matches Chromium on Windows:
    // "--", "-" and "/" prefixes, case-insensitive names, "name=value" values,
    // the last occurrence wins, and a bare "--" ends switch parsing.
    [[nodiscard]] ChromiumSwitches ParseChromiumSwitches(std::vector<std::wstring> const& arguments);

    // Quotes one argument so SplitCommandLine returns it unchanged.
    [[nodiscard]] std::wstring QuoteCommandLineArgument(std::wstring_view argument);

    // Lexical comparison of two absolute local paths: separators unified,
    // "\\?\" and trailing separators dropped, case ignored. Does not touch
    // the file system, so it works for folders that do not exist yet.
    [[nodiscard]] std::wstring NormalizePathForComparison(std::wstring_view path);
    [[nodiscard]] bool PathsReferToSameLocation(std::wstring_view left, std::wstring_view right);
}
