#include "Core/AppSettings.h"
#include "Core/ClaudeLauncher.h"
#include "Core/StartupRegistration.h"
#include "Core/CommandLineParsing.h"
#include "Core/ProfileCatalog.h"
#include "Core/ProfileFolders.h"
#include "Core/ProfileStore.h"
#include "Core/ProfileValidation.h"
#include "Core/ShortcutWriter.h"

#include <windows.h>

#include <winrt/base.h>

#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace
{
    namespace core = ClaudeSwitcher::Core;

    void Require(bool condition, char const* message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    // A fresh folder under %TEMP% per test, removed afterwards.
    class TemporaryFolder
    {
    public:
        TemporaryFolder()
        {
            m_path = std::filesystem::temp_directory_path() /
                     (L"ClaudeSwitcherTests-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(++s_counter));
            std::filesystem::remove_all(m_path);
            std::filesystem::create_directories(m_path);
        }
        ~TemporaryFolder()
        {
            std::error_code ignored;
            std::filesystem::remove_all(m_path, ignored);
        }
        TemporaryFolder(TemporaryFolder const&) = delete;
        TemporaryFolder& operator=(TemporaryFolder const&) = delete;

        [[nodiscard]] std::filesystem::path const& Path() const noexcept { return m_path; }

    private:
        static inline int s_counter = 0;
        std::filesystem::path m_path;
    };

    void WriteBytes(std::filesystem::path const& path, std::string const& bytes)
    {
        std::ofstream stream{ path, std::ios::binary | std::ios::trunc };
        stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        Require(stream.good(), "test fixture could not be written");
    }

    std::string ReadBytes(std::filesystem::path const& path)
    {
        std::ifstream stream{ path, std::ios::binary };
        return std::string{ std::istreambuf_iterator<char>{ stream }, std::istreambuf_iterator<char>{} };
    }

    std::optional<std::wstring> UserDataDirOf(std::wstring const& commandLine)
    {
        return core::ParseChromiumSwitches(core::SplitCommandLine(commandLine)).UserDataDir;
    }

    void TestChromiumSwitchParsing()
    {
        // Command lines as Claude Desktop's processes actually report them.
        auto const main = core::ParseChromiumSwitches(core::SplitCommandLine(
            LR"("C:\Users\Dev\AppData\Local\Microsoft\WindowsApps\claude-desktop.exe" --user-data-dir="C:\Users\Dev\ClaudeProfiles\spike")"));
        Require(!main.HasProcessType, "the main process has no --type");
        Require(main.UserDataDir == LR"(C:\Users\Dev\ClaudeProfiles\spike)", "a quoted value is read without its quotes");

        auto const renderer = core::ParseChromiumSwitches(core::SplitCommandLine(
            LR"("C:\Program Files\WindowsApps\Claude_3.1623.3.0_x64__pzs8sxrjxfjjc\app\Claude.exe" --type=renderer --user-data-dir="C:\Users\Dev\ClaudeProfiles\spike" --lang=en-GB)"));
        Require(renderer.HasProcessType, "a renderer is recognised as a helper");

        Require(
            UserDataDirOf(LR"("C:\x\Claude.exe" --type=crashpad-handler --user-data-dir=C:\Users\Dev\ClaudeProfiles\spike /prefetch:4)") ==
                LR"(C:\Users\Dev\ClaudeProfiles\spike)",
            "an unquoted value ends at the next space");

        auto const defaultSession = core::ParseChromiumSwitches(core::SplitCommandLine(
            LR"("C:\Program Files\WindowsApps\Claude_3.1623.3.0_x64__pzs8sxrjxfjjc\app\Claude.exe")"));
        Require(!defaultSession.HasProcessType && !defaultSession.UserDataDir, "no switches means the default session");

        Require(UserDataDirOf(LR"(app.exe --USER-DATA-DIR=C:\a)") == LR"(C:\a)", "switch names are case-insensitive");
        Require(UserDataDirOf(LR"(app.exe -user-data-dir=C:\a)") == LR"(C:\a)", "a single dash is a switch prefix");
        Require(UserDataDirOf(LR"(app.exe /user-data-dir=C:\a)") == LR"(C:\a)", "a slash is a switch prefix");
        Require(UserDataDirOf(LR"(app.exe --user-data-dir=C:\a --user-data-dir=C:\b)") == LR"(C:\b)", "the last occurrence wins");
        Require(!UserDataDirOf(LR"(app.exe -- --user-data-dir=C:\a)"), "nothing after a bare -- is a switch");
        Require(!UserDataDirOf(LR"(app.exe --user-data-dir=)"), "an empty value means the default folder");
        Require(!UserDataDirOf(LR"(app.exe --user-data-dir)"), "a switch without a value means the default folder");
        Require(!UserDataDirOf(LR"(app.exe --user-data-directory=C:\a)"), "a longer switch name is a different switch");
        Require(!UserDataDirOf(LR"(app.exe claude://claude.ai/new)"), "a deep link is not a switch");
        Require(core::SplitCommandLine(L"").empty(), "an empty command line has no arguments");
        Require(core::SplitCommandLine(L"   ").empty(), "a blank command line has no arguments");
    }

    void TestArgumentQuoting()
    {
        std::vector<std::wstring> const samples{
            L"plain",
            L"",
            LR"(C:\Program Files\Claude)",
            LR"(C:\Users\A B\ClaudeProfiles\work\)",
            LR"(has "quotes" inside)",
            LR"(a\\"b)",
            LR"(trailing\\)",
            L"tab\there",
            LR"(--user-data-dir=C:\Users\A B\ClaudeProfiles\work\)",
        };
        for (auto const& sample : samples)
        {
            auto const arguments = core::SplitCommandLine(L"prog.exe " + core::QuoteCommandLineArgument(sample));
            Require(arguments.size() == 2, "a quoted argument stays one argument");
            Require(arguments[1] == sample, "a quoted argument round-trips unchanged");
        }
    }

    void TestLaunchArguments()
    {
        Require(core::BuildClaudeArguments(std::nullopt).empty(), "the default session starts without arguments");

        std::filesystem::path const folder{ LR"(C:\Users\Some One\ClaudeProfiles\work)" };
        auto const commandLine = LR"("C:\alias\claude-desktop.exe" )" + core::BuildClaudeArguments(folder);
        Require(UserDataDirOf(commandLine) == folder.wstring(), "the launch argument parses back to the profile folder");
    }

    void TestPathComparison()
    {
        auto const base = LR"(C:\Users\Dev\ClaudeProfiles\work)";
        Require(core::PathsReferToSameLocation(base, L"c:/users/dev/claudeprofiles/WORK/"), "case, slashes and a trailing separator are ignored");
        Require(core::PathsReferToSameLocation(base, LR"(\\?\C:\Users\Dev\ClaudeProfiles\work)"), "the long-path prefix is ignored");
        Require(!core::PathsReferToSameLocation(base, LR"(C:\Users\Dev\ClaudeProfiles\work-2)"), "a longer name is a different folder");
        Require(!core::PathsReferToSameLocation(base, LR"(C:\Users\Dev\ClaudeProfiles)"), "a parent is a different folder");
        Require(core::NormalizePathForComparison(LR"(C:\)") == LR"(C:\)", "a drive root keeps its separator");
        Require(
            core::NormalizePathForComparison(LR"(\\?\UNC\server\share\x)") == LR"(\\server\share\x)",
            "a long UNC path becomes a plain UNC path");
    }

    void TestNameAndColorRules()
    {
        Require(core::TrimWhitespace(L"  Work \t") == L"Work", "names are trimmed");
        Require(core::CheckProfileName(L"") == core::NameProblem::Empty, "an empty name is rejected");
        Require(core::CheckProfileName(std::wstring(41, L'a')) == core::NameProblem::TooLong, "a long name is rejected");
        Require(core::CheckProfileName(std::wstring(40, L'a')) == core::NameProblem::None, "a 40 character name is accepted");
        Require(core::CheckProfileName(L"Work\nTwo") == core::NameProblem::InvalidCharacters, "control characters are rejected");
        Require(core::CheckProfileName(L"Kunde · München") == core::NameProblem::None, "non-ASCII names are accepted");
        Require(core::IsValidProfileNote(L"") && core::IsValidProfileNote(L"me@example.com"), "notes are optional");
        Require(!core::IsValidProfileNote(std::wstring(81, L'a')), "a long note is rejected");

        Require(core::IsValidAccentColor(L"#D97757") && core::IsValidAccentColor(L"#d97757"), "hex colours are accepted");
        Require(!core::IsValidAccentColor(L"D97757") && !core::IsValidAccentColor(L"#GG0000") && !core::IsValidAccentColor(L"#FFF"),
                "malformed colours are rejected");
        Require(core::ParseAccentColor(L"#D97757") == 0xD97757u, "colours parse to 0xRRGGBB");
    }

    void TestFolderNames()
    {
        std::vector<std::wstring> const none;
        Require(core::MakeUniqueFolderName(L"Work", none) == L"work", "a simple name becomes a lower-case folder");
        Require(core::MakeUniqueFolderName(L"  Client A / B  ", none) == L"client-a-b", "runs of other characters become one dash");
        Require(core::MakeUniqueFolderName(L"日本語", none) == L"profile", "a name with no ASCII letters falls back");
        Require(core::MakeUniqueFolderName(L"CON", none) == L"con-profile", "reserved device names are avoided");
        Require(core::MakeUniqueFolderName(L"Work", { L"work", L"WORK-2" }) == L"work-3", "taken names are skipped case-insensitively");

        auto const longName = core::MakeUniqueFolderName(std::wstring(60, L'x'), none);
        Require(longName.size() <= 32, "the base folder name is capped");
        Require(core::IsValidFolderName(longName), "a capped name is still valid");

        std::vector<std::wstring> const derived{
            core::MakeUniqueFolderName(L"--Odd--Name--", none),
            core::MakeUniqueFolderName(L"a", none),
            core::MakeUniqueFolderName(L"nul", none),
            core::MakeUniqueFolderName(std::wstring(60, L'x'), { std::wstring(32, L'x') }),
        };
        for (auto const& name : derived)
        {
            Require(core::IsValidFolderName(name), "every derived folder name is valid");
        }

        Require(!core::IsValidFolderName(L""), "an empty folder name is invalid");
        Require(!core::IsValidFolderName(L"-a") && !core::IsValidFolderName(L"a-"), "edge dashes are invalid");
        Require(!core::IsValidFolderName(L"Work"), "upper case is invalid");
        Require(!core::IsValidFolderName(L"a_b") && !core::IsValidFolderName(L"..") && !core::IsValidFolderName(LR"(a\b)"),
                "separators and other characters are invalid");
        Require(!core::IsValidFolderName(L"com1") && !core::IsValidFolderName(L"aux"), "device names are invalid");
        Require(!core::IsValidFolderName(std::wstring(49, L'a')), "an overlong folder name is invalid");
    }

    void TestDocumentParsing()
    {
        core::ProfileDocument document;
        document.ProfilesRoot = LR"(D:\Claude)";
        document.SignInHintDismissed = true;
        document.Profiles.push_back({ L"{A}", L"Work", L"me@work.example", L"#3B82F6", L"work", 1761136000 });
        document.Profiles.push_back({ L"{B}", L"Kunde · München", L"", L"#D97757", L"kunde-m-nchen", 1761136001 });

        auto const parsed = core::ParseProfileDocument(core::SerializeProfileDocument(document));
        Require(parsed.Status == core::ParseStatus::Parsed, "a serialized document parses");
        Require(parsed.Document.ProfilesRoot == document.ProfilesRoot, "the root round-trips");
        Require(parsed.Document.SignInHintDismissed, "the hint flag round-trips");
        Require(parsed.Document.Profiles.size() == 2, "every profile round-trips");
        Require(parsed.Document.Profiles[1].Name == L"Kunde · München", "non-ASCII names round-trip");
        Require(parsed.Document.Profiles[0].CreatedAtUnixSeconds == 1761136000, "timestamps round-trip");

        auto const profileJson = [](std::wstring const& fields)
        {
            return LR"({"schemaVersion":1,"profiles":[{)" + fields + L"}]}";
        };
        std::wstring const good = LR"("id":"{A}","name":"Work","note":"","accentColor":"#3B82F6","folderName":"work","createdAt":1)";
        Require(core::ParseProfileDocument(profileJson(good)).Status == core::ParseStatus::Parsed, "a minimal document parses");
        Require(core::ParseProfileDocument(LR"({"schemaVersion":2,"profiles":"anything"})").Status == core::ParseStatus::NewerSchema,
                "a newer schema is reported rather than parsed");
        Require(core::ParseProfileDocument(L"not json").Status == core::ParseStatus::Invalid, "non-JSON is invalid");
        Require(core::ParseProfileDocument(LR"({"schemaVersion":1})").Status == core::ParseStatus::Invalid, "a missing list is invalid");
        Require(core::ParseProfileDocument(LR"({"schemaVersion":0,"profiles":[]})").Status == core::ParseStatus::Invalid, "schema 0 is invalid");
        Require(core::ParseProfileDocument(LR"({"schemaVersion":1.5,"profiles":[]})").Status == core::ParseStatus::Invalid, "a fractional schema is invalid");

        std::vector<std::wstring> const badProfiles{
            LR"("id":"{A}","name":"Work","note":"","accentColor":"#3B82F6","folderName":"..\\..\\Windows","createdAt":1)",
            LR"("id":"{A}","name":"Work","note":"","accentColor":"#3B82F6","folderName":"C:\\Windows","createdAt":1)",
            LR"("id":"{A}","name":" Work","note":"","accentColor":"#3B82F6","folderName":"work","createdAt":1)",
            LR"("id":"{A}","name":"Work","note":"","accentColor":"blue","folderName":"work","createdAt":1)",
            LR"("id":"","name":"Work","note":"","accentColor":"#3B82F6","folderName":"work","createdAt":1)",
            LR"("id":"{A}","name":"Work","note":"","accentColor":"#3B82F6","folderName":"work","createdAt":-1)",
            LR"("id":"{A}","name":"Work","note":"","accentColor":"#3B82F6","folderName":"work")",
            LR"("id":"{A}","name":7,"note":"","accentColor":"#3B82F6","folderName":"work","createdAt":1)",
        };
        for (auto const& fields : badProfiles)
        {
            Require(core::ParseProfileDocument(profileJson(fields)).Status == core::ParseStatus::Invalid, "a malformed profile invalidates the document");
        }

        auto const twoProfiles = [](std::wstring const& first, std::wstring const& second)
        {
            return LR"({"schemaVersion":1,"profiles":[{)" + first + L"},{" + second + L"}]}";
        };
        Require(core::ParseProfileDocument(twoProfiles(good, good)).Status == core::ParseStatus::Invalid, "duplicate ids are invalid");
        Require(core::ParseProfileDocument(twoProfiles(
                    good, LR"("id":"{B}","name":"WORK","note":"","accentColor":"#3B82F6","folderName":"work-2","createdAt":1)"))
                        .Status == core::ParseStatus::Invalid,
                "duplicate names are invalid");
        Require(core::ParseProfileDocument(twoProfiles(
                    good, LR"("id":"{B}","name":"Other","note":"","accentColor":"#3B82F6","folderName":"WORK","createdAt":1)"))
                        .Status == core::ParseStatus::Invalid,
                "two profiles sharing a folder are invalid");
    }

    void TestStoreFiles()
    {
        TemporaryFolder folder;
        auto const file = folder.Path() / L"settings" / L"profiles.json";
        core::ProfileStore store{ file };

        Require(store.Load().Status == core::LoadStatus::Missing, "a missing file loads as missing");

        core::ProfileDocument document;
        document.Profiles.push_back({ L"{A}", L"Work", L"", L"#3B82F6", L"work", 1 });
        store.Save(document);
        Require(std::filesystem::exists(file), "saving creates the settings folder and file");
        Require(!std::filesystem::exists(file.wstring() + L".tmp"), "no temporary file is left behind");
        auto const loaded = store.Load();
        Require(loaded.Status == core::LoadStatus::Loaded && loaded.Document.Profiles.size() == 1, "a saved list loads back");

        WriteBytes(file, "{ this is not json");
        auto const corrupt = store.Load();
        Require(corrupt.Status == core::LoadStatus::Quarantined, "a corrupt file is quarantined");
        Require(!std::filesystem::exists(file), "the corrupt file no longer blocks the name");
        Require(std::filesystem::exists(corrupt.QuarantinedTo), "the corrupt file is kept beside it");
        Require(ReadBytes(corrupt.QuarantinedTo) == "{ this is not json", "the quarantined file is unchanged");

        WriteBytes(file, "\xFF\xFE\x00garbage");
        Require(store.Load().Status == core::LoadStatus::Quarantined, "invalid UTF-8 is quarantined");

        std::string const newer = R"({"schemaVersion":99,"profiles":[]})";
        WriteBytes(file, newer);
        Require(store.Load().Status == core::LoadStatus::NewerSchema, "a newer file is reported");
        Require(ReadBytes(file) == newer, "a newer file is left untouched");

        WriteBytes(file, "\xEF\xBB\xBF" R"({"schemaVersion":1,"profiles":[]})");
        Require(store.Load().Status == core::LoadStatus::Loaded, "a UTF-8 byte order mark is accepted");
    }

    core::ProfileCatalog MakeCatalog(std::filesystem::path const& settings, std::filesystem::path const& root, core::RootProblem rootProblem = core::RootProblem::None)
    {
        return core::ProfileCatalog{
            core::ProfileStore{ settings },
            root,
            [rootProblem](std::filesystem::path const&) { return rootProblem; },
        };
    }

    void TestCatalog()
    {
        TemporaryFolder folder;
        auto const settings = folder.Path() / L"profiles.json";
        auto const root = folder.Path() / L"ClaudeProfiles";

        auto catalog = MakeCatalog(settings, root);
        Require(catalog.Load().Status == core::LoadStatus::Missing, "a first run starts empty");
        Require(!catalog.IsReadOnly(), "a first run is editable");

        auto const added = catalog.Add({ L"  Work  ", L" me@work.example ", L"#3b82f6" });
        Require(added.Error == core::ProfileEditError::None, "a valid profile is added");
        Require(added.Created.Name == L"Work" && added.Created.Note == L"me@work.example", "input is trimmed");
        Require(added.Created.AccentColor == L"#3B82F6", "colours are stored upper-case");
        Require(added.Created.FolderName == L"work", "the folder is named after the profile");
        Require(std::filesystem::is_directory(root / L"work"), "the data folder is created");
        Require(core::PathsReferToSameLocation(catalog.DataDirectoryFor(added.Created).wstring(), (root / L"work").wstring()),
                "the data folder sits directly in the root");

        Require(catalog.Add({ L"work", L"", L"#3B82F6" }).Error == core::ProfileEditError::NameTaken, "names are unique ignoring case");
        Require(catalog.Add({ L"default", L"", L"#3B82F6" }).Error == core::ProfileEditError::NameTaken, "Default is reserved");
        Require(catalog.Add({ L"   ", L"", L"#3B82F6" }).Error == core::ProfileEditError::NameEmpty, "a blank name is rejected");
        Require(catalog.Add({ L"Client", L"", L"red" }).Error == core::ProfileEditError::ColorInvalid, "a bad colour is rejected");
        Require(catalog.Profiles().size() == 1, "rejected input changes nothing");

        Require(catalog.Update(added.Created.Id, { L"Office", L"", L"#D97757" }) == core::ProfileEditError::None, "a profile can be renamed");
        Require(catalog.Find(added.Created.Id)->FolderName == L"work", "renaming keeps the folder");
        Require(catalog.Update(L"{missing}", { L"X", L"", L"#D97757" }) == core::ProfileEditError::NotFound, "updating an unknown id fails");
        Require(catalog.Update(added.Created.Id, { L"Office", L"", L"#D97757" }) == core::ProfileEditError::None,
                "a profile may keep its own name");

        auto reloaded = MakeCatalog(settings, root);
        static_cast<void>(reloaded.Load());
        Require(reloaded.Profiles().size() == 1 && reloaded.Profiles()[0].Name == L"Office", "changes are on disk");

        Require(catalog.Remove(added.Created.Id) == core::ProfileEditError::None, "a profile can be removed");
        Require(catalog.Profiles().empty(), "the removed profile is gone");
        Require(catalog.Remove(added.Created.Id) == core::ProfileEditError::NotFound, "removing twice fails");

        // The old folder is still on disk (the app recycles it separately),
        // so a new profile with the same name must not adopt it.
        auto const again = catalog.Add({ L"Work", L"", L"#3B82F6" });
        Require(again.Error == core::ProfileEditError::None && again.Created.FolderName == L"work-2",
                "a leftover folder is never reused");

        Require(!catalog.SignInHintDismissed(), "the hint starts visible");
        catalog.DismissSignInHint();
        auto hintReload = MakeCatalog(settings, root);
        static_cast<void>(hintReload.Load());
        Require(hintReload.SignInHintDismissed(), "dismissing the hint is saved");
    }

    void TestCatalogFailuresLeaveNothingBehind()
    {
        TemporaryFolder folder;
        auto const root = folder.Path() / L"ClaudeProfiles";

        // A directory appearing where the settings file should be makes every
        // later save fail (loading such a path is refused, so it comes after).
        auto const blocked = folder.Path() / L"blocked.json";
        auto catalog = MakeCatalog(blocked, root);
        Require(catalog.Load().Status == core::LoadStatus::Missing, "the blocked path starts missing");
        std::filesystem::create_directories(blocked);
        auto threw = false;
        try
        {
            static_cast<void>(catalog.Add({ L"Work", L"", L"#3B82F6" }));
        }
        catch (std::system_error const&)
        {
            threw = true;
        }
        Require(threw, "a failed save is reported");
        Require(catalog.Profiles().empty(), "a failed save changes nothing in memory");
        Require(!std::filesystem::exists(root / L"work"), "a failed save removes the folder it created");

        auto unusable = MakeCatalog(folder.Path() / L"ok.json", root, core::RootProblem::InsideAppData);
        static_cast<void>(unusable.Load());
        Require(unusable.Add({ L"Work", L"", L"#3B82F6" }).Error == core::ProfileEditError::ProfilesRootUnusable,
                "an unusable root blocks new profiles");

        auto const newer = folder.Path() / L"newer.json";
        WriteBytes(newer, R"({"schemaVersion":99})");
        auto readOnly = MakeCatalog(newer, root);
        Require(readOnly.Load().Status == core::LoadStatus::NewerSchema, "a newer list is detected");
        Require(readOnly.IsReadOnly(), "a newer list makes the catalog read-only");
        Require(readOnly.Add({ L"Work", L"", L"#3B82F6" }).Error == core::ProfileEditError::ReadOnly, "a read-only catalog refuses changes");
        readOnly.DismissSignInHint();
        Require(ReadBytes(newer) == R"({"schemaVersion":99})", "a read-only catalog never writes");
    }

    void TestRootRules()
    {
        std::vector<std::wstring> const appData{ LR"(C:\Users\Dev\AppData\Local)", LR"(C:\Users\Dev\AppData\Roaming)" };
        Require(core::ClassifyProfilesRoot(LR"(C:\Users\Dev\ClaudeProfiles)", appData) == core::RootProblem::None, "the default root is fine");
        Require(core::ClassifyProfilesRoot(LR"(D:\Claude)", appData) == core::RootProblem::None, "another drive is fine");
        Require(core::ClassifyProfilesRoot(LR"(C:\Users\Dev\AppData\Local\Claude)", appData) == core::RootProblem::InsideAppData, "AppData is refused");
        Require(core::ClassifyProfilesRoot(L"c:/users/dev/appdata/roaming", appData) == core::RootProblem::InsideAppData, "AppData itself is refused");
        Require(core::ClassifyProfilesRoot(LR"(C:\Users\Dev\AppData\Local\..\..\ClaudeProfiles)", appData) == core::RootProblem::None,
                "dot-dot segments are resolved before checking");
        Require(core::ClassifyProfilesRoot(LR"(C:\Users\Dev\AppData\LocalLow)", appData) == core::RootProblem::None,
                "a sibling with a longer name is not inside");
        Require(core::ClassifyProfilesRoot(LR"(\\server\share\Claude)", appData) == core::RootProblem::NetworkPath, "UNC paths are refused");
        Require(core::ClassifyProfilesRoot(LR"(\\?\UNC\server\share)", appData) == core::RootProblem::NetworkPath, "long UNC paths are refused");
        Require(core::ClassifyProfilesRoot(LR"(relative\path)", appData) == core::RootProblem::NotAbsolute, "relative paths are refused");
        Require(core::ClassifyProfilesRoot(L"C:relative", appData) == core::RootProblem::NotAbsolute, "drive-relative paths are refused");

        std::filesystem::path const root{ LR"(C:\Users\Dev\ClaudeProfiles)" };
        Require(core::IsDirectChildOf(root / L"work", root), "a profile folder is a direct child");
        Require(!core::IsDirectChildOf(root / L"a" / L"b", root), "a grandchild is not");
        Require(!core::IsDirectChildOf(root, root), "the root is not its own child");
        Require(!core::IsDirectChildOf(root / L".." / L"work", root), "dot-dot escapes are not children");
    }

    void TestShortcutNames()
    {
        Require(core::SanitizeFileName(L"Work: A/B?") == L"Work_ A_B_", "forbidden characters are replaced");
        Require(core::SanitizeFileName(L"trailing. ") == L"trailing__", "trailing dots and spaces are replaced");
        Require(core::SanitizeFileName(L"Kunde · München") == L"Kunde · München", "ordinary names are kept");
        Require(core::ShortcutFileNameFor(L"Work: A") == L"Claude (Work_ A).lnk", "shortcut names carry the profile name");
        Require(core::ShortcutFolderFor(core::ShortcutLocation::StartMenu).filename() == L"Claude Profiles",
                "Start menu shortcuts share one folder");
    }

    void TestShortcutFiles()
    {
        TemporaryFolder folder;
        core::ShortcutSpec spec;
        spec.Location = folder.Path() / L"nested" / core::ShortcutFileNameFor(L"Work");
        spec.Target = LR"(C:\Windows\System32\notepad.exe)";
        spec.Arguments = core::BuildClaudeArguments(std::filesystem::path{ LR"(C:\Users\Some One\ClaudeProfiles\work)" });
        spec.Description = L"test";
        spec.WorkingDirectory = folder.Path();
        core::WriteShortcut(spec);

        Require(std::filesystem::exists(spec.Location), "a shortcut is written, creating its folder");
        Require(core::ReadShortcutArguments(spec.Location) == spec.Arguments, "shortcut arguments read back exactly");
        Require(!core::ReadShortcutArguments(folder.Path() / L"missing.lnk"), "a missing shortcut has no arguments");

        auto const notShortcut = folder.Path() / L"plain.lnk";
        WriteBytes(notShortcut, "not a shortcut");
        Require(!core::ReadShortcutArguments(notShortcut), "a file that is not a shortcut has no arguments");
    }

    void TestAppSettings()
    {
        auto const defaults = core::ParseAppSettings(L"not json");
        Require(defaults.Theme == core::ThemePreference::System && !defaults.MinimizeToTray, "garbage falls back to defaults");

        auto const parsed = core::ParseAppSettings(LR"({"theme":"dark","minimizeToTray":true})");
        Require(parsed.Theme == core::ThemePreference::Dark && parsed.MinimizeToTray, "known values are read");
        Require(core::ParseAppSettings(LR"({"theme":"purple"})").Theme == core::ThemePreference::System, "an unknown theme means system");
        Require(!core::ParseAppSettings(LR"({"minimizeToTray":"yes"})").MinimizeToTray, "a wrongly typed value is ignored");

        for (auto const theme : { core::ThemePreference::System, core::ThemePreference::Light, core::ThemePreference::Dark })
        {
            core::AppSettings settings;
            settings.Theme = theme;
            settings.MinimizeToTray = theme == core::ThemePreference::Light;
            auto const back = core::ParseAppSettings(core::SerializeAppSettings(settings));
            Require(back.Theme == settings.Theme && back.MinimizeToTray == settings.MinimizeToTray, "settings round-trip");
        }

        TemporaryFolder folder;
        core::AppSettingsStore store{ folder.Path() / L"sub" / L"settings.json" };
        Require(store.Load().Theme == core::ThemePreference::System, "a missing file loads defaults");
        core::AppSettings saved;
        saved.Theme = core::ThemePreference::Light;
        saved.MinimizeToTray = true;
        store.Save(saved);
        auto const loaded = store.Load();
        Require(loaded.Theme == core::ThemePreference::Light && loaded.MinimizeToTray, "saved settings load back");
        WriteBytes(folder.Path() / L"sub" / L"settings.json", "\xFF\xFE garbage");
        Require(store.Load().Theme == core::ThemePreference::System, "an unreadable file loads defaults");
    }

    void TestStartupRegistration()
    {
        auto const commandLine = core::BuildStartupCommand(LR"(C:\Program Files\Claude Switcher\ClaudeSwitcher.exe)");
        auto const arguments = core::SplitCommandLine(commandLine);
        Require(arguments.size() == 2 && arguments[0] == LR"(C:\Program Files\Claude Switcher\ClaudeSwitcher.exe)" &&
                    arguments[1] == core::StartupSwitch,
                "the startup command quotes the path and adds the switch");

        // A scratch key in HKCU so the real Run key is never touched.
        auto const root = L"Software\\ClaudeSwitcherTests-" + std::to_wstring(GetCurrentProcessId());
        auto const cleanup = [&root] { RegDeleteTreeW(HKEY_CURRENT_USER, root.c_str()); };
        cleanup();
        core::StartupRegistration registration{ root + L"\\Run", root + L"\\Approved", L"ClaudeSwitcher" };
        try
        {
            Require(!registration.IsEnabled() && !registration.RegisteredCommand(), "nothing is registered at first");
            registration.Disable();
            Require(!registration.IsEnabled(), "disabling when absent is harmless");

            registration.Enable(commandLine);
            Require(registration.IsEnabled(), "enabling registers");
            Require(registration.RegisteredCommand() == commandLine, "the command is stored exactly");

            // What Windows writes when the app is switched off in Settings > Apps > Startup.
            BYTE disabled[12]{ 0x03 };
            Require(RegSetKeyValueW(HKEY_CURRENT_USER, (root + L"\\Approved").c_str(), L"ClaudeSwitcher", REG_BINARY, disabled, sizeof(disabled)) ==
                        ERROR_SUCCESS,
                    "test fixture written");
            Require(!registration.IsEnabled(), "a switch-off in Windows settings is respected");
            registration.Enable(commandLine);
            Require(registration.IsEnabled(), "enabling again clears the switch-off");

            registration.Disable();
            Require(!registration.IsEnabled() && !registration.RegisteredCommand(), "disabling removes the entry");
        }
        catch (...)
        {
            cleanup();
            throw;
        }
        cleanup();
    }

    struct TestCase
    {
        char const* Name;
        std::function<void()> Run;
    };
}

int wmain()
{
    winrt::init_apartment(winrt::apartment_type::single_threaded);

    std::vector<TestCase> const tests{
        { "Chromium switch parsing", TestChromiumSwitchParsing },
        { "argument quoting", TestArgumentQuoting },
        { "launch arguments", TestLaunchArguments },
        { "path comparison", TestPathComparison },
        { "name and colour rules", TestNameAndColorRules },
        { "folder names", TestFolderNames },
        { "document parsing", TestDocumentParsing },
        { "store files", TestStoreFiles },
        { "catalog", TestCatalog },
        { "catalog failures", TestCatalogFailuresLeaveNothingBehind },
        { "profiles root rules", TestRootRules },
        { "shortcut names", TestShortcutNames },
        { "shortcut files", TestShortcutFiles },
        { "app settings", TestAppSettings },
        { "startup registration", TestStartupRegistration },
    };

    auto failures = 0;
    for (auto const& test : tests)
    {
        try
        {
            test.Run();
            std::cout << "PASS  " << test.Name << '\n';
        }
        catch (std::exception const& error)
        {
            ++failures;
            std::cout << "FAIL  " << test.Name << ": " << error.what() << '\n';
        }
        catch (winrt::hresult_error const& error)
        {
            ++failures;
            std::cout << "FAIL  " << test.Name << ": HRESULT 0x" << std::hex << static_cast<unsigned int>(error.code()) << std::dec << '\n';
        }
    }

    std::cout << (tests.size() - static_cast<std::size_t>(failures)) << " of " << tests.size() << " test groups passed\n";
    return failures == 0 ? 0 : 1;
}
