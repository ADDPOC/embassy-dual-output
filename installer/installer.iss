; Embassy Dual Output - Windows installer (Inno Setup 6)
; Installs into the OBS 33+ plugin location:
;   C:\ProgramData\obs-studio\plugins\embassy-dual-output\

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\release\RelWithDebInfo\embassy-dual-output"
#endif
#ifndef OutputDir
  #define OutputDir "..\release"
#endif

[Setup]
AppId={{13ADA163-3EFC-4263-A690-75190F90BAF0}
AppName=Embassy Dual Output for OBS
AppVersion={#AppVersion}
AppPublisher=The Embassy Group
AppPublisherURL=https://www.theembassygroup.net
DefaultDirName={commonappdata}\obs-studio\plugins\embassy-dual-output
DisableDirPage=yes
DisableProgramGroupPage=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir={#OutputDir}
OutputBaseFilename=embassy-dual-output-{#AppVersion}-windows-x64-Installer
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName=Embassy Dual Output for OBS
CloseApplications=yes

[Messages]
WelcomeLabel2=This will install Embassy Dual Output {#AppVersion} into OBS Studio.%n%nPlease CLOSE OBS before continuing.%n%nRequires OBS Studio 33 or newer.

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Excludes: "*.pdb"; Flags: ignoreversion recursesubdirs createallsubdirs

[InstallDelete]
; Remove the older bin\64bit layout if an earlier build was copied by hand
Type: filesandordirs; Name: "{app}\bin"

[Code]
function IsObsRunning(): Boolean;
var
  ResultCode: Integer;
begin
  { tasklist exits 0 either way; find returns 0 only when obs64.exe is listed }
  Exec(ExpandConstant('{cmd}'), '/C tasklist /FI "IMAGENAME eq obs64.exe" | find /I "obs64.exe" >nul',
       '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
  Result := (ResultCode = 0);
end;

function InitializeSetup(): Boolean;
begin
  Result := True;
  while IsObsRunning() do
  begin
    if MsgBox('OBS Studio is still running. Please close OBS, then click OK.' + #13#10 +
              'Click Cancel to stop the installer.', mbInformation, MB_OKCANCEL) = IDCANCEL then
    begin
      Result := False;
      Exit;
    end;
  end;
end;
