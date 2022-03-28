#define NOMINMAX
#include <wxx_appcore.h>        // Add CWinApp
#include <wxx_controls.h>       // Add CAnimation, CComboBox, CComboBoxEx, CDateTime, CHeader, CHotKey, CIPAddress,
#include <wxx_dialog.h>         // Add CDialog, CResizer
#include <wxx_richedit.h>       // Add CRichEdit

#include "test-app.h"
#include "utils/BaseUtil.h"

#include "wingui/WinGui.h"
#include "wingui/Layout.h"
#include "wingui/Window.h"
#include "wingui/ButtonCtrl.h"

#include "resource.h"

// in TestTab.cpp
extern int TestTab(HINSTANCE hInstance, int nCmdShow);
// in TestLayout.cpp
extern int TestLayout(HINSTANCE hInstance, int nCmdShow);
// in TestLice.cpp
extern int TestLice(HINSTANCE hInstance, int nCmdShow);

HINSTANCE gHinst = nullptr;

static void LaunchTabs() {
    TestTab(gHinst, SW_SHOW);
}

static void LaunchLayout() {
    TestLayout(gHinst, SW_SHOW);
}

/*
static void LaunchLice() {
    TestLice(gHinst, SW_SHOW);
}
*/

static ILayout* CreateMainLayout(HWND hwnd) {
    auto* vbox = new VBox();

    vbox->alignMain = MainAxisAlign::MainCenter;
    vbox->alignCross = CrossAxisAlign::CrossCenter;

    {
        auto b = CreateButton(hwnd, "Tabs test", LaunchTabs);
        vbox->AddChild(b);
    }

    {
        auto b = CreateButton(hwnd, "Layout test", LaunchLayout);
        vbox->AddChild(b);
    }

    /*
    {
        auto b = CreateButton(hwnd, "Lice test", LaunchLice);
        vbox->AddChild(b);
    }
    */
    auto padding = new Padding(vbox, DefaultInsets());
    return padding;
}

namespace TestWin32Framework1 {

class CMyDialog : public CDialog {
  public:
    CMyDialog(UINT resID);
    virtual ~CMyDialog();

  protected:
    virtual void OnDestroy();
    virtual BOOL OnInitDialog();
    virtual INT_PTR DialogProc(UINT msg, WPARAM wparam, LPARAM lparam);
    virtual BOOL OnCommand(WPARAM wparam, LPARAM lparam);
    virtual BOOL OnEraseBkgnd(CDC& dc);
    virtual void OnOK();

  private:
    BOOL OnButtonClose();
    BOOL OnButtonGetFontList();

    CResizer m_resizer;

    CButton m_buttonClose;
    CButton m_buttonGetFontList;
    CRichEdit m_richEdit;
};

CMyDialog::CMyDialog(UINT resID) : CDialog(resID) {
}

// Destructor.
CMyDialog::~CMyDialog() {
}

// Process the dialog's window messages.
INT_PTR CMyDialog::DialogProc(UINT msg, WPARAM wparam, LPARAM lparam) {
    // Pass resizing messages on to the resizer.
    m_resizer.HandleMessage(msg, wparam, lparam);

    //  switch (msg)
    //  {
    //  Additional messages to be handled go here.
    //  }

    // Pass unhandled messages on to parent DialogProc.
    return DialogProcDefault(msg, wparam, lparam);
}

// Process the dialog's command messages(WM_COMMAND)
BOOL CMyDialog::OnCommand(WPARAM wparam, LPARAM) {
    UINT id = LOWORD(wparam);
    switch (id) {
        case IDC_BTN_CLOSE:
            return OnButtonClose();
        case IDC_BTN_GET_FONT_LIST:
            return OnButtonGetFontList();
    }

    return FALSE;
}

void CMyDialog::OnDestroy() {
    ::PostQuitMessage(0);
}

struct StringStreamInInfo {
    const char* s;
    int sLen;
    int currPos = 0;
};

// streaming stops when returns non-zero or *pcb == 0
DWORD CALLBACK StreamInStringCallback(DWORD_PTR dwCookie, LPBYTE pbBuff, LONG cb, LONG* pcb) {
    if (!cb) {
        return 1;
    }
    
    auto si = (StringStreamInInfo*)dwCookie;
    if (si->currPos >= si->sLen) {
        *pcb = 0;
        return 0;
    }
    int left = si->sLen - si->currPos;
    int toSend = cb;
    if (cb > left) {
        toSend = left;
    }
    const char* s2 = si->s + si->currPos;
    memcpy(pbBuff, s2, toSend);
    *pcb = toSend;
    si->currPos += toSend;
    return 0;
}

LRESULT RichEditStreamInString(HWND hwndEdit, UINT format, const char* s) {
    StringStreamInInfo si;
    si.s = s;
    si.sLen = str::Len(s);
    EDITSTREAM es;
    es.dwError = 0;
    es.pfnCallback = StreamInStringCallback;
    es.dwCookie = (DWORD_PTR)&si;
    return SendMessageW(hwndEdit, EM_STREAMIN, (WPARAM)format, (LPARAM)&es);
} 

BOOL CMyDialog::OnInitDialog() {
    // Set the Icon
    //SetIconLarge(IDW_MAIN);
    //SetIconSmall(IDW_MAIN);

    // Attach CWnd objects to the dialog items
    AttachItem(IDC_BTN_CLOSE, m_buttonClose);
    AttachItem(IDC_BTN_GET_FONT_LIST, m_buttonGetFontList);
    AttachItem(IDC_RICHEDIT_PROPS, m_richEdit);

    m_resizer.Initialize(*this, CRect(0, 0, 640, 400));
    m_resizer.AddChild(m_richEdit, CResizer::topleft, RD_STRETCH_WIDTH | RD_STRETCH_HEIGHT);
    m_resizer.AddChild(m_buttonClose, CResizer::bottomright, 0);
    m_resizer.AddChild(m_buttonGetFontList, CResizer::bottomleft, 0);

    long chStart = -3;
    long chEnd = -3;

    m_richEdit.SetTextMode(TM_RICHTEXT);
    m_richEdit.SetReadOnly();
    m_richEdit.GetSel(chStart, chEnd);
    //m_richEdit.SetSel(0, 0);

    const char* s = R"foo({\rtf1\ansi\deff0 {\fonttbl {\f0 Arial;}}
\f0\fs30\qc Hello World!
\line
And another world\par
\f0\fs20 Another line\par
What now\par

\trowd\trgaph60\cellx1440\cellx6800
\pard\intbl\qr {\b Foo:}\cell
\pard\intbl bar\cell
\row
\trowd\trgaph180\cellx1440\cellx6000
\pard\intbl {\b Foo2}\cell
\pard\intbl bar2 and they went abroad for them ine the mix
\line another line\cell
\row


{\pard Hmmm \par}
{\pard
\trowd\trgaph300\trleft400\cellx1500\cellx3000
\pard\intbl Too. Doo wah\cell
\pard\intbl Chree. Doo wah ditty ditty dum ditty do \cell
\row
\trowd\trgaph300\trleft400\cellx1500\cellx3000
\pard\intbl Fahv. Doo wah ditty ditty dum ditty do \cell
\pard\intbl Saxe. Doo wah ditty ditty dum ditty do \cell
\row
\trowd\trgaph300\trleft400\cellx1500\cellx3500
\pard\intbl Saven. Doo wah ditty ditty dum ditty do \cell
\pard\intbl Ight. Doo wah ditty ditty dum ditty do \cell
\row
}
{\pard I LIKE PIE}

})foo";
    RichEditStreamInString(m_richEdit, SF_RTF, s);
    m_richEdit.SetModify(FALSE);
    //m_richEdit.GetSel(chStart, chEnd);
    m_richEdit.SetSel(0, 1);
    m_richEdit.GetSel(chStart, chEnd);
    //m_richEdit.HideSelection(true, false);
    //m_richEdit.SetFocus();

    return TRUE;
}

