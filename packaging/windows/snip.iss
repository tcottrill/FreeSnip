; Inno Setup script for the Windows installer, Snip-<version>-windows-x64-setup.exe.
; Build it with tools\build-installer.ps1 after tools\build-windows.ps1. It is written to
; build\installer; tools\collect-release.ps1 copies it to dist\release with the other downloads.
;
; Installs for the current user (no administrator rights) into %LOCALAPPDATA%\Programs\Snip.
; A running Snip (it stays in the tray) is closed first and started again at the end.

#define Root AddBackslash(SourcePath) + "..\.."
#define AppExe Root + "\dist\snip.exe"
#if !FileExists(AppExe)
  #error dist\snip.exe is missing: run tools\build-windows.ps1 first
#endif
#define AppVersion GetStringFileInfo(AppExe, "ProductVersion")

[Setup]
AppId={{D16B3DB7-B36B-4851-A731-9DABA54A269C}
AppName=Snip
AppVersion={#AppVersion}
AppVerName=Snip {#AppVersion}
AppPublisher=Tim Cottrill
AppCopyright=Copyright (C) 2026 Tim Cottrill
DefaultDirName={autopf}\Snip
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
SetupIconFile={#Root}\packaging\icons\snip.ico
UninstallDisplayIcon={app}\snip.exe
UninstallDisplayName=Snip
VersionInfoVersion={#AppVersion}
OutputDir={#Root}\build\installer
OutputBaseFilename=Snip-{#AppVersion}-windows-x64-setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
CloseApplications=force
RestartApplications=no

[Tasks]
Name: desktopicon; Description: "Create a &desktop shortcut"; Flags: unchecked
Name: startup; Description: "Start Snip in the tray when I &sign in (Win+Shift+S and PrtScn work right away)"

[Files]
Source: "{#AppExe}"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Root}\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Root}\vendor\wxWidgets\docs\licence.txt"; DestDir: "{app}"; DestName: "wxWidgets-licence.txt"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\Snip"; Filename: "{app}\snip.exe"
Name: "{autodesktop}\Snip"; Filename: "{app}\snip.exe"; Tasks: desktopicon

[Registry]
; The same entry as "Start with Windows" in Snip's tray menu.
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "Snip"; \
  ValueData: """{app}\snip.exe"" /tray"; Flags: uninsdeletevalue; Tasks: startup
; Removed on uninstall even when it was set from the tray menu instead.
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: none; ValueName: "Snip"; \
  Flags: uninsdeletevalue dontcreatekey

[Run]
Filename: "{app}\snip.exe"; Description: "Start Snip"; Flags: nowait postinstall skipifsilent

[UninstallRun]
; Snip keeps running in the tray: stop this installation's copy (and only it) so its
; files can be removed.
Filename: "{sys}\WindowsPowerShell\v1.0\powershell.exe"; \
  Parameters: "-NoProfile -NonInteractive -Command ""Get-Process snip -ErrorAction SilentlyContinue | Where-Object Path -eq '{app}\snip.exe' | Stop-Process -Force"""; \
  Flags: runhidden; RunOnceId: "StopSnip"
