

/* Copyright 2021 the SumatraPDF project authors (see AUTHORS file).
   License: Simplified BSD (see COPYING.BSD) */

#include "utils/BaseUtil.h"
#include "utils/VecSegmented.h"
#include "utils/ScopedWin.h"
#include "utils/WinUtil.h"

#include "wingui/WinGui.h"
#include "wingui/Layout.h"
#include "wingui/Window.h"

#include "utils/Log.h"

// TODO: call RemoveWindowSubclass in WM_NCDESTROY as per
// https://devblogs.microsoft.com/oldnewthing/20031111-00/?p=41883

#define DEFAULT_WIN_CLASS L"WC_WIN32_WINDOW"

static UINT_PTR g_subclassId = 0;

UINT_PTR NextSubclassId() {
    g_subclassId++;
    return g_subclassId;
}

// initial value which should be save
static int g_currCtrlID = 100;

int GetNextCtrlID() {
    ++g_currCtrlID;
    return g_currCtrlID;
}

// a way to register for messages for a given hwnd / msg combo

struct HwndMsgHandler {
    HWND hwnd;
    UINT msg;
    void* user = nullptr;
    void (*handler)(void* user, WndEvent* ev);
};

VecSegmented<HwndMsgHandler> gHwndMsgHandlers;

void WindowCleanup() {
    gHwndMsgHandlers.allocator.FreeAll();
}

static void ClearHwndMsgHandler(HwndMsgHandler* h) {
    CrashIf(!h);
    if (!h) {
        return;
    }
    h->hwnd = nullptr;
    h->msg = 0;
    h->user = nullptr;
    h->handler = nullptr;
}

static HwndMsgHandler* FindHandlerForHwndAndMsg(HWND hwnd, UINT msg, bool create) {
    CrashIf(hwnd == nullptr);
    for (auto h : gHwndMsgHandlers) {
        if (h->hwnd == hwnd && h->msg == msg) {
            return h;
        }
    }
    if (!create) {
        return nullptr;
    }
    // we might have free slot
    HwndMsgHandler* res = nullptr;
    for (auto h : gHwndMsgHandlers) {
        if (h->hwnd == nullptr) {
            res = h;
            break;
        }
    }
    if (!res) {
        res = gHwndMsgHandlers.AllocAtEnd();
    }
    ClearHwndMsgHandler(res);
    res->hwnd = hwnd;
    res->msg = msg;
    return res;
}

void RegisterHandlerForMessage(HWND hwnd, UINT msg, void (*handler)(void* user, WndEvent*), void* user) {
    auto h = FindHandlerForHwndAndMsg(hwnd, msg, true);
    h->handler = handler;
    h->user = user;
}

void UnregisterHandlerForMessage(HWND hwnd, UINT msg) {
    auto h = FindHandlerForHwndAndMsg(hwnd, msg, false);
    ClearHwndMsgHandler(h);
}

// TODO: potentially more messages
// https://docs.microsoft.com/en-us/cpp/mfc/reflected-window-message-ids?view=vs-2019
static HWND GetChildHWNDForMessage(UINT msg, WPARAM wp, LPARAM lp) {
    // https://docs.microsoft.com/en-us/windows/win32/controls/wm-ctlcolorbtn
    if (WM_CTLCOLORBTN == msg) {
        return (HWND)lp;
    }
    if (WM_CTLCOLORSTATIC == msg) {
        HDC hdc = (HDC)wp;
        return WindowFromDC(hdc);
    }
    // https://docs.microsoft.com/en-us/windows/win32/controls/wm-notify
    if (WM_NOTIFY == msg) {
        NMHDR* hdr = (NMHDR*)lp;
        return hdr->hwndFrom;
    }
    // https://docs.microsoft.com/en-us/windows/win32/menurc/wm-command
    if (WM_COMMAND == msg) {
        return (HWND)lp;
    }
    // https://docs.microsoft.com/en-us/windows/win32/controls/wm-drawitem
    if (WM_DRAWITEM == msg) {
        DRAWITEMSTRUCT* s = (DRAWITEMSTRUCT*)lp;
        return s->hwndItem;
    }
    // https://docs.microsoft.com/en-us/windows/win32/menurc/wm-contextmenu
    if (WM_CONTEXTMENU == msg) {
        return (HWND)wp;
    }
    // https://docs.microsoft.com/en-us/windows/win32/controls/wm-vscroll--trackbar-
    if (WM_VSCROLL == msg) {
        return (HWND)lp;
    }
    // https://docs.microsoft.com/en-us/windows/win32/controls/wm-hscroll--trackbar-
    if (WM_HSCROLL == msg) {
        return (HWND)lp;
    }

    // TODO: there's no HWND so have to do it differently e.g. allocate
    // unique CtlID, store it in WindowBase and compare that
#if 0
    // https://docs.microsoft.com/en-us/windows/win32/controls/wm-measureitem
    if (WM_MEASUREITEM == msg) {
        MEASUREITEMSTRUCT* s = (MEASUREITEMSTRUCT*)lp;
        return s->CtlID;
    }
#endif
    return nullptr;
}

bool HandleRegisteredMessages(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, LRESULT& res) {
    HWND hwndLookup = hwnd;
    HWND hwndMaybe = GetChildHWNDForMessage(msg, wp, lp);
    if (hwndMaybe != nullptr) {
        hwndLookup = hwndMaybe;
    }
    auto h = FindHandlerForHwndAndMsg(hwndLookup, msg, false);
    if (!h) {
        return false;
    }
    WndEvent ev;
    SetWndEventSimple(ev);
    h->handler(h->user, &ev);
    res = ev.result;
    return ev.didHandle;
}

// to ensure we never overflow control ids
// we reset the counter in Window::Window(),
// because ids only need to be unique within window
// this works as long as we don't interleave creation
// of windows and controls in those windows
void ResetCtrlID() {
    g_currCtrlID = 100;
}

