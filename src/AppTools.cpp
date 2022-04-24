/* Copyright 2022 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

#include "utils/BaseUtil.h"
#include "utils/WinDynCalls.h"
#include "utils/CmdLineArgsIter.h"
#include "utils/DbgHelpDyn.h"
#include "utils/FileUtil.h"
#include "utils/WinUtil.h"
#include "utils/CryptoUtil.h"
#include "utils/DirIter.h"

#include "AppTools.h"
#include "SumatraConfig.h"
#include "Translations.h"
#include "Version.h"

#include "utils/Log.h"

/* Returns true, if a Registry entry indicates that this executable has been
   created by an installer (and should be updated through an installer) */
bool HasBeenInstalled() {
    // see GetInstallationDir() in Installer.cpp
    const WCHAR* appName = GetAppNameTemp();
    const WCHAR* regPathUninst = str::JoinTemp(L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\", appName);
    AutoFreeWstr installedPath = LoggedReadRegStr2(regPathUninst, L"InstallLocation");
    if (!installedPath) {
        return false;
    }

    auto exePath = GetExePathTemp();
    if (exePath.empty()) {
        return false;
    }

    if (!str::EndsWithI(installedPath, L".exe")) {
        WCHAR* tmp = path::Join(installedPath, path::GetBaseNameTemp(exePath));
        installedPath.Set(tmp);
    }
    return path::IsSame(installedPath, exePath);
}

/* Return false if this program has been started from "Program Files" directory
   (which is an indicator that it has been installed) or from the last known
   location of a SumatraPDF installation: */
bool IsRunningInPortableMode() {
    // cache the result so that it will be consistent during the lifetime of the process
    static int sCacheIsPortable = -1; // -1 == uninitialized, 0 == installed, 1 == portable
    if (sCacheIsPortable != -1) {
        return sCacheIsPortable != 0;
    }
    sCacheIsPortable = 1;

    if (HasBeenInstalled()) {
        sCacheIsPortable = 0;
        return false;
    }

    auto exePath = GetExePathTemp().Get();
    WCHAR* programFilesDir = GetSpecialFolderTemp(CSIDL_PROGRAM_FILES).Get();
    // if we can't get a path, assume we're not running from "Program Files"
    if (!exePath || !programFilesDir) {
        return true;
    }

    // check if one of the exePath's parent directories is "Program Files"
    // (or a junction to it)
    WCHAR* baseName;
    while ((baseName = (WCHAR*)path::GetBaseNameTemp(exePath)) > exePath) {
        baseName[-1] = '\0';
        if (path::IsSame(programFilesDir, exePath)) {
            sCacheIsPortable = 0;
            return false;
        }
    }

    return true;
}

bool IsDllBuild() {
    HRSRC resSrc = FindResourceW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1), RT_RCDATA);
    return resSrc != nullptr;
}

static AutoFreeWstr gAppDataDir;

void SetAppDataPath(const WCHAR* path) {
    gAppDataDir.Set(path::Normalize(path));
}

/* Generate the full path for a filename used by the app in the userdata path. */
/* Caller needs to free() the result. */
WCHAR* AppGenDataFilename(const WCHAR* fileName) {
    if (!fileName) {
        return nullptr;
    }

    if (gAppDataDir && dir::Exists(gAppDataDir)) {
        return path::Join(gAppDataDir, fileName);
    }

    if (IsRunningInPortableMode()) {
        /* Use the same path as the binary */
        return path::GetPathOfFileInAppDir(fileName);
    }

    WCHAR* path = GetSpecialFolderTemp(CSIDL_LOCAL_APPDATA, true).Get();
    if (!path) {
        return nullptr;
    }
    const WCHAR* appName = GetAppNameTemp();
    path = path::Join(path, appName);
    if (!path) {
        return nullptr;
    }
    WCHAR* res = nullptr;
    bool ok = dir::Create(path);
    if (ok) {
        res = path::Join(path, fileName);
    }
    str::Free(path);
    return res;
}

