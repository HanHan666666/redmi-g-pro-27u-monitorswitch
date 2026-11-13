#ifndef AppVersion
  #define AppVersion "1.1.0"
#endif
#ifndef SourceDir
  #error SourceDir must point to the staged application directory.
#endif
#ifndef OutputDir
  #error OutputDir must point to the package output directory.
#endif

[Setup]
AppId={{8B2F1A24-6C5B-4B4E-9A3D-6E9D1C0B7A51}
AppName=MonitorSwitch
AppVersion={#AppVersion}
AppPublisher=MonitorSwitch contributors
AppPublisherURL=https://github.com/HanHan666666/redmi-g-pro-27u-monitorswitch
AppSupportURL=https://github.com/HanHan666666/redmi-g-pro-27u-monitorswitch/issues
AppUpdatesURL=https://github.com/HanHan666666/redmi-g-pro-27u-monitorswitch/releases
DefaultDirName={localappdata}\Programs\MonitorSwitch
DefaultGroupName=MonitorSwitch
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.17763
AppMutex=Local\MonitorSwitch.Tray.SingleInstance
UninstallDisplayIcon={app}\MonitorSwitch.exe
SetupIconFile=..\Assets\AppIcon.ico
LicenseFile=..\..\LICENSE
OutputDir={#OutputDir}
OutputBaseFilename=MonitorSwitch-{#AppVersion}-Windows-x64-Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes
RestartApplications=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\MonitorSwitch"; Filename: "{app}\MonitorSwitch.exe"
Name: "{autodesktop}\MonitorSwitch"; Filename: "{app}\MonitorSwitch.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\MonitorSwitch.exe"; Description: "Launch MonitorSwitch"; Flags: nowait postinstall skipifsilent