// http://www.guyswithtowels.com/blog/10-things-i-hate-about-win32.html#ModelessDialogs
// to implement a standard dialog navigation we need to call
// IsDialogMessage(hwnd) in message loop.
// hwnd has to be current top-level window that is modeless dialog
// we need to manually maintain this window
HWND g_currentModelessDialog = nullptr;

HWND GetCurrentModelessDialog() {
    return g_currentModelessDialog;
}

// set to nullptr to disable
void SetCurrentModelessDialog(HWND hwnd) {
    g_currentModelessDialog = hwnd;
}

CopyWndEvent::CopyWndEvent(WndEvent* dst, WndEvent* src) {
    this->dst = dst;
    this->src = src;
    dst->hwnd = src->hwnd;
    dst->msg = src->msg;
    dst->lp = src->lp;
    dst->wp = src->wp;
    dst->w = src->w;
}

CopyWndEvent::~CopyWndEvent() {
    src->didHandle = dst->didHandle;
    src->result = dst->result;
}

Kind kindWindowBase = "windowBase";

static LRESULT wndBaseProcDispatch(WindowBase* w, HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, bool& didHandle) {
    CrashIf(hwnd != w->hwnd);

    // or maybe get rid of WindowBase::WndProc and use msgFilterInternal
    // when per-control custom processing is needed
    if (w->msgFilter) {
        WndEvent ev{};
        SetWndEvent(ev);
        w->msgFilter(&ev);
        if (ev.didHandle) {
            didHandle = true;
            return ev.result;
        }
    }

    // https://docs.microsoft.com/en-us/windows/win32/controls/wm-ctlcolorbtn
    if (WM_CTLCOLORBTN == msg) {
        auto bgBrush = w->backgroundColorBrush;
        if (bgBrush != nullptr) {
            didHandle = true;
            return (LRESULT)bgBrush;
        }
    }

    // https://docs.microsoft.com/en-us/windows/win32/controls/wm-ctlcolorstatic
    if (WM_CTLCOLORSTATIC == msg) {
        HDC hdc = (HDC)wp;
        if (w->textColor != ColorUnset) {
            SetTextColor(hdc, w->textColor);
            // SetTextColor(hdc, RGB(255, 255, 255));
            didHandle = true;
        }
        auto bgBrush = w->backgroundColorBrush;
        if (bgBrush != nullptr) {
            SetBkMode(hdc, TRANSPARENT);
            didHandle = true;
            return (LRESULT)bgBrush;
        }
    }

    // https://docs.microsoft.com/en-us/windows/win32/winmsg/wm-size
    if (WM_SIZE == msg) {
        if (!w->onSize) {
            return 0;
        }
        SizeEvent ev;
        SetWndEvent(ev);
        ev.dx = LOWORD(lp);
        ev.dy = HIWORD(lp);
        w->onSize(&ev);
        if (ev.didHandle) {
            didHandle = true;
            return 0;
        }
    }

    // https://docs.microsoft.com/en-us/windows/win32/menurc/wm-command
    if (WM_COMMAND == msg) {
        if (!w->onWmCommand) {
            return 0;
        }
        WmCommandEvent ev{};
        SetWndEvent(ev);
        ev.id = LOWORD(wp);
        ev.ev = HIWORD(wp);
        w->onWmCommand(&ev);
        if (ev.didHandle) {
            didHandle = true;
            return ev.result;
        }
    }

    // https://docs.microsoft.com/en-us/windows/win32/inputdev/wm-keydown
    // https://docs.microsoft.com/en-us/windows/win32/inputdev/wm-keyup
    if ((WM_KEYUP == msg) || (WM_KEYDOWN == msg)) {
        if (!w->onKeyDownUp) {
            return 0;
        }
        KeyEvent ev{};
        SetWndEvent(ev);
        ev.isDown = (WM_KEYDOWN == msg);
        ev.keyVirtCode = (int)wp;
        w->onKeyDownUp(&ev);
        if (ev.didHandle) {
            didHandle = true;
            // 0 means: did handle
            return 0;
        }
    }

    // https://docs.microsoft.com/en-us/windows/win32/inputdev/wm-char
    if (WM_CHAR == msg) {
        if (!w->onChar) {
            return 0;
        }
        CharEvent ev{};
        SetWndEvent(ev);
        ev.keyCode = (int)wp;
        w->onChar(&ev);
        if (ev.didHandle) {
            didHandle = true;
            // 0 means: did handle
            return 0;
        }
    }

    // https://docs.microsoft.com/en-us/windows/win32/inputdev/wm-mousewheel
    if (msg == WM_MOUSEWHEEL || msg == WM_MOUSEHWHEEL) {
        if (!w->onMouseWheel) {
            return 0;
        }
        MouseWheelEvent ev{};
        SetWndEvent(ev);
        ev.isVertical = (msg == WM_MOUSEWHEEL);
        ev.delta = GET_WHEEL_DELTA_WPARAM(wp);
        ev.keys = GET_KEYSTATE_WPARAM(wp);
        ev.x = GET_X_LPARAM(lp);
        ev.y = GET_Y_LPARAM(lp);
        w->onMouseWheel(&ev);
        if (ev.didHandle) {
            didHandle = true;
            return 0;
        }
    }

    // https://docs.microsoft.com/en-us/windows/win32/shell/wm-dropfiles
    if (msg == WM_DROPFILES) {
        if (!w->onDropFiles) {
            return 0;
        }

        DropFilesEvent ev{};
        SetWndEvent(ev);
        ev.hdrop = (HDROP)wp;
        // TODO: docs say it's always zero but sumatra code elsewhere
        // treats 0 and 1 differently
        CrashIf(lp != 0);
        w->onDropFiles(&ev);
        if (ev.didHandle) {
            didHandle = true;
            return 0; // 0 means: did handle
        }
    }

    // handle the rest in WndProc
    WndEvent ev{};
    SetWndEvent(ev);
    w->WndProc(&ev);
    didHandle = ev.didHandle;
    return ev.result;
}

