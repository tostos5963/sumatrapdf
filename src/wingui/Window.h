
/* Copyright 2021 the SumatraPDF project authors (see AUTHORS file).
   License: Simplified BSD (see COPYING.BSD) */

struct WindowBase;
struct Window;

// A structure that contains the data members for CDC.
struct CDC_Data {
    // Constructor
    CDC_Data() : dc(0), count(1L), isManagedHDC(FALSE), wnd(0), savedDCState(0), isPaintDC(false) {
        ZeroMemory(&ps, sizeof(ps));
    }

    // CBitmap bitmap;
    //  CBrush brush;
    //  CFont font;
    //  CPalette palette;
    //  CPen pen;
    //  CRgn rgn;
    HDC dc;            // The HDC belonging to this CDC
    long count;        // Reference count
    bool isManagedHDC; // Delete/Release the HDC on destruction
    HWND wnd;          // The HWND of a Window or Client window DC
    int savedDCState;  // The save state of the HDC.
    bool isPaintDC;
    PAINTSTRUCT ps;
};

struct CDC {
    CDC();       // Constructs a new CDC without assigning a HDC
    CDC(HDC dc); // Constructs a new CDC and assigns a HDC
    virtual ~CDC();
    operator HDC() const {
        return m_pData->dc;
    } // Converts a CDC to a HDC

    CDC_Data* m_pData; // pointer to the class's data members
};

struct WndEvent {
    // args sent to WndProc
    HWND hwnd = nullptr;
    UINT msg = 0;
    WPARAM wp = 0;
    LPARAM lp = 0;

    // indicate if we handled the message and the result (if handled)
    bool didHandle = false;
    LRESULT result = 0;

    // window that logically received the message
    // (we reflect messages sent to parent windows back to real window)
    WindowBase* w = nullptr;
};

void RegisterHandlerForMessage(HWND hwnd, UINT msg, void (*handler)(void* user, WndEvent*), void* user);
void UnregisterHandlerForMessage(HWND hwnd, UINT msg);
bool HandleRegisteredMessages(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, LRESULT& res);

#define SetWndEvent(n) \
    {                  \
        n.w = w;       \
        n.hwnd = hwnd; \
        n.msg = msg;   \
        n.wp = wp;     \
        n.lp = lp;     \
    }

#define SetWndEventSimple(n) \
    {                        \
        n.hwnd = hwnd;       \
        n.msg = msg;         \
        n.wp = wp;           \
        n.lp = lp;           \
    }

struct CopyWndEvent {
    WndEvent* dst = nullptr;
    WndEvent* src = nullptr;

    CopyWndEvent() = delete;

    CopyWndEvent(CopyWndEvent&) = delete;
    CopyWndEvent(CopyWndEvent&&) = delete;
    CopyWndEvent& operator=(CopyWndEvent&) = delete;

    CopyWndEvent(WndEvent* dst, WndEvent* src);
    ~CopyWndEvent();
};

using MsgFilter = std::function<void(WndEvent*)>;

struct SizeEvent : WndEvent {
    int dx = 0;
    int dy = 0;
};

using SizeHandler = std::function<void(SizeEvent*)>;

struct ContextMenuEvent : WndEvent {
    // mouse x,y position relative to the window
    Point mouseWindow{};
    // global (screen) mouse x,y position
    Point mouseGlobal{};
};

using ContextMenuHandler = std::function<void(ContextMenuEvent*)>;

struct WindowCloseEvent : WndEvent {
    bool cancel = false;
};

struct WmCommandEvent : WndEvent {
    int id = 0;
    int ev = 0;
};

using WmCommandHandler = std::function<void(WmCommandEvent*)>;

struct WmNotifyEvent : WndEvent {
    NMTREEVIEWW* treeView = nullptr;
};

using WmNotifyHandler = std::function<void(WmNotifyEvent*)>;

using CloseHandler = std::function<void(WindowCloseEvent*)>;

