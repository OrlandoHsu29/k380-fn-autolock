#ifndef MyAppVersion
  #define MyAppVersion "1.0.3"
#endif

#define MyAppName "K380 Fn Auto Lock"
#define MyAppExeName "k380FnAutoLock.exe"
#define MyAppUrl "https://github.com/OrlandoHsu29/k380-fn-autolock"

[Setup]
AppId={{9C847864-F089-45CA-9555-57D643812299}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} v{#MyAppVersion}
AppPublisher=OrlandoHsu29
AppPublisherURL={#MyAppUrl}
AppSupportURL={#MyAppUrl}/issues
AppUpdatesURL={#MyAppUrl}/releases
DefaultDirName={localappdata}\Programs\K380-Fn-Auto-Lock
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
OutputDir=..\dist
OutputBaseFilename=K380-Fn-Auto-Lock-v{#MyAppVersion}-windows-x64-setup
SetupIconFile=..\media\k380-fn-autolock-logo.ico
UninstallDisplayIcon={app}\{#MyAppExeName}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes
RestartApplications=no
VersionInfoVersion={#MyAppVersion}.0
VersionInfoProductName={#MyAppName}
VersionInfoProductVersion={#MyAppVersion}
VersionInfoDescription={#MyAppName} Installer
VersionInfoCompany=OrlandoHsu29

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "chinesesimp"; MessagesFile: "compiler:Languages\ChineseSimplified.isl"

[CustomMessages]
english.AutoStartTask=Start automatically when Windows starts
english.LaunchProgram=Launch K380 Fn Auto Lock
chinesesimp.AutoStartTask=开机时自动启动
chinesesimp.LaunchProgram=运行 K380 Fn Auto Lock

[Tasks]
Name: "autostart"; Description: "{cm:AutoStartTask}"; Flags: checkedonce

[Files]
Source: "..\build\{#MyAppExeName}"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\build\hidapi.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\build\LICENSE-hidapi-bsd.txt"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"

[Registry]
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "K380FnAutoLock"; ValueData: """{app}\{#MyAppExeName}"" --background"; Flags: uninsdeletevalue; Tasks: autostart

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram}"; Flags: nowait postinstall skipifsilent

[Code]
const
  AppWindowClass = 'K380FnAutoLockWindow';
  RunKey = 'Software\Microsoft\Windows\CurrentVersion\Run';
  RunValue = 'K380FnAutoLock';
  PreferencesKey = 'Software\K380FnAutoLock';
  WM_CLOSE = $0010;

procedure StopRunningApp;
var
  AppWindow: HWND;
  Attempt: Integer;
begin
  AppWindow := FindWindowByClassName(AppWindowClass);
  if AppWindow = 0 then
    Exit;

  PostMessage(AppWindow, WM_CLOSE, 0, 0);
  for Attempt := 1 to 40 do
  begin
    Sleep(50);
    if FindWindowByClassName(AppWindowClass) = 0 then
      Exit;
  end;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  StopRunningApp;
  Result := '';
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if (CurStep = ssPostInstall) and (not WizardIsTaskSelected('autostart')) then
    RegDeleteValue(HKCU, RunKey, RunValue);
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usUninstall then
  begin
    StopRunningApp;
    RegDeleteValue(HKCU, RunKey, RunValue);
    RegDeleteKeyIncludingSubkeys(HKCU, PreferencesKey);
  end;
end;