static LRESULT CALLBACK wndProcCustom(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    // auto msgName = GetWinMessageName(msg);
    // logf("hwnd: 0x%6p, msg: 0x%03x (%s), wp: 0x%x\n", hwnd, msg, msgName, wp);

    if (WM_NCCREATE == msg) {
        CREATESTRUCT* cs = (CREATESTRUCT*)lp;
        Window* w = (Window*)cs->lpCreateParams;
        w->hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)w);
        return DefWindowProc(hwnd, msg, wp, lp);
    }

    LRESULT res = 0;
    if (HandleRegisteredMessages(hwnd, msg, wp, lp, res)) {
        return res;
    }

    // TODDO: a hack, a Window might be deleted when we get here
    // happens e.g. when we call CloseWindow() inside
    // wndproc. Maybe instead of calling DestroyWindow()
    // we should delete WindowInfo, for proper shutdown sequence
    if (WM_DESTROY == msg) {
        return DefWindowProc(hwnd, msg, wp, lp);
    }

    Window* w = (Window*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    if (!w) {
        return DefWindowProc(hwnd, msg, wp, lp);
    }

    // this is the last message ever received by hwnd
    // TODO: move it to wndBaseProcDispatch? Maybe they don't
    // need WM_*DESTROY notifications?
    if (WM_NCDESTROY == msg) {
        if (w->onDestroy) {
            WindowDestroyEvent ev{};
            SetWndEvent(ev);
            ev.window = w;
            w->onDestroy(&ev);
            return 0;
        }
        return DefWindowProc(hwnd, msg, wp, lp);
    }

    // TODO: should this go into WindowBase?
    if (WM_CLOSE == msg) {
        if (w->onClose) {
            WindowCloseEvent ev{};
            SetWndEvent(ev);
            w->onClose(&ev);
            if (ev.cancel) {
                return 0;
            }
        }
        return DefWindowProc(hwnd, msg, wp, lp);
    }

    if (w->isDialog) {
        // TODO: should handle more messages as per
        // https://stackoverflow.com/questions/35688400/set-full-focus-on-a-button-setfocus-is-not-enough
        // and https://docs.microsoft.com/en-us/windows/win32/dlgbox/dlgbox-programming-considerations
        if (WM_ACTIVATE == msg) {
            if (wp == 0) {
                // becoming inactive
                SetCurrentModelessDialog(nullptr);
            } else {
                // becoming active
                SetCurrentModelessDialog(w->hwnd);
            }
        }
    }

    if (WM_PAINT == msg) {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        auto bgBrush = w->backgroundColorBrush;
        if (bgBrush != nullptr) {
            FillRect(ps.hdc, &ps.rcPaint, bgBrush);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }

    bool didHandle = false;
    res = wndBaseProcDispatch(w, hwnd, msg, wp, lp, didHandle);
    if (didHandle) {
        return res;
    }
    res = DefWindowProcW(hwnd, msg, wp, lp);
    // auto msgName = GetWinMessageName(msg);
    // logf("hwnd: 0x%6p, msg: 0x%03x (%s), wp: 0x%x, res: 0x%x\n", hwnd, msg, msgName, wp, res);
    return res;
}

// TODO: do I need WM_CTLCOLORSTATIC?
#if 0
    // https://docs.microsoft.com/en-us/windows/win32/controls/wm-ctlcolorstatic
    if (WM_CTLCOLORSTATIC == msg) {
        HDC hdc = (HDC)wp;
        if (w->textColor != ColorUnset) {
            SetTextColor(hdc, w->textColor);
        }
        auto bgBrush = w->backgroundColorBrush;
        if (bgBrush != nullptr) {
            // SetBkMode(hdc, TRANSPARENT);
            return (LRESULT)bgBrush;
        }
    }
#endif

WindowBase::WindowBase(HWND p) {
    kind = kindWindowBase;
    parent = p;
    ctrlID = GetNextCtrlID();
}

WindowBase::~WindowBase() {
    if (GetCWndPtr(*this) == this) // Is window managed by Win32++?
    {
        if (IsWindow())
            ::DestroyWindow(*this);
    }

    RemoveFromMap();

#if 0 // my old code
    if (backgroundColorBrush != nullptr) {
        DeleteObject(backgroundColorBrush);
    }
#endif
}

void WindowBase::WndProc(WndEvent* ev) {
    ev->didHandle = false;
}

Size WindowBase::GetIdealSize() {
    return {};
}

void Handle_WM_CONTEXTMENU(WindowBase* w, WndEvent* ev) {
    CrashIf(ev->msg != WM_CONTEXTMENU);
    CrashIf(!w->onContextMenu);
    // https://docs.microsoft.com/en-us/windows/win32/menurc/wm-contextmenu
    ContextMenuEvent cmev;
    CopyWndEvent cpev(&cmev, ev);
    cmev.w = w;
    cmev.mouseGlobal.x = GET_X_LPARAM(ev->lp);
    cmev.mouseGlobal.y = GET_Y_LPARAM(ev->lp);
    POINT pt{cmev.mouseGlobal.x, cmev.mouseGlobal.y};
    if (pt.x != -1) {
        MapWindowPoints(HWND_DESKTOP, w->hwnd, &pt, 1);
    }
    cmev.mouseWindow.x = pt.x;
    cmev.mouseWindow.y = pt.y;
    w->onContextMenu(&cmev);
    ev->didHandle = true;
}

static void Dispatch_WM_CONTEXTMENU(void* user, WndEvent* ev) {
    WindowBase* w = (WindowBase*)user;
    Handle_WM_CONTEXTMENU(w, ev);
}

bool WindowBase::Create() {
    auto h = GetModuleHandle(nullptr);
    int x = CW_USEDEFAULT;
    if (initialPos.x != -1) {
        x = initialPos.x;
    }
    int y = CW_USEDEFAULT;
    if (initialPos.y != -1) {
        y = initialPos.y;
    }

    int dx = CW_USEDEFAULT;
    if (initialSize.dx > 0) {
        dx = initialSize.dx;
    }
    int dy = CW_USEDEFAULT;
    if (initialSize.dy > 0) {
        dy = initialSize.dy;
    }
    HMENU m = (HMENU)(UINT_PTR)ctrlID;
    hwnd = CreateWindowExW(dwExStyle, winClass, L"", dwStyle, x, y, dx, dy, parent, m, h, nullptr);
    CrashIf(!hwnd);

    if (hwnd == nullptr) {
        return false;
    }

    if (onDropFiles != nullptr) {
        DragAcceptFiles(hwnd, TRUE);
    }

    // TODO: maybe always register so that we can set onContextMenu
    // after creation
    if (onContextMenu) {
        void* user = this;
        RegisterHandlerForMessage(hwnd, WM_CONTEXTMENU, Dispatch_WM_CONTEXTMENU, user);
    }

    if (hfont == nullptr) {
        hfont = GetDefaultGuiFont();
    }
    SetFont(hfont);
    HwndSetText(hwnd, text.AsView());
    return true;
}

void WindowBase::SuspendRedraw() const {
    SendMessageW(hwnd, WM_SETREDRAW, FALSE, 0);
}

void WindowBase::ResumeRedraw() const {
    SendMessageW(hwnd, WM_SETREDRAW, TRUE, 0);
}

void WindowBase::SetFocus() const {
    ::SetFocus(hwnd);
}

bool WindowBase::IsFocused() const {
    BOOL isFocused = ::IsFocused(hwnd);
    return tobool(isFocused);
}

void WindowBase::SetIsEnabled(bool isEnabled) const {
    // TODO: make it work even if not yet created?
    CrashIf(!hwnd);
    BOOL enabled = isEnabled ? TRUE : FALSE;
    ::EnableWindow(hwnd, enabled);
}

bool WindowBase::IsEnabled() const {
    BOOL enabled = ::IsWindowEnabled(hwnd);
    return tobool(enabled);
}

Kind WindowBase::GetKind() {
    return kind;
}

void WindowBase::SetVisibility(Visibility newVisibility) {
    // TODO: make it work before Create()?
    CrashIf(!hwnd);
    visibility = newVisibility;
    bool isVisible = IsVisible();
    // TODO: a different way to determine if is top level vs. child window?
    if (GetParent(hwnd) == nullptr) {
        ::ShowWindow(hwnd, isVisible ? SW_SHOW : SW_HIDE);
    } else {
        BOOL bIsVisible = toBOOL(isVisible);
        SetWindowStyle(hwnd, WS_VISIBLE, bIsVisible);
    }
}

Visibility WindowBase::GetVisibility() {
    return visibility;
#if 0
    if (GetParent(hwnd) == nullptr) {
        // TODO: what to do for top-level window?
        CrashMe();
        return true;
    }
    bool isVisible = IsWindowStyleSet(hwnd, WS_VISIBLE);
    return isVisible;
#endif
}

// convenience function
void WindowBase::SetIsVisible(bool isVisible) {
    SetVisibility(isVisible ? Visibility::Visible : Visibility::Collapse);
}

bool WindowBase::IsVisible() const {
    return visibility == Visibility::Visible;
}

void WindowBase::SetFont(HFONT f) {
    hfont = f;
    HwndSetFont(hwnd, f);
}

HFONT WindowBase::GetFont() const {
    HFONT res = hfont;
    if (!res) {
        res = HwndGetFont(hwnd);
    }
    if (!res) {
        res = GetDefaultGuiFont();
    }
    return res;
}

void WindowBase::SetIcon(HICON iconIn) {
    hIcon = iconIn;
    HwndSetIcon(hwnd, hIcon);
}

HICON WindowBase::GetIcon() const {
    return hIcon;
}

void WindowBase::SetText(const WCHAR* s) {
    auto str = ToUtf8Temp(s);
    SetText(str.AsView());
}

void WindowBase::SetText(std::string_view sv) {
    text.Set(sv);
    // can be set before we create the window
    HwndSetText(hwnd, text.AsView());
    HwndInvalidate(hwnd);
}

std::string_view WindowBase::GetText() {
    auto sw = win::GetTextTemp(hwnd);
    auto sa = ToUtf8Temp(sw.AsView());
    text.Set(sa.AsView());
    return text.AsView();
}

void WindowBase::SetTextColor(COLORREF col) {
    if (ColorNoChange == col) {
        return;
    }
    textColor = col;
    // can be set before we create the window
    if (!hwnd) {
        return;
    }
    InvalidateRect(hwnd, nullptr, FALSE);
}

void WindowBase::SetBackgroundColor(COLORREF col) {
    if (col == ColorNoChange) {
        return;
    }
    backgroundColor = col;
    if (backgroundColorBrush != nullptr) {
        DeleteObject(backgroundColorBrush);
        backgroundColorBrush = nullptr;
    }
    if (backgroundColor != ColorUnset) {
        backgroundColorBrush = CreateSolidBrush(backgroundColor);
    }
    // can be set before we create the window
    if (!hwnd) {
        return;
    }
    InvalidateRect(hwnd, nullptr, FALSE);
}

void WindowBase::SetColors(COLORREF bg, COLORREF txt) {
    SetBackgroundColor(bg);
    SetTextColor(txt);
}

void WindowBase::SetRtl(bool isRtl) const {
    SetWindowExStyle(hwnd, WS_EX_LAYOUTRTL | WS_EX_NOINHERITLAYOUT, isRtl);
}

Kind kindWindow = "window";

struct winClassWithAtom {
    const WCHAR* winClass = nullptr;
    ATOM atom = 0;
};

Vec<winClassWithAtom> gRegisteredClasses;

static void RegisterWindowClass(Window* w) {
    // check if already registered
    for (auto&& ca : gRegisteredClasses) {
        if (str::Eq(ca.winClass, w->winClass)) {
            if (ca.atom != 0) {
                return;
            }
        }
    }
    WNDCLASSEXW wcex = {};
    wcex.cbSize = sizeof(wcex);
    wcex.hIcon = w->hIcon;
    wcex.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wcex.hIconSm = w->hIconSm;
    wcex.lpfnWndProc = wndProcCustom;
    wcex.lpszClassName = w->winClass;
    wcex.lpszMenuName = w->lpszMenuName;
    ATOM atom = RegisterClassExW(&wcex);
    CrashIf(!atom);
    winClassWithAtom ca = {w->winClass, atom};
    gRegisteredClasses.Append(ca);
}

Window::Window() {
    ResetCtrlID();
    kind = kindWindow;
    dwExStyle = 0;
    dwStyle = WS_OVERLAPPEDWINDOW;
    // TODO: at this point parent cannot be set yet
    if (parent == nullptr) {
        dwStyle |= WS_CLIPCHILDREN;
    } else {
        dwStyle |= WS_CHILD;
    }
}

bool Window::Create() {
    if (winClass == nullptr) {
        winClass = DEFAULT_WIN_CLASS;
    }
    RegisterWindowClass(this);

    int x = CW_USEDEFAULT;
    if (initialPos.x != -1) {
        x = initialPos.x;
    }
    int y = CW_USEDEFAULT;
    if (initialPos.y != -1) {
        y = initialPos.y;
    }

    int dx = CW_USEDEFAULT;
    if (initialSize.dx > 0) {
        dx = initialSize.dx;
    }
    int dy = CW_USEDEFAULT;
    if (initialSize.dy > 0) {
        dy = initialSize.dy;
    }
    auto title = ToWstrTemp(this->text.AsView());
    HINSTANCE hinst = GetInstance();
    hwnd = CreateWindowExW(dwExStyle, winClass, title, dwStyle, x, y, dx, dy, parent, nullptr, hinst, (void*)this);
    CrashIf(!hwnd);
    if (!hwnd) {
        return false;
    }
    if (hfont == nullptr) {
        hfont = GetDefaultGuiFont();
    }
    // trigger creating a backgroundBrush
    SetBackgroundColor(backgroundColor);
    SetFont(hfont);
    SetIcon(hIcon);
    HwndSetText(hwnd, text.AsView());
    return true;
}

Window::~Window() = default;

void Window::SetTitle(std::string_view title) {
    SetText(title);
}

void Window::Close() {
    ::SendMessageW(hwnd, WM_CLOSE, 0, 0);
}

// if only top given, set them all to top
// if top, right given, set bottom to top and left to right
void WindowBase::SetInsetsPt(int top, int right, int bottom, int left) {
    insets = DpiScaledInsets(hwnd, top, right, bottom, left);
}

Size WindowBase::Layout(const Constraints bc) {
    dbglayoutf("WindowBase::Layout() %s ", GetKind());
    LogConstraints(bc, "\n");

    auto hinset = insets.left + insets.right;
    auto vinset = insets.top + insets.bottom;
    auto innerConstraints = bc.Inset(hinset, vinset);

    int dx = MinIntrinsicWidth(0);
    int dy = MinIntrinsicHeight(0);
    childSize = innerConstraints.Constrain(Size{dx, dy});
    auto res = Size{
        childSize.dx + hinset,
        childSize.dy + vinset,
    };
    return res;
}

int WindowBase::MinIntrinsicHeight(int) {
#if 0
    auto vinset = insets.top + insets.bottom;
    Size s = GetIdealSize();
    return s.dy + vinset;
#else
    Size s = GetIdealSize();
    return s.dy;
#endif
}

int WindowBase::MinIntrinsicWidth(int) {
#if 0
    auto hinset = insets.left + insets.right;
    Size s = GetIdealSize();
    return s.dx + hinset;
#else
    Size s = GetIdealSize();
    return s.dx;
#endif
}

void WindowBase::SetPos(RECT* r) const {
    ::MoveWindow(hwnd, r);
}

void WindowBase::SetBounds(Rect bounds) {
    dbglayoutf("WindowBaseLayout:SetBounds() %s %d,%d - %d, %d\n", GetKind(), bounds.x, bounds.y, bounds.dx, bounds.dy);

    lastBounds = bounds;

    bounds.x += insets.left;
    bounds.y += insets.top;
    bounds.dx -= (insets.right + insets.left);
    bounds.dy -= (insets.bottom + insets.top);

    auto r = RectToRECT(bounds);
    ::MoveWindow(hwnd, &r);
    // TODO: optimize if doesn't change position
    ::InvalidateRect(hwnd, nullptr, TRUE);
}

#if 0
void WindowBase::SetBounds(const RECT& r) const {
    SetPos((RECT*)&r);
}
#endif

int RunMessageLoop(HACCEL accelTable, HWND hwndDialog) {
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        if (TranslateAccelerator(msg.hwnd, accelTable, &msg)) {
            continue;
        }
        if (hwndDialog && IsDialogMessage(hwndDialog, &msg)) {
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return (int)msg.wParam;
}

// TODO: support accelerator table?
// TODO: a better way to stop the loop e.g. via shared
// atomic int to signal termination and sending WM_IDLE
// to trigger processing of the loop
void RunModalWindow(HWND hwndDialog, HWND hwndParent) {
    if (hwndParent != nullptr) {
        EnableWindow(hwndParent, FALSE);
    }

    MSG msg;
    bool isFinished{false};
    while (!isFinished) {
        BOOL ok = WaitMessage();
        if (!ok) {
            DWORD err = GetLastError();
            LogLastError(err);
            isFinished = true;
            continue;
        }
        while (!isFinished && PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                isFinished = true;
                break;
            }
            if (!IsDialogMessage(hwndDialog, &msg)) {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        }
    }

    if (hwndParent != nullptr) {
        EnableWindow(hwndParent, TRUE);
    }
}

// sets initial position of w within hwnd. Assumes w->initialSize is set.
void PositionCloseTo(WindowBase* w, HWND hwnd) {
    CrashIf(!hwnd);
    Size is = w->initialSize;
    CrashIf(is.IsEmpty());
    RECT r{};
    BOOL ok = GetWindowRect(hwnd, &r);
    CrashIf(!ok);

    // position w in the the center of hwnd
    // if window is bigger than hwnd, let the system position
    // we don't want to hide it
    int offX = (RectDx(r) - is.dx) / 2;
    if (offX < 0) {
        return;
    }
    int offY = (RectDy(r) - is.dy) / 2;
    if (offY < 0) {
        return;
    }
    Point& ip = w->initialPos;
    ip.x = (int)r.left + (int)offX;
    ip.y = (int)r.top + (int)offY;
}

struct HwndToWindowBase {
    HWND hwnd;
    WindowBase* win;
};

Vec<HwndToWindowBase> gHwndToWindowBase;

WindowBase* GetCWndFromMap(HWND hwnd) {
    for (auto& e : gHwndToWindowBase) {
        if (e.hwnd == hwnd) {
            return e.win;
        }
    }
    return nullptr;
}

bool RemoveWindowFromMap(WindowBase* win) {
    int n = gHwndToWindowBase.isize();
    for (int i = 0; i < n; i++) {
        auto& e = gHwndToWindowBase[i];
        if (e.win == win) {
            gHwndToWindowBase.RemoveAtFast((size_t)i);
            return true;
        }
    }
    return false;
}

void AddHwndToMap(HWND hwnd, WindowBase* win) {
    // This HWND is should not be in the map yet
    assert(0 == GetCWndFromMap(hwnd));

    // Remove any old map entry for this CWnd (required when the CWnd is reused).
    RemoveWindowFromMap(win);

    // TOOD: lock
    HwndToWindowBase el{hwnd, win};
    gHwndToWindowBase.Append(el);
}

// Store the window handle and CWnd pointer in the HWND map.
void WindowBase::AddToMap() {
    AddHwndToMap(hwnd, this);
}

// Removes this CWnd's pointer from the application's map.
// TODO: remove
bool WindowBase::RemoveFromMap() {
    return RemoveWindowFromMap(this);
}

__declspec(thread) WindowBase* gCurrWindowBase;

// All Window windows direct their messages here. This function redirects the message
// to the CWnd's WndProc function.
LRESULT CALLBACK WindowBase::StaticWindowProc(HWND wnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    auto w = GetCWndFromMap(wnd);
    if (w == 0) {
        // The CWnd pointer wasn't found in the map, so add it now.
        // Retrieve pointer to CWnd object from Thread Local Storage TLS.
        w = gCurrWindowBase;
        if (w) {
            gCurrWindowBase = NULL;

            // Store the CWnd pointer in the HWND map.
            w->hwnd = wnd;
            w->AddToMap();
        }
    }

    if (w == 0) {
        // Got a message for a window that's not in the map.
        // We should never get here.
        // TRACE("*** Warning in CWnd::StaticWindowProc: HWND not in window map ***\n");
        return 0;
    }

    return w->WndProc(msg, wparam, lparam);
}

// A private function used by CreateEx, Attach and AttachDlgItem.
void WindowBase::Subclass(HWND wnd) {
    assert(::IsWindow(wnd));

    this->hwnd = wnd;
    AddToMap(); // Store the CWnd pointer in the HWND map
    LONG_PTR pWndProc = reinterpret_cast<LONG_PTR>(Window::StaticWindowProc);
    LONG_PTR pRes = ::SetWindowLongPtr(wnd, GWLP_WNDPROC, pWndProc);
    m_prevWindowProc = reinterpret_cast<WNDPROC>(pRes);
}

// Pass messages on to the appropriate default window procedure
// CMDIChild and CMDIFrame override this function.
LRESULT WindowBase::FinalWindowProc(UINT msg, WPARAM wparam, LPARAM lparam) {
    if (m_prevWindowProc)
        return ::CallWindowProc(m_prevWindowProc, *this, msg, wparam, lparam);
    else
        return ::DefWindowProc(*this, msg, wparam, lparam);
}

// Retrieves the pointer to the CWnd associated with the specified HWND.
// Returns NULL if a CWnd object doesn't already exist for this HWND.
WindowBase* WindowBase::GetCWndPtr(HWND wnd) {
    return wnd ? GetCWndFromMap(wnd) : 0;
}

// Processes this window's message. Override this function in your class
// derived from CWnd to handle window messages.
LRESULT WindowBase::WndProc(UINT msg, WPARAM wparam, LPARAM lparam) {
    //  A typical function might look like this:

    //  switch (msg)
    //  {
    //  case MESSAGE1:  return OnMessage1();
    //  case MESSAGE2:  return OnMessage2();
    //  }

    // The message functions should return a value recommended by the Windows API documentation.
    // Alternatively, return FinalWindowProc to continue with default processing.

    // Always pass unhandled messages on to WndProcDefault
    return WndProcDefault(msg, wparam, lparam);
}

// Provides default processing for this window's messages.
// All WndProc functions should pass unhandled window messages to this function.
LRESULT WindowBase::WndProcDefault(UINT msg, WPARAM wparam, LPARAM lparam) {
    LRESULT result = 0;
    if (UWM_WINDOWCREATED == msg) {
        OnInitialUpdate();
        return 0;
    }

    switch (msg) {
        case WM_CLOSE: {
            OnClose();
            return 0;
        }
        case WM_COMMAND: {
            // Reflect this message if it's from a control.
            CWnd* pWnd = GetCWndPtr(reinterpret_cast<HWND>(lparam));
            if (pWnd != NULL)
                result = pWnd->OnCommand(wparam, lparam);

            // Handle user commands.
            if (0 == result)
                result = OnCommand(wparam, lparam);

            if (0 != result)
                return 0;
        } break; // Note: Some MDI commands require default processing.
        case WM_CREATE: {
            LPCREATESTRUCT pcs = reinterpret_cast<LPCREATESTRUCT>(lparam);
            if (pcs == NULL) {
                // throw CWinException(_T("WM_CREATE failed"));
                CrashAlwaysIf(true);
                return 0;
            }

            return OnCreate(*pcs);
        }
        case WM_DESTROY:
            OnDestroy();
            break; // Note: Some controls require default processing.
        case WM_NOTIFY: {
            // Do notification reflection if message came from a child window.
            // Restricting OnNotifyReflect to child windows avoids double handling.
            LPNMHDR pHeader = reinterpret_cast<LPNMHDR>(lparam);
            HWND from = pHeader->hwndFrom;
            CWnd* pWndFrom = GetCWndFromMap(from);

            if (pWndFrom != NULL)
                if (::GetParent(from) == hwnd)
                    result = pWndFrom->OnNotifyReflect(wparam, lparam);

            // Handle user notifications
            if (result == 0)
                result = OnNotify(wparam, lparam);
            if (result != 0)
                return result;
            break;
        }

        case WM_PAINT: {
            // OnPaint calls OnDraw when appropriate.
            OnPaint(msg, wparam, lparam);
        }

            return 0;

        case WM_ERASEBKGND: {
            CDC dc(reinterpret_cast<HDC>(wparam));
            BOOL preventErasure;

            preventErasure = OnEraseBkgnd(dc);
            if (preventErasure)
                return TRUE;
        } break;

        // A set of messages to be reflected back to the control that generated them.
        case WM_CTLCOLORBTN:
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORDLG:
        case WM_CTLCOLORLISTBOX:
        case WM_CTLCOLORSCROLLBAR:
        case WM_CTLCOLORSTATIC:
        case WM_DRAWITEM:
        case WM_MEASUREITEM:
        case WM_DELETEITEM:
        case WM_COMPAREITEM:
        case WM_CHARTOITEM:
        case WM_VKEYTOITEM:
        case WM_HSCROLL:
        case WM_VSCROLL:
        case WM_PARENTNOTIFY: {
            result = MessageReflect(msg, wparam, lparam);
            if (result != 0)
                return result; // Message processed so return.
        } break;               // Do default processing when message not already processed.

        case UWM_UPDATECOMMAND:
            OnMenuUpdate(static_cast<UINT>(wparam)); // Perform menu updates.
            break;

        case UWM_GETCWND: {
            // assert(this == GetCWndPtr(m_wnd));
            return reinterpret_cast<LRESULT>(this);
        }

    } // switch (msg)

    // Now hand all messages to the default procedure.
    return FinalWindowProc(msg, wparam, lparam);

} // LRESULT CWnd::WindowProc(...)

// Called when menu items are about to be displayed. Override this function to
// enable/disable the menu item, or add/remove the check box or radio button
// to menu items.
void CWnd::OnMenuUpdate(UINT) {
    // Override this function to modify the behavior of menu items,
    // such as adding or removing checkmarks.
}

CDC::CDC() {
    // Allocate memory for our data members
    m_pData = new CDC_Data;
}

// This constructor assigns a pre-existing HDC to the CDC.
// The HDC will NOT be released or deleted when the CDC object is destroyed.
// Note: this constructor permits a call like this:
// CDC MyCDC = SomeHDC;
CDC::CDC(HDC dc) {
    m_pData = new CDC_Data;
    // TODO: fix me
    CrashAlwaysIf(true);
    // Attach(dc);
}

// Processes notification (WM_NOTIFY) messages from a child window.
LRESULT CWnd::OnNotify(WPARAM, LPARAM) {
    // You can use either OnNotifyReflect or OnNotify to handle notifications
    // Override OnNotifyReflect to handle notifications in the CWnd class that
    //   generated the notification.   OR
    // Override OnNotify to handle notifications in the PARENT of the CWnd class
    //   that generated the notification.

    // Your overriding function should look like this ...

    // LPNMHDR pHeader = reinterpret_cast<LPNMHDR>(lparam);
    // switch (pHeader->code)
    // {
    //      Handle your notifications from the CHILD window here
    //      Return the value recommended by the Windows API documentation.
    //      For many notifications, the return value doesn't matter, but for some it does.
    // }

    // return 0 for unhandled notifications
    // The framework will call SetWindowLongPtr(DWLP_MSGRESULT, result) for dialogs.
    return 0;
}

// Called when the background of the window's client area needs to be erased.
// Override this function in your derived class to perform drawing tasks.
// Return Value: Return FALSE to also permit default erasure of the background
//               Return TRUE to prevent default erasure of the background
bool CWnd::OnEraseBkgnd(CDC&) {
    return FALSE;
}

// This function is called automatically once the window is created
// Override it in your derived class to automatically perform tasks
// after window creation.
void CWnd::OnInitialUpdate() {
}

// Called in response to WM_CLOSE, before the window is destroyed.
// Override this function to suppress destroying the window.
// WM_CLOSE is sent by SendMessage(WM_CLOSE, 0, 0) or by clicking X
//  in the top right corner.
// Child windows don't receive WM_CLOSE unless they are closed using
//  the Close function.
void CWnd::OnClose() {
    Destroy();
}

// Destroys the window and returns the CWnd back to its default state,
//  ready for reuse.
void CWnd::Destroy() {
    if (GetCWndPtr(*this) == this) {
        if (IsWindow())
            ::DestroyWindow(*this);
    }

    // Return the CWnd to its default state.
    Cleanup();
}

// Called when the window paints its client area.
LRESULT CWnd::OnPaint(UINT msg, WPARAM wparam, LPARAM lparam) {
    // Window controls and other subclassed windows are expected to do their own
    // drawing, so we don't call OnDraw for those.

    // Note: CustomDraw or OwnerDraw are normally used to modify the drawing of
    //       controls, but overriding OnPaint is also an option.

    if (!m_prevWindowProc) {
        if (::GetUpdateRect(*this, NULL, FALSE)) {
            CrashMe();
            // CPaintDC dc(*this);
            // OnDraw(dc);
        } else
        // RedrawWindow can require repainting without an update rect.
        {
            CrashMe();
            // CClientDC dc(*this);
            // OnDraw(dc);
        }

        // No more drawing required
        return 0;
    }

    // Allow window controls to do their default drawing.
    return FinalWindowProc(msg, wparam, lparam);
}

// A function used internally to call OnMessageReflect. Don't call or override this function.
LRESULT CWnd::MessageReflect(UINT msg, WPARAM wparam, LPARAM lparam) {
    HWND wnd = 0;
    switch (msg) {
        case WM_COMMAND:
        case WM_CTLCOLORBTN:
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORDLG:
        case WM_CTLCOLORLISTBOX:
        case WM_CTLCOLORSCROLLBAR:
        case WM_CTLCOLORSTATIC:
        case WM_CHARTOITEM:
        case WM_VKEYTOITEM:
        case WM_HSCROLL:
        case WM_VSCROLL:
            wnd = reinterpret_cast<HWND>(lparam);
            break;

        case WM_DRAWITEM:
        case WM_MEASUREITEM:
        case WM_DELETEITEM:
        case WM_COMPAREITEM:
            wnd = ::GetDlgItem(hwnd, static_cast<int>(wparam));
            break;

        case WM_PARENTNOTIFY:
            switch (LOWORD(wparam)) {
                case WM_CREATE:
                case WM_DESTROY:
                    wnd = reinterpret_cast<HWND>(lparam);
                    break;
            }
    }

    CWnd* pWnd = GetCWndFromMap(wnd);

    if (pWnd != NULL)
        return pWnd->OnMessageReflect(msg, wparam, lparam);

    return 0;
}

// This function processes those special messages sent by some older controls,
// and reflects them back to the originating CWnd object.
// Override this function in your derived class to handle these special messages:
// WM_COMMAND, WM_CTLCOLORBTN, WM_CTLCOLOREDIT, WM_CTLCOLORDLG, WM_CTLCOLORLISTBOX,
// WM_CTLCOLORSCROLLBAR, WM_CTLCOLORSTATIC, WM_CHARTOITEM,  WM_VKEYTOITEM,
// WM_HSCROLL, WM_VSCROLL, WM_DRAWITEM, WM_MEASUREITEM, WM_DELETEITEM,
// WM_COMPAREITEM, WM_PARENTNOTIFY.
LRESULT CWnd::OnMessageReflect(UINT, WPARAM, LPARAM) {
    // This function processes those special messages (see above) sent
    // by some older controls, and reflects them back to the originating CWnd object.
    // Override this function in your derived class to handle these special messages.

    // Your overriding function should look like this ...

    // switch (msg)
    // {
    //      Handle your reflected messages here
    // }

    // return 0 for unhandled messages
    return 0;
}

// The GetDlgItem function retrieves a handle to a control in the dialog box.
// Refer to GetDlgItem in the Windows API documentation for more information.
CWnd CWnd::GetDlgItem(int dlgItemID) const {
    CrashIf(!IsWindow());
    return CWnd(::GetDlgItem(*this, dlgItemID));
}

// The IsWindow function determines whether the window exists.
// Refer to IsWindow in the Windows API documentation for more information.
bool CWnd::IsWindow() const {
    return ::IsWindow(*this);
}

// Returns the CWnd to its default state.
void CWnd::Cleanup() {
    CrashIf(!IsWindow());
    if (!IsWindow()) {
        RemoveFromMap();
        hwnd = 0;
        m_prevWindowProc = 0;
    }
}

// Called when the user interacts with the menu or toolbar.
bool CWnd::OnCommand(WPARAM, LPARAM) {
    // Override this to handle WM_COMMAND messages, for example

    //  UINT id = LOWORD(wparam);
    //  switch (id)
    //  {
    //  case IDM_FILE_NEW:
    //      OnFileNew();
    //      TRUE;   // return TRUE for handled commands
    //  }

    // return FALSE for unhandled commands
    return false;
}

// Called during window creation. Override this functions to perform tasks
// such as creating child windows.
int CWnd::OnCreate(CREATESTRUCT&) {
    // This function is called when a WM_CREATE message is received
    // Override it to automatically perform tasks during window creation.
    // Return 0 to continue creating the window.

    // Note: Window controls don't call OnCreate. They are sublcassed (attached)
    //  after their window is created.

    return 0;
}

// This function is called when a window is destroyed.
// Override it to do additional tasks, such as ending the application
//  with PostQuitMessage.
void CWnd::OnDestroy() {
}

// Processes the notification (WM_NOTIFY) messages in the child window that originated them.
LRESULT CWnd::OnNotifyReflect(WPARAM, LPARAM) {
    // Override OnNotifyReflect to handle notifications in the CWnd class that
    //   generated the notification.

    // Your overriding function should look like this ...

    // LPNMHDR pHeader = reinterpret_cast<LPNMHDR>(lparam);
    // switch (pHeader->code)
    // {
    //      Handle your notifications from this window here
    //      Return the value recommended by the Windows API documentation.
    // }

    // Return 0 for unhandled notifications.
    // The framework will call SetWindowLongPtr(DWLP_MSGRESULT, result) for dialogs.
    return 0;
}
