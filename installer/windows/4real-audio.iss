; Inno Setup script: one installer (Setup.exe) for 4real-audio on Windows 64-bit.
; Built by .github/workflows/build.yml:  iscc /DAppVersion=0.9.0 /DBuildDir=<build/FourReal_artefacts/Release> /DOutDir=<dir> 4real-audio.iss
;   VST3        -> C:\Program Files\Common Files\VST3\4real-audio.vst3   (every DAW that supports VST3 finds it there)
;   Standalone  -> C:\Program Files\4real-audio\4real-audio.exe  + Start menu shortcut (+ optional desktop icon)
; The uninstaller (Settings > Apps) removes everything the installer put in place.

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef BuildDir
  #define BuildDir "..\..\build\FourReal_artefacts\Release"
#endif
#ifndef OutDir
  #define OutDir "."
#endif

[Setup]
AppId={{B3A2F7DE-4C61-4F0A-9A1D-3F4E0A7D2C15}
AppName=4real-audio
AppVersion={#AppVersion}
AppVerName=4real-audio {#AppVersion}
AppPublisher=4Real
AppPublisherURL=https://3ffect.my.id
AppSupportURL=https://3ffect.my.id
DefaultDirName={autopf}\4real-audio
DefaultGroupName=4real-audio
DisableProgramGroupPage=yes
OutputDir={#OutDir}
OutputBaseFilename=4real-audio-{#AppVersion}-Windows-x64-Setup
Compression=lzma2/max
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
WizardStyle=modern
UninstallDisplayName=4real-audio
UninstallDisplayIcon={app}\4real-audio.exe

[Types]
Name: "full"; Description: "Full installation (plugin and standalone app)"
Name: "plugin"; Description: "Plugin only (VST3)"
Name: "standalone"; Description: "Standalone app only"
Name: "custom"; Description: "Custom installation"; Flags: iscustom

[Components]
Name: "vst3"; Description: "VST3 plugin (for your DAW, e.g. Studio One, Reaper, Cubase)"; Types: full plugin custom
Name: "standalone"; Description: "Standalone app"; Types: full standalone custom

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; Components: standalone; Flags: unchecked

[Files]
Source: "{#BuildDir}\VST3\4real-audio.vst3\*"; DestDir: "{commoncf64}\VST3\4real-audio.vst3"; Components: vst3; Flags: recursesubdirs createallsubdirs ignoreversion
Source: "{#BuildDir}\Standalone\4real-audio.exe"; DestDir: "{app}"; Components: standalone; Flags: ignoreversion

[Icons]
Name: "{group}\4real-audio"; Filename: "{app}\4real-audio.exe"; Components: standalone
Name: "{group}\Uninstall 4real-audio"; Filename: "{uninstallexe}"
Name: "{autodesktop}\4real-audio"; Filename: "{app}\4real-audio.exe"; Tasks: desktopicon; Components: standalone

[Run]
Filename: "{app}\4real-audio.exe"; Description: "Start 4real-audio"; Flags: nowait postinstall skipifsilent unchecked; Components: standalone
