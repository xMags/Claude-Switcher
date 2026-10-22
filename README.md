# Claude Switcher

A native Windows app (C++/WinRT, WinUI 3) for running several Claude Desktop
accounts side by side. Each profile opens Claude Desktop with its own data
folder, so each one keeps its own sign-in, settings and MCP configuration.

## How it works

- Claude Desktop (the MSIX build, package family `Claude_pzs8sxrjxfjjc`) is
  started through its `claude-desktop.exe` app execution alias with
  `--user-data-dir=<profile folder>`. The alias keeps Claude's package identity.
- Claude holds one single-instance lock per data folder. Different profiles can
  therefore run at the same time, and opening a profile that is already
  running just brings its window forward.
- Profile folders live in `%USERPROFILE%\ClaudeProfiles\<name>`. They cannot be
  under AppData, because Windows redirects Claude's AppData writes into its
  package.
- The profile list is `%LOCALAPPDATA%\ClaudeSwitcher\profiles.json`.
- The **Default** card is your existing Claude data. The switcher never moves
  or changes it.
- Deleting a profile moves its folder to the Recycle Bin.
- Each profile's menu can create a desktop shortcut or a Start menu entry
  (`Start Menu\Programs\Claude Profiles`) that opens that profile directly,
  without the switcher. Renaming a profile renames these shortcuts, and
  deleting it removes them. Only shortcuts pointing at that profile's folder
  are touched.

## Settings

The gear at the bottom of the side panel opens Settings:

- **App theme:** follow Windows, light or dark.
- **Start with Windows:** a per-user entry under
  `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`, started with
  `--startup`. It honours the switch in Windows Settings > Apps > Startup, and
  follows the exe if it moves.
- **Minimize to tray:** minimizing hides the window in the notification area.
  Click the icon to bring it back, right-click it to quit. A sign-in start goes
  straight to the tray when this is on, otherwise it starts minimized.

Preferences are stored in `%LOCALAPPDATA%\ClaudeSwitcher\settings.json`,
separate from the profile list.

## Signing in to a new profile

Sign in with the **emailed code**. Browser sign-in links and Google sign-in
come back through the `claude://` protocol, and Windows always hands that to
the Default Claude window, not to the profile you are setting up.

## Building

Requirements: Visual Studio 2026 with the C++ desktop workload and the Windows
App SDK C++ component.

```powershell
$msbuild = "C:\Program Files\Microsoft Visual Studio\18\Professional\MSBuild\Current\Bin\amd64\MSBuild.exe"
& $msbuild ClaudeSwitcher.slnx -t:restore -p:RestorePackagesConfig=true
& $msbuild ClaudeSwitcher.slnx -p:Configuration=Release -p:Platform=x64
.\x64\Release\CoreTests.exe
.\x64\Release\ClaudeSwitcher\ClaudeSwitcher.exe
```

## Layout

- `src/Core`: profile storage and validation, Claude install lookup,
  launching, running-session detection, shortcuts and folder handling. No UI.
- `src/ClaudeSwitcher`: the WinUI 3 app (window, page, dialog, view models).
- `tests/CoreTests`: console tests for the Core logic.