char* AppGenDataFilenameTemp(const char* fileName) {
    if (!fileName) {
        return nullptr;
    }
    WCHAR* tmp = ToWstrTemp(fileName);
    WCHAR* path = AppGenDataFilename(tmp);
    char* res = ToUtf8Temp(path);
    str::Free(path);
    return res;
}

#if 0
WCHAR* PathForFileInAppDataDir(const WCHAR* fileName) {
    if (!fileName) {
        return nullptr;
    }

    /* Use local (non-roaming) app data directory */
    TempWstr dataDir = GetSpecialFolderTemp(CSIDL_LOCAL_APPDATA, true);
    AutoFreeWstr dir = path::Join(dataDir.Get(), APP_NAME_STR);
    bool ok = dir::Create(dir);
    if (!ok) {
        return nullptr;
    }

    return path::Join(dir, fileName);
}
#endif

// List of rules used to detect TeX editors.

// type of path information retrieved from the registy
enum EditorPathType {
    BinaryPath,  // full path to the editor's binary file
    BinaryDir,   // directory containing the editor's binary file
    SiblingPath, // full path to a sibling file of the editor's binary file
};

#define kRegWinCurrentVer L"Software\\Microsoft\\Windows\\CurrentVersion"

static struct {
    const char* binaryFilename;    // Editor's binary file name
    const char* inverseSearchArgs; // Parameters to be passed to the editor;
                                   // use placeholder '%f' for path to source file and '%l' for line number.
    EditorPathType Type;           // Type of the path information obtained from the registry
    HKEY RegRoot;                  // Root of the regkey
    const WCHAR* RegKey;           // Registry key path
    const WCHAR* RegValue;         // Registry value name
} editorRules[] = {
    {"WinEdt.exe", "\"[Open(|%f|);SelPar(%l,8)]\"", BinaryPath, HKEY_LOCAL_MACHINE,
     kRegWinCurrentVer L"\\App Paths\\WinEdt.exe", nullptr},
    {"WinEdt.exe", "\"[Open(|%f|);SelPar(%l,8)]\"", BinaryDir, HKEY_CURRENT_USER, L"Software\\WinEdt", L"Install Root"},
    {"notepad++.exe", "-n%l \"%f\"", BinaryPath, HKEY_LOCAL_MACHINE, kRegWinCurrentVer L"\\App Paths\\notepad++.exe",
     nullptr},
    {"notepad++.exe", "-n%l \"%f\"", BinaryDir, HKEY_LOCAL_MACHINE, L"Software\\Notepad++", nullptr},
    {"notepad++.exe", "-n%l \"%f\"", BinaryPath, HKEY_LOCAL_MACHINE, kRegWinCurrentVer L"\\Uninstall\\Notepad++",
     L"DisplayIcon"},
    {"sublime_text.exe", "\"%f:%l\"", BinaryDir, HKEY_LOCAL_MACHINE,
     kRegWinCurrentVer L"\\Uninstall\\Sublime Text 3_is1", L"InstallLocation"},
    {"sublime_text.exe", "\"%f:%l\"", BinaryPath, HKEY_LOCAL_MACHINE,
     kRegWinCurrentVer L"\\Uninstall\\Sublime Text 3_is1", L"DisplayIcon"},
    {"sublime_text.exe", "\"%f:%l\"", BinaryDir, HKEY_LOCAL_MACHINE,
     kRegWinCurrentVer L"\\Uninstall\\Sublime Text 2_is1", L"InstallLocation"},
    {"sublime_text.exe", "\"%f:%l\"", BinaryPath, HKEY_LOCAL_MACHINE,
     kRegWinCurrentVer L"\\Uninstall\\Sublime Text 2_is1", L"DisplayIcon"},
    {"TeXnicCenter.exe", "/ddecmd \"[goto('%f', '%l')]\"", BinaryDir, HKEY_LOCAL_MACHINE,
     L"Software\\ToolsCenter\\TeXnicCenterNT", L"AppPath"},
    {"TeXnicCenter.exe", "/ddecmd \"[goto('%f', '%l')]\"", BinaryDir, HKEY_LOCAL_MACHINE,
     kRegWinCurrentVer L"\\Uninstall\\TeXnicCenter_is1", L"InstallLocation"},
    {"TeXnicCenter.exe", "/ddecmd \"[goto('%f', '%l')]\"", BinaryDir, HKEY_LOCAL_MACHINE,
     kRegWinCurrentVer L"\\Uninstall\\TeXnicCenter Alpha_is1", L"InstallLocation"},
    {"TEXCNTR.exe", "/ddecmd \"[goto('%f', '%l')]\"", BinaryDir, HKEY_LOCAL_MACHINE,
     L"Software\\ToolsCenter\\TeXnicCenter", L"AppPath"},
    {"TEXCNTR.exe", "/ddecmd \"[goto('%f', '%l')]\"", BinaryDir, HKEY_LOCAL_MACHINE,
     kRegWinCurrentVer L"\\Uninstall\\TeXnicCenter_is1", L"InstallLocation"},
    {"WinShell.exe", "-c \"%f\" -l %l", BinaryDir, HKEY_LOCAL_MACHINE, kRegWinCurrentVer L"\\Uninstall\\WinShell_is1",
     L"InstallLocation"},
    {"gvim.exe", "\"%f\" +%l", BinaryPath, HKEY_LOCAL_MACHINE, L"Software\\Vim\\Gvim", L"path"},
    {// TODO: add this rule only if the latex-suite for ViM is installed
     // (http://vim-latex.sourceforge.net/documentation/latex-suite.txt)
     "gvim.exe", "-c \":RemoteOpen +%l %f\"", BinaryPath, HKEY_LOCAL_MACHINE, L"Software\\Vim\\Gvim", L"path"},
    {"texmaker.exe", "\"%f\" -line %l", SiblingPath, HKEY_LOCAL_MACHINE, kRegWinCurrentVer L"\\Uninstall\\Texmaker",
     L"UninstallString"},
    {
        "TeXworks.exe", "-p=%l \"%f\"", BinaryDir, HKEY_LOCAL_MACHINE,
        kRegWinCurrentVer "\\Uninstall\\{41DA4817-4D2A-4D83-AD02-6A2D95DC8DCB}_is1", L"InstallLocation",
        // TODO: find a way to detect where emacs is installed
        // L"emacsclientw.exe",L"+%l \"%f\"", BinaryPath, HKEY_LOCAL_MACHINE, L"???", L"???",
    }};