struct WindowDestroyEvent : WndEvent {
    Window* window = nullptr;
};

using DestroyHandler = std::function<void(WindowDestroyEvent*)>;

struct CharEvent : WndEvent {
    int keyCode = 0;
};

using CharHandler = std::function<void(CharEvent*)>;

// TODO: extract data from LPARAM
struct KeyEvent : WndEvent {
    bool isDown = false;
    int keyVirtCode = 0;
};

using KeyHandler = std::function<void(KeyEvent*)>;

struct MouseWheelEvent : WndEvent {
    bool isVertical = false;
    int delta = 0;
    u32 keys = 0;
    int x = 0;
    int y = 0;
};

using MouseWheelHandler = std::function<void(MouseWheelEvent*)>;

// https://docs.microsoft.com/en-us/windows/win32/api/shellapi/nf-shellapi-dragacceptfiles
struct DropFilesEvent : WndEvent {
    HDROP hdrop = nullptr;
};

using DropFilesHandler = std::function<void(DropFilesEvent*)>;

struct WindowBase;
using CWnd = WindowBase;

struct WindowBase : public ILayout {
    Kind kind{nullptr};

    Insets insets{};
    Size childSize{};
    Rect lastBounds{};

    // data that can be set before calling Create()
    Visibility visibility{Visibility::Visible};

    // either a custom class that we registered or
    // a win32 control class. Assumed static so not freed
    const WCHAR* winClass{nullptr};

    HWND parent{nullptr};
    Point initialPos{-1, -1};
    Size initialSize{0, 0};
    DWORD dwStyle{0};
    DWORD dwExStyle{0};
    HFONT hfont{nullptr}; // TODO: this should be abstract Font description

    // those tweak WNDCLASSEX for RegisterClass() class
    HICON hIcon{nullptr};
    HICON hIconSm{nullptr};
    LPCWSTR lpszMenuName{nullptr};

    int ctrlID{0};

    // called at start of windows proc to allow intercepting messages
    MsgFilter msgFilter;

    // allow handling WM_CONTEXTMENU. Must be set before Create()
    ContextMenuHandler onContextMenu{nullptr};
    // allow handling WM_SIZE
    SizeHandler onSize{nullptr};
    // for WM_COMMAND
    WmCommandHandler onWmCommand{nullptr};
    // for WM_NCDESTROY
    DestroyHandler onDestroy{nullptr};
    // for WM_CLOSE
    CloseHandler onClose{nullptr};
    // for WM_KEYDOWN / WM_KEYUP
    KeyHandler onKeyDownUp{nullptr};
    // for WM_CHAR
    CharHandler onChar{nullptr};
    // for WM_MOUSEWHEEL and WM_MOUSEHWHEEL
    MouseWheelHandler onMouseWheel{nullptr};
    // for WM_DROPFILES
    // when set after Create() must also call DragAcceptFiles(hwnd, TRUE);
    DropFilesHandler onDropFiles{nullptr};

    COLORREF textColor{ColorUnset};
    COLORREF backgroundColor{ColorUnset};
    HBRUSH backgroundColorBrush{nullptr};

    str::Str text;

    HWND hwnd{nullptr};

    WindowBase() = default;
    explicit WindowBase(HWND p);
    ~WindowBase() override;

    virtual bool Create();
    virtual Size GetIdealSize();

    virtual void WndProc(WndEvent*);

    // ILayout
    Kind GetKind() override;
    void SetVisibility(Visibility) override;
    Visibility GetVisibility() override;
    int MinIntrinsicHeight(int width) override;
    int MinIntrinsicWidth(int height) override;
    Size Layout(Constraints bc) override;
    void SetBounds(Rect) override;
    void SetInsetsPt(int top, int right = -1, int bottom = -1, int left = -1);

    void SetIsEnabled(bool) const;
    bool IsEnabled() const;

    void SetIsVisible(bool);
    [[nodiscard]] bool IsVisible() const;

