; ============================================================
; Inno Setup script for Subtitle Extractor
;
; Build with:
;   "C:\Users\charl\AppData\Local\Programs\Inno Setup 6\ISCC.exe" installer.iss
;
; Output:
;   installer\SubtitleExtractor_Setup_v1.0.exe
;
; Notes for Halit:
;   - This bundles the entire build\Release\ tree (the Qt DLLs and plugins
;     windeployqt already prepared).
;   - It does NOT bundle FFmpeg or Tesseract — those must be installed on
;     the target machine and reachable on PATH. To bundle them too, copy
;     ffmpeg.exe and tesseract.exe into build\Release\ AND modify
;     ProcessRunner.cpp to look in QCoreApplication::applicationDirPath()
;     first. Out of scope for now.
;   - Default install dir is "Program Files\Subtitle Extractor". Requires
;     admin (UAC). To allow non-admin user-local installs, change
;     PrivilegesRequired to "lowest" and DefaultDirName to "{userpf}\...".
; ============================================================

#define MyAppName       "Subtitle Extractor"
#define MyAppVersion    "1.0.3"
#define MyAppPublisher  "Halit"
#define MyAppExeName    "subtitle_extractor_gui.exe"

[Setup]
; AppId uniquely identifies the application. Used by Windows' uninstaller
; registry. Generate a NEW GUID if you fork this for a different product.
AppId={{8B1F2E1D-7C3A-4F2D-9B9A-9C2E1A4B5C6D}}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppVerName={#MyAppName} {#MyAppVersion}

; {autopf} resolves to "Program Files" on x64. Requires admin install.
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes

; Output settings — Setup.exe goes into installer\ subfolder.
OutputDir=installer
OutputBaseFilename=SubtitleExtractor_Setup_v{#MyAppVersion}

; LZMA2 max gives the smallest installer (Qt DLLs compress well).
Compression=lzma2/max
SolidCompression=yes

; Use the custom icon if it exists. Inno's #ifexist preprocessor
; lets us keep this script working before the icon is added.
#ifexist "resources\icon.ico"
SetupIconFile=resources\icon.ico
#endif

; ",0" tells Windows to use the first icon embedded in the .exe.
UninstallDisplayIcon={app}\{#MyAppExeName},0

; Modern wizard look.
WizardStyle=modern

; x64 only — our build is 64-bit.
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible

; Uninstall display name (icon is set above via UninstallDisplayIcon).
UninstallDisplayName={#MyAppName}

PrivilegesRequired=admin

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut"; \
    GroupDescription: "Additional shortcuts:"; Flags: unchecked

[Files]
; Bundle the entire windeployqt-prepared Release folder. recursesubdirs
; pulls in platforms\, imageformats\, styles\, etc.
; ignoreversion: replace existing files regardless of version metadata.
Source: "build\Release\*"; DestDir: "{app}"; \
    Flags: recursesubdirs ignoreversion

; Bundle the default Whisper model if it's sitting at the project root.
; Friends-of-Halit get a working program with zero setup. Adds ~147 MB.
; #ifexist keeps the script working on machines that don't have a copy.
#ifexist "ggml-base.en.bin"
Source: "ggml-base.en.bin"; DestDir: "{app}"; Flags: ignoreversion
#endif

; Bundle ffmpeg.exe so friends don't need a separate FFmpeg install.
; The gyan.dev "full_build" is a single static binary (~227 MB raw,
; ~100 MB after LZMA2 compression). ProcessRunner::setBundledBinaryDir
; in gui_main.cpp causes the GUI to prefer this copy over PATH.
;
; The hard-coded source path matches winget's install layout for
; Gyan.FFmpeg on Halit's machine. If you reinstall FFmpeg via another
; method, update this path or copy ffmpeg.exe somewhere stable.
#define FFmpegExe "C:\Users\charl\AppData\Local\Microsoft\WinGet\Packages\Gyan.FFmpeg_Microsoft.Winget.Source_8wekyb3d8bbwe\ffmpeg-8.1.1-full_build\bin\ffmpeg.exe"
#ifexist FFmpegExe
Source: {#FFmpegExe}; DestDir: "{app}"; Flags: ignoreversion
#endif

; Bundle Tesseract OCR for the "burned-in subtitles" mode.
; Layout: tesseract.exe + ~50 sibling DLLs all in {app}, plus
; tessdata\eng.traineddata for English. Tesseract searches relative
; to its own exe path, so a flat install works.
#ifexist "C:\Program Files\Tesseract-OCR\tesseract.exe"
Source: "C:\Program Files\Tesseract-OCR\tesseract.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "C:\Program Files\Tesseract-OCR\*.dll";        DestDir: "{app}"; Flags: ignoreversion
Source: "C:\Program Files\Tesseract-OCR\tessdata\eng.traineddata"; DestDir: "{app}\tessdata"; Flags: ignoreversion
#endif

[Icons]
; Start Menu shortcut.
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\Uninstall {#MyAppName}"; Filename: "{uninstallexe}"

; Optional desktop shortcut (controlled by the [Tasks] checkbox above).
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; \
    Tasks: desktopicon

[Run]
; Offer to launch the program when the installer finishes.
Filename: "{app}\{#MyAppExeName}"; \
    Description: "Launch {#MyAppName}"; \
    Flags: nowait postinstall skipifsilent