// Detect TeX editors installed on the system and construct the
// corresponding inverse search commands.
//
// Parameters:
//      hwndCombo   -- (optional) handle to a combo list that will be filled with the list of possible inverse search
//      commands.
// Returns:
//      the inverse search command of the first detected editor (the caller needs to free() the result).
WCHAR* AutoDetectInverseSearchCommands(HWND hwndCombo) {
    WCHAR* firstEditor = nullptr;
    WStrList foundExes;

    for (int i = 0; i < dimof(editorRules); i++) {
        auto& rule = editorRules[i];
        AutoFreeWstr path(LoggedReadRegStr(rule.RegRoot, rule.RegKey, rule.RegValue));
        if (!path) {
            continue;
        }

        AutoFreeWstr exePath;
        WCHAR* binaryFileName = ToWstrTemp(rule.binaryFilename);
        WCHAR* inverseSearchArgs = ToWstrTemp(rule.inverseSearchArgs);
        if (rule.Type == SiblingPath) {
            // remove file part
            AutoFreeWstr dir(path::GetDir(path));
            exePath.Set(path::Join(dir, binaryFileName));
        } else if (rule.Type == BinaryDir) {
            exePath.Set(path::Join(path, binaryFileName));
        } else { // if (editor_rules[i].Type == BinaryPath)
            exePath.Set(path.StealData());
        }
        // don't show duplicate entries
        if (foundExes.FindI(exePath) != -1) {
            continue;
        }
        // don't show inexistent paths (and don't try again for them)
        if (!file::Exists(exePath)) {
            foundExes.Append(exePath.StealData());
            continue;
        }

        AutoFreeWstr editorCmd(str::Format(L"\"%s\" %s", exePath.Get(), inverseSearchArgs));

        if (!hwndCombo) {
            // no need to fill a combo box: return immeditately after finding an editor.
            return editorCmd.StealData();
        }

        ComboBox_AddString(hwndCombo, editorCmd);
        if (!firstEditor) {
            firstEditor = editorCmd.StealData();
        }
        foundExes.Append(exePath.StealData());
    }

    // Fall back to notepad as a default handler
    if (!firstEditor) {
        firstEditor = str::Dup(L"notepad %f");
        if (hwndCombo) {
            ComboBox_AddString(hwndCombo, firstEditor);
        }
    }
    return firstEditor;
}