    void SuspendRedraw() const;
    void ResumeRedraw() const;

    void SetFocus() const;
    bool IsFocused() const;

    void SetFont(HFONT f);
    [[nodiscard]] HFONT GetFont() const;

    void SetIcon(HICON);
    [[nodiscard]] HICON GetIcon() const;

    void SetText(const WCHAR* s);
    void SetText(std::string_view);
    std::string_view GetText();

    void SetPos(RECT* r) const;
    // void SetBounds(const RECT& r) const;
    void SetTextColor(COLORREF);
    void SetBackgroundColor(COLORREF);
    void SetColors(COLORREF bg, COLORREF txt);
    void SetRtl(bool) const;

    // from win32-framework
    WNDPROC m_prevWindowProc;

    void Subclass(HWND wnd);
    void AddToMap();
    bool RemoveFromMap();
    void Cleanup();
    static WindowBase* GetCWndPtr(HWND wnd);

    static LRESULT CALLBACK StaticWindowProc(HWND wnd, UINT msg, WPARAM wparam, LPARAM lparam);
    operator HWND() const {
        return GetHwnd();
    }
    HWND GetHwnd() const {
        return hwnd;
    }
    WNDPROC GetPrevWindowProc() const {
        return m_prevWindowProc;
    }

    bool IsWindow() const;
    CWnd GetDlgItem(int dlgItemID) const;

    // Not intended to be overridden
    virtual LRESULT WndProcDefault(UINT msg, WPARAM wparam, LPARAM lparam);

    virtual LRESULT WndProc(UINT msg, WPARAM wparam, LPARAM lparam);

    // Override these functions as required
    virtual LRESULT FinalWindowProc(UINT msg, WPARAM wparam, LPARAM lparam);
    virtual void OnInitialUpdate();
    virtual int OnCreate(CREATESTRUCT& cs);
    virtual void OnDestroy();
    virtual void OnClose();
    virtual void Destroy();
    virtual bool OnCommand(WPARAM wparam, LPARAM lparam);
    virtual LRESULT OnNotifyReflect(WPARAM wparam, LPARAM lparam);
    virtual LRESULT OnNotify(WPARAM wparam, LPARAM lparam);
    virtual LRESULT OnPaint(UINT msg, WPARAM wparam, LPARAM lparam);
    virtual bool OnEraseBkgnd(CDC& dc);
    virtual LRESULT OnMessageReflect(UINT msg, WPARAM wparam, LPARAM lparam);
    LRESULT MessageReflect(UINT msg, WPARAM wparam, LPARAM lparam);
    virtual void OnMenuUpdate(UINT id);
};

// Registered messages defined by Win32++
const UINT UWM_WINDOWCREATED =
    ::RegisterWindowMessageA("UWM_WINDOWCREATED"); // Posted when a window is created or attached.

// Messages defined by Win32++
// WM_APP range: 0x8000 through 0xBFFF
// Note: The numbers defined for window messages don't always need to be unique. View windows defined by users for
// example,
//  could use other user defined messages with the same number as those below without issue.
#define UWM_UPDATECOMMAND (WM_APP + 0x3F18) // Message - sent before a menu is displayed. Used by OnMenuUpdate.
#define UWM_GETCWND (WM_APP + 0x3F0C)       // Message - returns a pointer to this CWnd.

void Handle_WM_CONTEXTMENU(WindowBase* w, WndEvent* ev);

// a top-level window. Must set winClass before
// calling Create()
struct Window : WindowBase {
    bool isDialog = false;

    Window();
    ~Window() override;

    bool Create() override;

    void SetTitle(std::string_view);

    void Close();
};

int RunMessageLoop(HACCEL accelTable, HWND hwndDialog);
void RunModalWindow(HWND hwndDialog, HWND hwndParent);
void PositionCloseTo(WindowBase* w, HWND hwnd);
int GetNextCtrlID();
HWND GetCurrentModelessDialog();
void SetCurrentModelessDialog(HWND);
