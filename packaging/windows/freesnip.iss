; Inno Setup script for the Windows installer, FreeSnip-<version>-windows-x64-setup.exe.
; Build it with tools\build-installer.ps1 after tools\build-windows.ps1. It is written to
; build\installer; tools\collect-release.ps1 copies it to dist\release with the other downloads.
;
; Installs for the current user (no administrator rights) into %LOCALAPPDATA%\Programs\FreeSnip.
; A running FreeSnip (it stays in the tray) is closed first and started again at the end.
;
; FreeSnip used to be called Snip (snip.exe). The AppId is unchanged, so this
; upgrades a Snip installation in place (same folder): its snip.exe and shortcuts are
; removed, and its "Snip" sign-in entry becomes FreeSnip's.

#define Root AddBackslash(SourcePath) + "..\.."
#define AppExe Root + "\dist\freesnip.exe"
#if !FileExists(AppExe)
  #error dist\freesnip.exe is missing: run tools\build-windows.ps1 first
#endif
#define AppVersion GetStringFileInfo(AppExe, "ProductVersion")

[Setup]
AppId={{D16B3DB7-B36B-4851-A731-9DABA54A269C}
AppName=FreeSnip
AppVersion={#AppVersion}
AppVerName=FreeSnip {#AppVersion}
AppPublisher=Tim Cottrill
AppCopyright=Copyright (C) 2026 Tim Cottrill
AppComments=Grab it. Clip it. Done.
DefaultDirName={autopf}\FreeSnip
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
SetupIconFile={#Root}\packaging\icons\freesnip.ico
UninstallDisplayIcon={app}\freesnip.exe
UninstallDisplayName=FreeSnip
VersionInfoVersion={#AppVersion}
OutputDir={#Root}\build\installer
OutputBaseFilename=FreeSnip-{#AppVersion}-windows-x64-setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
CloseApplications=force
RestartApplications=no

[Tasks]
Name: desktopicon; Description: "Create a &desktop shortcut"; Flags: unchecked
Name: startup; Description: "Start FreeSnip in the tray when I &sign in (Win+Shift+S and PrtScn work right away)"

[InstallDelete]
; What Snip installed before the rename.
Type: files; Name: "{app}\snip.exe"
Type: files; Name: "{autoprograms}\Snip.lnk"
Type: files; Name: "{autodesktop}\Snip.lnk"

[Files]
Source: "{#AppExe}"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Root}\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Root}\vendor\wxWidgets\docs\licence.txt"; DestDir: "{app}"; DestName: "wxWidgets-licence.txt"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\FreeSnip"; Filename: "{app}\freesnip.exe"
Name: "{autodesktop}\FreeSnip"; Filename: "{app}\freesnip.exe"; Tasks: desktopicon

[Registry]
; The same entry as "Start with Windows" in FreeSnip's tray menu.
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "FreeSnip"; \
  ValueData: """{app}\freesnip.exe"" /tray"; Flags: uninsdeletevalue; Tasks: startup
; Removed on uninstall even when it was set from the tray menu instead.
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: none; ValueName: "FreeSnip"; \
  Flags: uninsdeletevalue dontcreatekey

[Run]
Filename: "{app}\freesnip.exe"; Description: "Start FreeSnip"; Flags: nowait postinstall skipifsilent

[UninstallRun]
; FreeSnip keeps running in the tray: stop this installation's copy (and only it) so its
; files can be removed.
Filename: "{sys}\WindowsPowerShell\v1.0\powershell.exe"; \
  Parameters: "-NoProfile -NonInteractive -Command ""Get-Process freesnip -ErrorAction SilentlyContinue | Where-Object Path -eq '{app}\freesnip.exe' | Stop-Process -Force"""; \
  Flags: runhidden; RunOnceId: "StopFreeSnip"

[Code]
// Inno Setup 6.7 leaves too little room left of the task check boxes above 100% DPI,
// so their left edge is cut off; this margin shows them whole.
procedure InitializeWizard;
begin
  WizardForm.TasksList.Offset := ScaleX(4);
end;

// A running Snip from before the rename holds snip.exe, which [InstallDelete] removes, and
// the single-instance lock: stop this installation's copy before installing.
function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  ResultCode: Integer;
begin
  Exec(ExpandConstant('{sys}\WindowsPowerShell\v1.0\powershell.exe'),
    '-NoProfile -NonInteractive -Command "Get-Process snip -ErrorAction SilentlyContinue | Where-Object Path -eq ''' +
    ExpandConstant('{app}\snip.exe') + ''' | Stop-Process -Force"', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
  Result := '';
end;

// Snip's sign-in entry becomes FreeSnip's (FreeSnip itself does the same at start).
procedure CurStepChanged(CurStep: TSetupStep);
var
  RunKey: String;
begin
  RunKey := 'Software\Microsoft\Windows\CurrentVersion\Run';
  if (CurStep = ssPostInstall) and RegValueExists(HKCU, RunKey, 'Snip') then begin
    RegDeleteValue(HKCU, RunKey, 'Snip');
    RegWriteStringValue(HKCU, RunKey, 'FreeSnip', '"' + ExpandConstant('{app}\freesnip.exe') + '" /tray');
  end;
end;