#define UWM_DELAYED_SET_FOCUS (WM_APP + 1)
#define UWM_DELAYED_CTRL_BACK (WM_APP + 2)

// selects all text in an edit box if it's selected either
// through a keyboard shortcut or a non-selecting mouse click
// (or responds to Ctrl+Backspace as nowadays expected)
bool ExtendedEditWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM) {
    static bool delayFocus = false;

    switch (msg) {
        case WM_LBUTTONDOWN:
            delayFocus = !IsFocused(hwnd);
            return true;

        case WM_LBUTTONUP:
            if (delayFocus) {
                DWORD sel = Edit_GetSel(hwnd);
                if (LOWORD(sel) == HIWORD(sel)) {
                    PostMessageW(hwnd, UWM_DELAYED_SET_FOCUS, 0, 0);
                }
                delayFocus = false;
            }
            return true;

        case WM_KILLFOCUS:
            return false; // for easier debugging (make setting a breakpoint possible)

        case WM_SETFOCUS:
            if (!delayFocus) {
                PostMessageW(hwnd, UWM_DELAYED_SET_FOCUS, 0, 0);
            }
            return true;

        case UWM_DELAYED_SET_FOCUS:
            EditSelectAll(hwnd);
            return true;

        case WM_KEYDOWN:
            if (VK_BACK != wp || !IsCtrlPressed() || IsShiftPressed()) {
                return false;
            }
            PostMessageW(hwnd, UWM_DELAYED_CTRL_BACK, 0, 0);
            return true;

        case UWM_DELAYED_CTRL_BACK: {
            WCHAR* text = win::GetTextTemp(hwnd).Get();
            int selStart = LOWORD(Edit_GetSel(hwnd)), selEnd = selStart;
            // remove the rectangle produced by Ctrl+Backspace
            if (selStart > 0 && text[selStart - 1] == '\x7F') {
                memmove(text + selStart - 1, text + selStart, str::Len(text + selStart - 1) * sizeof(WCHAR));
                win::SetText(hwnd, text);
                selStart = selEnd = selStart - 1;
            }
            // remove the previous word (and any spacing after it)
            for (; selStart > 0 && str::IsWs(text[selStart - 1]); selStart--) {
                ;
            }
            for (; selStart > 0 && !str::IsWs(text[selStart - 1]); selStart--) {
                ;
            }
            Edit_SetSel(hwnd, selStart, selEnd);
            SendMessageW(hwnd, WM_CLEAR, 0, 0);
        }
            return true;

        default:
            return false;
    }
}

/* Default size for the window, happens to be american A4 size (I think) */
#define DEF_PAGE_RATIO (612.0 / 792.0)

#define MIN_WIN_DX 50
#define MIN_WIN_DY 50

