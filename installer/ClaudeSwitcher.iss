; Claude Switcher installer.
;
; Build it with tools\Build-Installer.ps1 rather than by opening this file in
; the Inno Setup IDE. That script builds Release, runs the tests and verifies
; the dependency closure first, so an installer can only come from an output
; that is actually self-contained.
;
; AppVersion is supplied by the build script via /DAppVersion from
; build\Version.props, so the version lives in one place.

#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif

#define AppName "Claude Switcher"
#define AppPublisher "Last Projects"
#define AppWebsite "https://github.com/xMags/Claude-Switcher"
#define AppExeName "ClaudeSwitcher.exe"
; Must match SetCurrentProcessExplicitAppUserModelID in App.xaml.cpp, so a
; pinned shortcut and the running window share one taskbar button.
#define AppUserModelId "ClaudeSwitcher.App"
#define RepositoryRoot ".."
#define SourceRoot "..\x64\Release\ClaudeSwitcher"

[Setup]
; Identifies this app to Windows across upgrades. Never change it: a new value
; orphans every existing install.
AppId={{f23711c1-8588-4ec6-8cbd-ad6e76afc7fa}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppWebsite}
AppSupportURL={#AppWebsite}/issues
AppUpdatesURL={#AppWebsite}/releases
VersionInfoVersion={#AppVersion}
VersionInfoCompany={#AppPublisher}
VersionInfoProductName={#AppName}

DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
DisableDirPage=auto

; All-users only, deliberately. A per-user install puts an unsigned executable
; in a user-writable folder and registers it without ever prompting, which is
; exactly what Defender's machine-learning heuristics flag as an unwanted
; installer. Program Files behind an elevation prompt removes that signal. It
; is not a substitute for signing.
PrivilegesRequired=admin

ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.17763

Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern

OutputDir=Output
OutputBaseFilename=ClaudeSwitcher-{#AppVersion}-Setup
SetupIconFile={#RepositoryRoot}\src\ClaudeSwitcher\Assets\AppIcon.ico
UninstallDisplayIcon={app}\{#AppExeName}
UninstallDisplayName={#AppName}
LicenseFile={#RepositoryRoot}\LICENSE

; A running copy is closed in PrepareToInstall below. Restart Manager stays on
; as a fallback for anything that still holds a file open.
CloseApplications=yes
CloseApplicationsFilter=*.exe
RestartApplications=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
; Build-only metadata is excluded. The .winmd files are WinRT metadata used at
; compile time; a compiled C++ app does not read them at run time. resources.pri
; is NOT excluded: XAML cannot resolve ms-appx:/// without it.
Source: "{#SourceRoot}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs; \
    Excludes: "*.pdb,*.lib,*.exp,*.winmd,*.ipdb,*.iobj,*.appxrecipe,AppxManifest.xml"

Source: "{#RepositoryRoot}\LICENSE"; DestDir: "{app}\licenses"; DestName: "LICENSE.txt"; Flags: ignoreversion
Source: "{#RepositoryRoot}\NOTICE.txt"; DestDir: "{app}\licenses"; Flags: ignoreversion
Source: "{#RepositoryRoot}\TERMS-AND-CONDITIONS.md"; DestDir: "{app}\licenses"; Flags: ignoreversion
Source: "{#RepositoryRoot}\THIRD-PARTY-NOTICES.txt"; DestDir: "{app}\licenses"; Flags: ignoreversion
Source: "{#RepositoryRoot}\licenses\third-party\*"; DestDir: "{app}\licenses\third-party"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#AppExeName}"; AppUserModelID: "{#AppUserModelId}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExeName}"; Tasks: desktopicon; AppUserModelID: "{#AppUserModelId}"

[Run]
; runasoriginaluser matters because setup is elevated: without it the app
; would inherit the administrator token, resolve %LOCALAPPDATA% for the
; elevating account, and keep its profile list somewhere the user's own
; session never reads.
Filename: "{app}\{#AppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(AppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent runasoriginaluser

[Code]
const
  RunKey = 'Software\Microsoft\Windows\CurrentVersion\Run';
  StartupApprovedKey = 'Software\Microsoft\Windows\CurrentVersion\Explorer\StartupApproved\Run';
  StartupValueName = 'ClaudeSwitcher';

// With "Close to tray" on, closing the window only hides it, so a polite close
// request cannot end it. The app saves every change the moment it is made, so
// ending it outright loses nothing. Only the copy installed in {app} is
// stopped; a copy running from anywhere else (a build folder) is left alone.
procedure StopInstalledCopy;
var
  ResultCode: Integer;
  Executable: String;
begin
  Executable := ExpandConstant('{app}\{#AppExeName}');
  if not FileExists(Executable) then
    Exit;
  Exec(ExpandConstant('{sys}\WindowsPowerShell\v1.0\powershell.exe'),
       '-NoProfile -NonInteractive -Command "Get-Process -Name ClaudeSwitcher -ErrorAction SilentlyContinue | ' +
       'Where-Object { $_.Path -eq ''' + Executable + ''' } | Stop-Process -Force"',
       '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  StopInstalledCopy;
  Result := '';
end;

function InitializeUninstall(): Boolean;
begin
  StopInstalledCopy;
  Result := True;
end;

// "Start with Windows" writes a per-user Run entry pointing at {app}. Left
// behind, every sign-in would try to start a program that no longer exists.
// It is removed only when it points at this install. The uninstaller is
// elevated, so this is the elevating account's hive, which is the right one
// whenever the person removing the app is the person who used it.
procedure RemoveStartupEntry;
var
  Command: String;
begin
  if not RegQueryStringValue(HKEY_CURRENT_USER, RunKey, StartupValueName, Command) then
    Exit;
  if Pos(Lowercase(ExpandConstant('{app}\{#AppExeName}')), Lowercase(Command)) = 0 then
    Exit;
  RegDeleteValue(HKEY_CURRENT_USER, RunKey, StartupValueName);
  RegDeleteValue(HKEY_CURRENT_USER, StartupApprovedKey, StartupValueName);
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  DataDirectory: String;
begin
  if CurUninstallStep <> usPostUninstall then
    Exit;

  RemoveStartupEntry;

  // A silent uninstall has nobody to ask, and deleting on an assumed answer
  // would lose the profile list, so silence always means keep.
  if UninstallSilent then
    Exit;

  DataDirectory := ExpandConstant('{localappdata}\ClaudeSwitcher');
  if not DirExists(DataDirectory) then
    Exit;

  // Profile data folders (with the Claude sign-ins in them) are never touched
  // here: they belong to Claude, and losing them would sign every account out.
  if MsgBox('Also remove Claude Switcher''s settings?' + #13#10 + #13#10 +
            DataDirectory + #13#10 + #13#10 +
            'This removes your profile list and preferences.' + #13#10 +
            'Your Claude profile folders, with their sign-ins, are kept either way in:' + #13#10 +
            ExpandConstant('{%USERPROFILE}\ClaudeProfiles') + #13#10 + #13#10 +
            'Choose No to keep everything for a future reinstall.',
            mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES then
  begin
    DelTree(DataDirectory, True, True, True);
  end;
end;