// Called when the OK button or Enter key is pressed.
void CMyDialog::OnOK() {
    MessageBox(_T("OK Button Pressed.  Program will exit now."), _T("Button"), MB_OK);
    CDialog::OnOK();
}

BOOL CMyDialog::OnButtonClose() {
    return TRUE;
}

BOOL CMyDialog::OnButtonGetFontList() {
    return TRUE;
}

// Called when the dialog's background is redrawn.
BOOL CMyDialog::OnEraseBkgnd(CDC&) {
    // Adding a gripper to a resizable dialog is a bit of a hack, but since it
    // is often done, here is one method of doing it safely.

    // Draw the dialog's background manually
    CRect rc = GetClientRect();
    CClientDC dcClient(*this);
    dcClient.SolidFill(GetSysColor(COLOR_3DFACE), rc);

    // draw size grip
    if (rc.Width() > m_resizer.GetMinRect().Width() && rc.Height() > m_resizer.GetMinRect().Height()) {
        int size = GetSystemMetrics(SM_CXVSCROLL);
        rc.left = rc.right - size;
        rc.top = rc.bottom - size;
        dcClient.DrawFrameControl(rc, DFC_SCROLL, DFCS_SCROLLSIZEGRIP);
    }

    // Suppress default background drawing
    return TRUE;
}


int Run() {
    // Start Win32++
    CWinApp theApp;

    // Create a CMyWindow object
    CMyDialog myWindow(IDD_DIALOG_DOC_PROPERTIES);

    // Create (and display) the window
    myWindow.DoModeless();

    // Run the application's message loop
    return theApp.Run();
}
}

void TestApp(HINSTANCE hInstance) {
    gHinst = hInstance;

    if (true) {
        TestWin32Framework1::Run();
        return;
    }

    // return TestDirectDraw(hInstance, nCmdShow);
    // return TestTab(hInstance, nCmdShow);
    // return TestLayout(hInstance, nCmdShow);

    auto w = new Window();
    w->backgroundColor = MkColor((u8)0xae, (u8)0xae, (u8)0xae);
    w->SetTitle("this is a title");
    w->initialPos = {100, 100};
    w->initialSize = {480, 640};
    bool ok = w->Create();
    CrashIf(!ok);

    auto l = CreateMainLayout(w->hwnd);
    w->onSize = [&](SizeEvent* args) {
        HWND hwnd = args->hwnd;
        int dx = args->dx;
        int dy = args->dy;
        if (dx == 0 || dy == 0) {
            return;
        }
        LayoutToSize(l, {dx, dy});
        InvalidateRect(hwnd, nullptr, false);
    };

    // important to call this after hooking up onSize to ensure
    // first layout is triggered
    w->SetIsVisible(true);

    auto res = RunMessageLoop(nullptr, w->hwnd);
    return;
}