void EnsureAreaVisibility(Rect& r) {
    // adjust to the work-area of the current monitor (not necessarily the primary one)
    Rect work = GetWorkAreaRect(r, nullptr);

    // make sure that the window is neither too small nor bigger than the monitor
    if (r.dx < MIN_WIN_DX || r.dx > work.dx) {
        r.dx = std::min((int)((double)work.dy * DEF_PAGE_RATIO), work.dx);
    }
    if (r.dy < MIN_WIN_DY || r.dy > work.dy) {
        r.dy = work.dy;
    }

    // check whether the lower half of the window's title bar is
    // inside a visible working area
    int captionDy = GetSystemMetrics(SM_CYCAPTION);
    Rect halfCaption(r.x, r.y + captionDy / 2, r.dx, captionDy / 2);
    if (halfCaption.Intersect(work).IsEmpty()) {
        r = Rect(work.TL(), r.Size());
    }
}

Rect GetDefaultWindowPos() {
    RECT workArea;
    SystemParametersInfo(SPI_GETWORKAREA, 0, &workArea, 0);
    Rect work = Rect::FromRECT(workArea);

    Rect r = work;
    r.dx = std::min((int)((double)r.dy * DEF_PAGE_RATIO), work.dx);
    r.x = (work.dx - r.dx) / 2;

    return r;
}

void SaveCallstackLogs() {
    AutoFree s = dbghelp::GetCallstacks();
    if (s.empty()) {
        return;
    }
    AutoFreeWstr filePath(AppGenDataFilename(L"callstacks.txt"));
    file::WriteFile(filePath.Get(), s.AsSpan());
}

// TODO: this can be used for extracting other data
#if 0
// cache because calculating md5 of the whole executable
// might be relatively expensive
static AutoFreeWstr gAppMd5;

// return hex version of md5 of app's executable
// nullptr if there was an error
// caller needs to free the result
static const WCHAR* Md5OfAppExe() {
    if (gAppMd5.Get()) {
        return str::Dup(gAppMd5.Get());
    }

    auto appPath = GetExePathTemp();
    if (appPath.empty()) {
        return {};
    }
    AutoFree d = file::ReadFile(appPath.data);
    if (d.empty()) {
        return nullptr;
    }

    u8 md5[16]{};
    CalcMD5Digest(d.data, d.size(), md5);

    AutoFree md5HexA(_MemToHex(&md5));
    AutoFreeWstr md5Hex = strconv::Utf8ToWchar(md5HexA.AsView());

    return md5Hex.StealData();
}

// remove all directories except for ours
//. need to avoid acuumulating the directories when testing
// locally or using pre-release builds (both cases where
// exe and its md5 changes frequently)
void RemoveMd5AppDataDirectories() {
    AutoFreeWstr extractedDir = PathForFileInAppDataDir(L"extracted");
    if (extractedDir.empty()) {
        return;
    }

    auto dirs = CollectDirsFromDirectory(extractedDir.data);
    if (dirs.empty()) {
        return;
    }

    AutoFreeWstr md5App = Md5OfAppExe();
    if (md5App.empty()) {
        return;
    }

    AutoFreeWstr md5Dir = path::Join(extractedDir.data, md5App.data);

    for (auto& dir : dirs) {
        const WCHAR* s = dir.data();
        if (str::Eq(s, md5Dir.data)) {
            continue;
        }
        dir::RemoveAll(s);
    }
}

