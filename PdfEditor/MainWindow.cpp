#include "MainWindow.h"
#include "App.h"

#include <windowsx.h>

bool MainWindow::Create(HINSTANCE hInst, App* app)
{
    this->hInstance = hInst;
    this->m_app = app;

    WNDCLASSW wc = {};

    wc.lpfnWndProc = MainWindow::WndProcThunk;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = L"PdfEditorMainWindowClass";
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);

    if (!RegisterClassW(&wc))
    {
        return false;
    }

    HMENU menu = CreateMainMenu();

    m_hwnd = CreateWindowExW(
        0,
        wc.lpszClassName,
        L"PDF Editor",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        1000,
        700,
        nullptr,
        menu,
        hInstance,
        this
    );

    if (!m_hwnd)
    {
        return false;
    }

    ShowWindow(m_hwnd, SW_SHOW);
    UpdateWindow(m_hwnd);

    return true;
}

void MainWindow::SetStatus(const std::wstring& text)
{
    if (m_status)
    {
        SendMessageW(
            m_status,
            SB_SETTEXTW,
            0,
            reinterpret_cast<LPARAM>(text.c_str())
        );
    }
}

HMENU MainWindow::CreateMainMenu()
{
    HMENU menuBar = CreateMenu();

    HMENU fileMenu = CreatePopupMenu();
    AppendMenuW(fileMenu, MF_STRING, ID_CMD_OPEN, L"Open PDF...");
    AppendMenuW(fileMenu, MF_STRING, ID_CMD_ADD, L"Add PDF Files...");
    AppendMenuW(fileMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(fileMenu, MF_STRING, ID_CMD_SAVEAS, L"Save As...");
    AppendMenuW(fileMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(fileMenu, MF_STRING, ID_CMD_EXIT, L"Exit");

    HMENU editMenu = CreatePopupMenu();
    AppendMenuW(editMenu, MF_STRING, ID_CMD_ROTATE_LEFT, L"Rotate Left");
    AppendMenuW(editMenu, MF_STRING, ID_CMD_ROTATE_RIGHT, L"Rotate Right");
    AppendMenuW(editMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(editMenu, MF_STRING, ID_CMD_MOVE_UP, L"Move Up");
    AppendMenuW(editMenu, MF_STRING, ID_CMD_MOVE_DOWN, L"Move Down");
    AppendMenuW(editMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(editMenu, MF_STRING, ID_CMD_DELETE_PAGE, L"Delete Page");
    AppendMenuW(editMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(editMenu, MF_STRING, ID_CMD_EXTRACT_SELECTED, L"Extract Selected Page...");
    AppendMenuW(editMenu, MF_STRING, ID_CMD_SPLIT_ALL, L"Export Every Page as Separate PDF...");
    AppendMenuW(editMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(editMenu, MF_STRING, ID_CMD_EDIT_PAGE_TEXT, L"Edit Page Text");
    AppendMenuW(editMenu, MF_STRING, ID_CMD_BACK_TO_PAGES, L"Back to Pages");

    HMENU viewMenu = CreatePopupMenu();

    HMENU helpMenu = CreatePopupMenu();
    AppendMenuW(helpMenu, MF_STRING, ID_CMD_ABOUT, L"About");

    AppendMenuW(menuBar, MF_POPUP, reinterpret_cast<UINT_PTR>(fileMenu), L"File");
    AppendMenuW(menuBar, MF_POPUP, reinterpret_cast<UINT_PTR>(editMenu), L"Edit");
    AppendMenuW(menuBar, MF_POPUP, reinterpret_cast<UINT_PTR>(viewMenu), L"View");
    AppendMenuW(menuBar, MF_POPUP, reinterpret_cast<UINT_PTR>(helpMenu), L"Help");

    return menuBar;
}

void MainWindow::CreateToolbar()
{
    m_toolbar = CreateWindowExW(
        0,
        TOOLBARCLASSNAMEW,
        nullptr,
        WS_CHILD |
        WS_VISIBLE |
        WS_BORDER |
        TBSTYLE_FLAT |
        TBSTYLE_LIST |
        TBSTYLE_TOOLTIPS |
        CCS_TOP |
        CCS_NORESIZE |
        CCS_ADJUSTABLE,
        0,
        0,
        0,
        0,
        m_hwnd,
        nullptr,
        hInstance,
        nullptr
    );

    if (!m_toolbar)
    {
        return;
    }

    SendMessageW(m_toolbar, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);

    auto AddButton = [&](int commandId, const wchar_t* text)
        {
            int stringIndex = static_cast<int>(
                SendMessageW(
                    m_toolbar,
                    TB_ADDSTRINGW,
                    0,
                    reinterpret_cast<LPARAM>(text)
                )
                );

            TBBUTTON button = {};
            button.idCommand = commandId;
            button.fsState = TBSTATE_ENABLED;
            button.fsStyle = BTNS_BUTTON | BTNS_SHOWTEXT;
            button.iString = stringIndex;

            SendMessageW(
                m_toolbar,
                TB_ADDBUTTONS,
                1,
                reinterpret_cast<LPARAM>(&button)
            );
        };

    AddButton(ID_CMD_OPEN, L"Open");
    AddButton(ID_CMD_SAVEAS, L"Save");
    AddButton(ID_CMD_ADD, L"Merge");
    AddButton(ID_CMD_SPLIT_ALL, L"Split");
    AddButton(ID_CMD_ROTATE_RIGHT, L"Rotate");
    AddButton(ID_CMD_DELETE_PAGE, L"Delete");
    AddButton(ID_CMD_EDIT_PAGE_TEXT, L"Edit Text");
    AddButton(ID_CMD_BACK_TO_PAGES, L"Pages");

    SendMessageW(m_toolbar, TB_AUTOSIZE, 0, 0);
}

void MainWindow::CreateStatus()
{
    m_status = CreateWindowExW(
        0,
        STATUSCLASSNAMEW,
        L"Status: 0 pages loaded",
        WS_CHILD |
        WS_VISIBLE |
        SBARS_SIZEGRIP,
        0,
        0,
        0,
        0,
        m_hwnd,
        nullptr,
        hInstance,
        nullptr
    );
}

void MainWindow::CreateControls()
{
    INITCOMMONCONTROLSEX icc = {};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);

    CreateToolbar();
    CreateStatus();

    m_pageView.Create(m_hwnd, hInstance);

    RECT rc;
    GetClientRect(m_hwnd, &rc);

    Layout(rc.right, rc.bottom);
}

void MainWindow::Layout(int width, int height)
{
    if (!m_toolbar || !m_status || !m_pageView.GetHwnd())
    {
        return;
    }

    SendMessageW(m_toolbar, WM_SIZE, 0, 0);
    SendMessageW(m_status, WM_SIZE, 0, 0);

    RECT toolbarRect;
    GetWindowRect(m_toolbar, &toolbarRect);

    RECT statusRect;
    GetWindowRect(m_status, &statusRect);

    int toolbarHeight = toolbarRect.bottom - toolbarRect.top;
    int statusHeight = statusRect.bottom - statusRect.top;

    int listHeight = height - toolbarHeight - statusHeight;

    if (listHeight < 0)
    {
        listHeight = 0;
    }

    MoveWindow(
        m_toolbar,
        0,
        0,
        width,
        toolbarHeight,
        TRUE
    );

    MoveWindow(
        m_status,
        0,
        height - statusHeight,
        width,
        statusHeight,
        TRUE
    );

    MoveWindow(
        m_pageView.GetHwnd(),
        0,
        toolbarHeight,
        width,
        listHeight,
        TRUE
    );
    HWND hEditView = GetDlgItem(m_hwnd, IDC_EDIT_VIEW);

    if (hEditView && IsWindowVisible(hEditView))
    {
        MoveWindow(
            hEditView,
            0,
            toolbarHeight,
            width,
            listHeight,
            TRUE
        );
    }
}

LRESULT CALLBACK MainWindow::WndProcThunk(
    HWND hwnd,
    UINT msg,
    WPARAM wParam,
    LPARAM lParam)
{
    MainWindow* self = nullptr;

    if (msg == WM_NCCREATE)
    {
        CREATESTRUCTW* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = reinterpret_cast<MainWindow*>(cs->lpCreateParams);

        SetWindowLongPtrW(
            hwnd,
            GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(self)
        );

        self->m_hwnd = hwnd;
    }
    else
    {
        self = reinterpret_cast<MainWindow*>(
            GetWindowLongPtrW(hwnd, GWLP_USERDATA)
            );
    }

    if (self)
    {
        return self->WndProc(msg, wParam, lParam);
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT MainWindow::WndProc(UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE:
    {
        CreateControls();
        return 0;
    }

    case WM_SIZE:
    {
        Layout(LOWORD(lParam), HIWORD(lParam));
        return 0;
    }

    case WM_COMMAND:
    {
        int commandId = LOWORD(wParam);

        if (m_app)
        {
            m_app->OnCommand(commandId);
        }

        return 0;
    }

    case WM_DESTROY:
    {
        PostQuitMessage(0);
        return 0;
    }

    default:
        return DefWindowProcW(m_hwnd, msg, wParam, lParam);
    }
}