// return a path on disk to extracted unrar.dll or nullptr if couldn't extract
// memory has to be freed by the caller
const WCHAR* ExractUnrarDll() {
    RemoveMd5AppDataDirectories();

    AutoFreeWstr extractedDir = PathForFileInAppDataDir(L"extracted");
    if (extractedDir.empty()) {
        return nullptr;
    }

    AutoFreeWstr md5App = Md5OfAppExe();
    if (md5App.empty()) {
        return nullptr;
    }

    AutoFreeWstr md5Dir = path::Join(extractedDir.data, md5App.data);
    AutoFreeWstr dllPath = path::Join(md5Dir.data, unrarFileName);

    if (file::Exists(dllPath.data)) {
        const WCHAR* ret = dllPath.data;
        dllPath = nullptr; // don't free
        return ret;
    }

    bool ok = dir::CreateAll(md5Dir.data);
    if (!ok) {
        return nullptr;
    }

    HGLOBAL res = 0;
    auto h = GetModuleHandle(nullptr);
    WCHAR* resName = MAKEINTRESOURCEW(1);
    HRSRC resSrc = FindResourceW(h, resName, RT_RCDATA);
    if (!resSrc) {
        return nullptr;
    }
    res = LoadResource(nullptr, resSrc);
    if (!res) {
        return nullptr;
    }
    const char* data = (const char*)LockResource(res);
    defer {
        UnlockResource(res);
    };
    DWORD dataSize = SizeofResource(nullptr, resSrc);
    ok = file::WriteFile(dllPath, data, dataSize);
    if (!ok) {
        return nullptr;
    }

    const WCHAR* ret = dllPath;
    dllPath = nullptr; // don't free
    return ret;
}
#endif

constexpr double KB = 1024;
constexpr double MB = (double)1024 * (double)1024;
constexpr double GB = (double)1024 * (double)1024 * (double)1024;

// Format the file size in a short form that rounds to the largest size unit
// e.g. "3.48 GB", "12.38 MB", "23 KB"
// Caller needs to free the result.
static WCHAR* FormatSizeSuccint(i64 size) {
    const WCHAR* unit = nullptr;
    double s = (double)size;

    if (s > GB) {
        s = s / GB;
        unit = _TR("GB");
    } else if (s > MB) {
        s = s / MB;
        unit = _TR("MB");
    } else {
        s = s / KB;
        unit = _TR("KB");
    }

    AutoFreeWstr sizestr = str::FormatFloatWithThousandSep(s);
    if (!unit) {
        return sizestr.StealData();
    }
    return str::Format(L"%s %s", sizestr.Get(), unit);
}

// format file size in a readable way e.g. 1348258 is shown
// as "1.29 MB (1,348,258 Bytes)"
// Caller needs to free the result
WCHAR* FormatFileSize(i64 size) {
    if (size <= 0) {
        return str::Format(L"%d", (int)size);
    }
    AutoFreeWstr n1(FormatSizeSuccint(size));
    AutoFreeWstr n2(str::FormatNumWithThousandSep(size));
    return str::Format(L"%s (%s %s)", n1.Get(), n2.Get(), _TR("Bytes"));
}

// Format the file size in a short form that rounds to the largest size unit
// e.g. "3.48 GB", "12.38 MB", "23 KB"
// To be used in a context where translations are not yet available
// Caller needs to free the result.
static WCHAR* FormatSizeSuccintNoTrans(i64 size) {
    const WCHAR* unit = nullptr;
    double s = (double)size;

    if (s > GB) {
        s = s / GB;
        unit = L"GB";
    } else if (s > MB) {
        s = s / MB;
        unit = L"MB";
    } else {
        s = s / KB;
        unit = L"KB";
    }

    AutoFreeWstr sizestr = str::FormatFloatWithThousandSep(s);
    if (!unit) {
        return sizestr.StealData();
    }
    return str::Format(L"%s %s", sizestr.Get(), unit);
}

// format file size in a readable way e.g. 1348258 is shown
// as "1.29 MB (1,348,258 Bytes)"
// Caller needs to free the result
WCHAR* FormatFileSizeNoTrans(i64 size) {
    if (size <= 0) {
        return str::Format(L"%d", (int)size);
    }
    AutoFreeWstr n1(FormatSizeSuccintNoTrans(size));
    AutoFreeWstr n2(str::FormatNumWithThousandSep(size));
    return str::Format(L"%s (%s %s)", n1.Get(), n2.Get(), L"Bytes");
}

void ShowLogFile(const char* logPath) {
    if (!logPath) {
        return;
    }
    WCHAR* path = ToWstrTemp(logPath);
    LaunchFile(path, nullptr, L"open");
}
