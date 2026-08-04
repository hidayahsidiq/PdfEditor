#ifndef UNICODE
#define UNICODE
#endif

#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <stdio.h>

#include <qpdf/QPDF.hh>
#include <qpdf/QPDFWriter.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFPageObjectHelper.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <algorithm>
#include <stdexcept>
#include <wchar.h>

#pragma comment(linker, "\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "qpdf.lib")

using namespace std;

///////////////////////////////////////////////////////////////////////////////
// Control and menu IDs
///////////////////////////////////////////////////////////////////////////////

constexpr int IDC_PAGE_LIST = 100;

enum MenuId
{
    IDM_OPEN = 2000,
    IDM_ADD,
    IDM_SAVEAS,
    IDM_EXIT,
    IDM_ROTATE_LEFT,
    IDM_ROTATE_RIGHT,
    IDM_MOVE_UP,
    IDM_MOVE_DOWN,
    IDM_DELETE_PAGE,
    IDM_EXTRACT_SELECTED,
    IDM_SPLIT_ALL_PAGES
};

///////////////////////////////////////////////////////////////////////////////
// Data model
///////////////////////////////////////////////////////////////////////////////

struct PageRef
{
    wstring sourcePath;
    int pageIndex = 0;
    int rotateDelta = 0;
};

vector<PageRef> g_pages;
HWND g_hList = nullptr;

///////////////////////////////////////////////////////////////////////////////
// String helpers
///////////////////////////////////////////////////////////////////////////////

string WideToAnsi(const wstring& wide)
{
    if (wide.empty())
    {
        return {};
    }

    int sizeNeeded = WideCharToMultiByte(
        CP_ACP,
        0,
        wide.c_str(),
        static_cast<int>(wide.size()),
        nullptr,
        0,
        nullptr,
        nullptr
    );

    string result(static_cast<size_t>(sizeNeeded), 0);

    WideCharToMultiByte(
        CP_ACP,
        0,
        wide.c_str(),
        static_cast<int>(wide.size()),
        result.data(),
        sizeNeeded,
        nullptr,
        nullptr
    );

    return result;
}

wstring AnsiToWide(const string& ansi)
{
    if (ansi.empty())
    {
        return {};
    }

    int sizeNeeded = MultiByteToWideChar(
        CP_ACP,
        0,
        ansi.c_str(),
        static_cast<int>(ansi.size()),
        nullptr,
        0
    );

    wstring result(static_cast<size_t>(sizeNeeded), 0);

    MultiByteToWideChar(
        CP_ACP,
        0,
        ansi.c_str(),
        static_cast<int>(ansi.size()),
        result.data(),
        sizeNeeded
    );

    return result;
}

wstring FileNameFromPath(const wstring& path)
{
    size_t pos = path.find_last_of(L"\\/");

    if (pos == wstring::npos)
    {
        return path;
    }

    return path.substr(pos + 1);
}

wstring RemoveExtension(const wstring& path)
{
    size_t dot = path.find_last_of(L'.');
    size_t slash = path.find_last_of(L"\\/");

    if (dot != wstring::npos)
    {
        if (slash == wstring::npos || dot > slash)
        {
            return path.substr(0, dot);
        }
    }

    return path;
}

wstring EnsurePdfExtension(const wstring& path)
{
    if (path.size() >= 4)
    {
        wstring ext = path.substr(path.size() - 4);

        if (_wcsicmp(ext.c_str(), L".pdf") == 0)
        {
            return path;
        }
    }

    return path + L".pdf";
}

wstring GetShortPath(const wstring& path)
{
    DWORD needed = GetShortPathNameW(path.c_str(), nullptr, 0);

    if (needed == 0)
    {
        return path;
    }

    vector<wchar_t> buffer(static_cast<size_t>(needed), 0);

    DWORD length = GetShortPathNameW(
        path.c_str(),
        buffer.data(),
        needed
    );

    if (length == 0)
    {
        return path;
    }

    return wstring(buffer.data(), length);
}

///////////////////////////////////////////////////////////////////////////////
// PDF helpers
///////////////////////////////////////////////////////////////////////////////

int NormalizeRotation(int rotation)
{
    rotation %= 360;

    if (rotation < 0)
    {
        rotation += 360;
    }

    return rotation;
}

///////////////////////////////////////////////////////////////////////////////
// PDF engine
///////////////////////////////////////////////////////////////////////////////

class PdfEngine
{
public:
    void ClearCache()
    {
        m_cache.clear();
    }

    int GetPageCount(const wstring& path)
    {
        shared_ptr<QPDF> doc = GetDocument(path);

        auto pages = QPDFPageDocumentHelper(*doc).getAllPages();

        return static_cast<int>(pages.size());
    }

    bool BuildPdf(
        const vector<PageRef>& pagesToWrite,
        const wstring& outputPath,
        string& error)
    {
        FILE* outputFile = nullptr;

        errno_t openError = _wfopen_s(
            &outputFile,
            outputPath.c_str(),
            L"wb"
        );

        if (openError != 0 || outputFile == nullptr)
        {
            error = "Cannot create output file.";
            return false;
        }

        try
        {
            QPDF outputPdf;
            outputPdf.emptyPDF();

            for (const PageRef& ref : pagesToWrite)
            {
                shared_ptr<QPDF> source = GetDocument(ref.sourcePath);

                auto sourcePages = QPDFPageDocumentHelper(*source).getAllPages();

                if (ref.pageIndex < 0 || ref.pageIndex >= static_cast<int>(sourcePages.size()))
                {
                    fclose(outputFile);
                    error = "Invalid page index.";
                    return false;
                }

                QPDFPageObjectHelper sourcePage = sourcePages[ref.pageIndex];

                outputPdf.addPage(sourcePage, false);

                if (ref.rotateDelta != 0)
                {
                    auto outputPages = QPDFPageDocumentHelper(outputPdf).getAllPages();

                    if (outputPages.empty())
                    {
                        fclose(outputFile);
                        error = "Failed to add page to output PDF.";
                        return false;
                    }

                    QPDFPageObjectHelper outputPage = outputPages.back();

                    int oldRotation = 0;

                    QPDFObjectHandle sourceObject = sourcePage.getObjectHandle();
                    QPDFObjectHandle rotateObject = sourceObject.getKey("/Rotate");

                    if (rotateObject.isNumber())
                    {
                        oldRotation = static_cast<int>(rotateObject.getIntValue());
                    }

                    int newRotation = NormalizeRotation(oldRotation + ref.rotateDelta);

                    outputPage.getObjectHandle().replaceKey(
                        "/Rotate",
                        QPDFObjectHandle::newInteger(newRotation)
                    );
                }
            }

            string ansiOutputPath = WideToAnsi(outputPath);

            QPDFWriter writer(outputPdf);

            writer.setOutputFile(ansiOutputPath.c_str(), outputFile, false);

            writer.write();

            fclose(outputFile);

            return true;
        }
        catch (const exception& e)
        {
            fclose(outputFile);
            error = e.what();
            return false;
        }
    }

private:
    map<wstring, shared_ptr<QPDF>> m_cache;

    shared_ptr<QPDF> GetDocument(const wstring& path)
    {
        auto it = m_cache.find(path);

        if (it != m_cache.end())
        {
            return it->second;
        }

        auto pdf = make_shared<QPDF>();

        wstring shortPath = GetShortPath(path);

        string ansiPath = WideToAnsi(shortPath);

        pdf->processFile(ansiPath.c_str());

        m_cache[path] = pdf;

        return pdf;
    }
};

PdfEngine g_engine;

///////////////////////////////////////////////////////////////////////////////
// Message helpers
///////////////////////////////////////////////////////////////////////////////

void ShowErrorW(HWND hwnd, const wstring& message)
{
    MessageBoxW(hwnd, message.c_str(), L"PDF Editor", MB_ICONERROR);
}

void ShowErrorAnsi(HWND hwnd, const string& message)
{
    MessageBoxW(hwnd, AnsiToWide(message).c_str(), L"PDF Editor", MB_ICONERROR);
}

void ShowInfoW(HWND hwnd, const wstring& message)
{
    MessageBoxW(hwnd, message.c_str(), L"PDF Editor", MB_ICONINFORMATION);
}

///////////////////////////////////////////////////////////////////////////////
// UI helpers
///////////////////////////////////////////////////////////////////////////////

void SetMainWindowStatus(HWND hwnd)
{
    wstring title = L"PDF Editor - " + to_wstring(g_pages.size()) + L" page(s)";
    SetWindowTextW(hwnd, title.c_str());
}

void SetupListView(HWND hList)
{
    ListView_SetExtendedListViewStyle(
        hList,
        LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES
    );

    LVCOLUMNW col = {};

    col.mask = LVCF_TEXT | LVCF_WIDTH;

    col.cx = 60;
    col.pszText = const_cast<LPWSTR>(L"#");
    ListView_InsertColumn(hList, 0, &col);

    col.cx = 90;
    col.pszText = const_cast<LPWSTR>(L"Rotation");
    ListView_InsertColumn(hList, 1, &col);

    col.cx = 350;
    col.pszText = const_cast<LPWSTR>(L"Source File");
    ListView_InsertColumn(hList, 2, &col);
}

void SelectListItem(HWND hList, int index)
{
    if (index < 0)
    {
        return;
    }

    ListView_SetItemState(
        hList,
        index,
        LVIS_SELECTED | LVIS_FOCUSED,
        LVIS_SELECTED | LVIS_FOCUSED
    );

    ListView_EnsureVisible(hList, index, FALSE);
}

void RefreshPageList(HWND hList, int selectedIndex = -1)
{
    ListView_DeleteAllItems(hList);

    for (size_t i = 0; i < g_pages.size(); i++)
    {
        wstring numberText = to_wstring(i + 1);

        LVITEMW item = {};
        item.mask = LVIF_TEXT;
        item.iItem = static_cast<int>(i);
        item.iSubItem = 0;
        item.pszText = const_cast<LPWSTR>(numberText.c_str());

        ListView_InsertItem(hList, &item);

        wstring rotationText = to_wstring(g_pages[i].rotateDelta);
        ListView_SetItemText(
            hList,
            static_cast<int>(i),
            1,
            const_cast<LPWSTR>(rotationText.c_str())
        );

        wstring fileName = FileNameFromPath(g_pages[i].sourcePath);
        ListView_SetItemText(
            hList,
            static_cast<int>(i),
            2,
            const_cast<LPWSTR>(fileName.c_str())
        );
    }

    if (selectedIndex >= 0 && selectedIndex < static_cast<int>(g_pages.size()))
    {
        SelectListItem(hList, selectedIndex);
    }

    SetMainWindowStatus(GetParent(hList));
}

int GetSelectedIndex(HWND hList)
{
    return ListView_GetNextItem(hList, -1, LVNI_SELECTED);
}

///////////////////////////////////////////////////////////////////////////////
// Dialog helpers
///////////////////////////////////////////////////////////////////////////////

bool OpenPdfFiles(HWND hwnd, vector<wstring>& files)
{
    files.clear();

    const DWORD bufferSize = 65536;
    vector<wchar_t> buffer(bufferSize, 0);

    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = L"PDF Files (*.pdf)\0*.pdf\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = buffer.data();
    ofn.nMaxFile = bufferSize;
    ofn.Flags =
        OFN_FILEMUSTEXIST |
        OFN_PATHMUSTEXIST |
        OFN_ALLOWMULTISELECT |
        OFN_EXPLORER;

    if (!GetOpenFileNameW(&ofn))
    {
        return false;
    }

    wstring first = buffer.data();
    size_t pos = first.size() + 1;

    if (pos >= bufferSize || buffer[pos] == 0)
    {
        files.push_back(first);
        return true;
    }

    while (pos < bufferSize && buffer[pos] != 0)
    {
        wstring fileName = &buffer[pos];

        wstring fullPath = first;

        if (!fullPath.empty() && fullPath.back() != L'\\' && fullPath.back() != L'/')
        {
            fullPath += L'\\';
        }

        fullPath += fileName;

        files.push_back(fullPath);

        pos += fileName.size() + 1;
    }

    return true;
}

bool SavePdfFile(HWND hwnd, wstring& savePath)
{
    wchar_t fileBuffer[MAX_PATH] = L"output.pdf";

    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = L"PDF Files (*.pdf)\0*.pdf\0";
    ofn.lpstrFile = fileBuffer;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags =
        OFN_OVERWRITEPROMPT |
        OFN_PATHMUSTEXIST;

    if (!GetSaveFileNameW(&ofn))
    {
        return false;
    }

    savePath = EnsurePdfExtension(fileBuffer);
    return true;
}

///////////////////////////////////////////////////////////////////////////////
// Page operations
///////////////////////////////////////////////////////////////////////////////

bool IsCurrentSourceFile(const wstring& path)
{
    for (const PageRef& page : g_pages)
    {
        if (_wcsicmp(page.sourcePath.c_str(), path.c_str()) == 0)
        {
            return true;
        }
    }

    return false;
}

bool AppendFiles(const vector<wstring>& files, string& error)
{
    size_t originalSize = g_pages.size();

    try
    {
        for (const wstring& file : files)
        {
            int pageCount = g_engine.GetPageCount(file);

            for (int i = 0; i < pageCount; i++)
            {
                PageRef ref;
                ref.sourcePath = file;
                ref.pageIndex = i;
                ref.rotateDelta = 0;

                g_pages.push_back(ref);
            }
        }

        return true;
    }
    catch (const exception& e)
    {
        g_pages.resize(originalSize);
        error = e.what();
        return false;
    }
}

void MoveSelectedPage(HWND hwnd, int direction)
{
    int index = GetSelectedIndex(g_hList);

    if (index < 0)
    {
        ShowInfoW(hwnd, L"Select a page first.");
        return;
    }

    int newIndex = index + direction;

    if (newIndex < 0 || newIndex >= static_cast<int>(g_pages.size()))
    {
        return;
    }

    iter_swap(g_pages.begin() + index, g_pages.begin() + newIndex);

    RefreshPageList(g_hList, newIndex);
}

void DeleteSelectedPage(HWND hwnd)
{
    int index = GetSelectedIndex(g_hList);

    if (index < 0)
    {
        ShowInfoW(hwnd, L"Select a page first.");
        return;
    }

    g_pages.erase(g_pages.begin() + index);

    int newSelection = index;

    if (newSelection >= static_cast<int>(g_pages.size()))
    {
        newSelection = static_cast<int>(g_pages.size()) - 1;
    }

    RefreshPageList(g_hList, newSelection);
}

void RotateSelectedPage(HWND hwnd, int amount)
{
    int index = GetSelectedIndex(g_hList);

    if (index < 0)
    {
        ShowInfoW(hwnd, L"Select a page first.");
        return;
    }

    g_pages[index].rotateDelta =
        NormalizeRotation(g_pages[index].rotateDelta + amount);

    RefreshPageList(g_hList, index);
}

void SaveCurrentPages(HWND hwnd)
{
    if (g_pages.empty())
    {
        ShowInfoW(hwnd, L"No pages loaded.");
        return;
    }

    wstring savePath;

    if (!SavePdfFile(hwnd, savePath))
    {
        return;
    }

    if (IsCurrentSourceFile(savePath))
    {
        ShowErrorW(
            hwnd,
            L"You cannot save over a source file while it is loaded.\n"
            L"Please choose a different file name."
        );
        return;
    }

    string error;

    if (g_engine.BuildPdf(g_pages, savePath, error))
    {
        ShowInfoW(hwnd, L"PDF saved successfully:\n" + savePath);
    }
    else
    {
        ShowErrorAnsi(hwnd, error);
    }
}

void ExtractSelectedPage(HWND hwnd)
{
    int index = GetSelectedIndex(g_hList);

    if (index < 0)
    {
        ShowInfoW(hwnd, L"Select a page first.");
        return;
    }

    wstring savePath;

    if (!SavePdfFile(hwnd, savePath))
    {
        return;
    }

    if (IsCurrentSourceFile(savePath))
    {
        ShowErrorW(
            hwnd,
            L"You cannot save over a source file while it is loaded.\n"
            L"Please choose a different file name."
        );
        return;
    }

    vector<PageRef> onePage;
    onePage.push_back(g_pages[index]);

    string error;

    if (g_engine.BuildPdf(onePage, savePath, error))
    {
        ShowInfoW(hwnd, L"Selected page saved successfully:\n" + savePath);
    }
    else
    {
        ShowErrorAnsi(hwnd, error);
    }
}

void SplitAllPagesToSeparateFiles(HWND hwnd)
{
    if (g_pages.empty())
    {
        ShowInfoW(hwnd, L"No pages loaded.");
        return;
    }

    wstring basePath;

    if (!SavePdfFile(hwnd, basePath))
    {
        return;
    }

    basePath = RemoveExtension(basePath);

    string error;

    for (size_t i = 0; i < g_pages.size(); i++)
    {
        wstring outputPath =
            basePath + L"_" + to_wstring(i + 1) + L".pdf";

        if (IsCurrentSourceFile(outputPath))
        {
            ShowErrorW(
                hwnd,
                L"One of the output files is also a loaded source file:\n" +
                outputPath +
                L"\n\nPlease choose a different base output file."
            );
            return;
        }

        vector<PageRef> onePage;
        onePage.push_back(g_pages[i]);

        if (!g_engine.BuildPdf(onePage, outputPath, error))
        {
            ShowErrorAnsi(hwnd, error);
            return;
        }
    }

    ShowInfoW(hwnd, L"All pages exported as separate PDF files.");
}

///////////////////////////////////////////////////////////////////////////////
// Menu
///////////////////////////////////////////////////////////////////////////////

HMENU CreateMainMenu()
{
    HMENU menuBar = CreateMenu();

    HMENU fileMenu = CreatePopupMenu();
    AppendMenuW(fileMenu, MF_STRING, IDM_OPEN, L"Open PDF...");
    AppendMenuW(fileMenu, MF_STRING, IDM_ADD, L"Add PDF Files...");
    AppendMenuW(fileMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(fileMenu, MF_STRING, IDM_SAVEAS, L"Save As...");
    AppendMenuW(fileMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(fileMenu, MF_STRING, IDM_EXIT, L"Exit");

    HMENU editMenu = CreatePopupMenu();
    AppendMenuW(editMenu, MF_STRING, IDM_ROTATE_LEFT, L"Rotate Left");
    AppendMenuW(editMenu, MF_STRING, IDM_ROTATE_RIGHT, L"Rotate Right");
    AppendMenuW(editMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(editMenu, MF_STRING, IDM_MOVE_UP, L"Move Up");
    AppendMenuW(editMenu, MF_STRING, IDM_MOVE_DOWN, L"Move Down");
    AppendMenuW(editMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(editMenu, MF_STRING, IDM_DELETE_PAGE, L"Delete Page");
    AppendMenuW(editMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(editMenu, MF_STRING, IDM_EXTRACT_SELECTED, L"Extract Selected Page...");
    AppendMenuW(editMenu, MF_STRING, IDM_SPLIT_ALL_PAGES, L"Export Every Page as Separate PDF...");

    AppendMenuW(menuBar, MF_POPUP, reinterpret_cast<UINT_PTR>(fileMenu), L"File");
    AppendMenuW(menuBar, MF_POPUP, reinterpret_cast<UINT_PTR>(editMenu), L"Edit");

    return menuBar;
}

///////////////////////////////////////////////////////////////////////////////
// Window procedure
///////////////////////////////////////////////////////////////////////////////

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE:
    {
        INITCOMMONCONTROLSEX icc = {};
        icc.dwSize = sizeof(icc);
        icc.dwICC = ICC_LISTVIEW_CLASSES;
        InitCommonControlsEx(&icc);

        RECT rc;
        GetClientRect(hWnd, &rc);

        g_hList = CreateWindowExW(
            WS_EX_CLIENTEDGE,
            WC_LISTVIEWW,
            L"",
            WS_CHILD |
            WS_VISIBLE |
            WS_BORDER |
            LVS_REPORT |
            LVS_SINGLESEL |
            LVS_SHOWSELALWAYS,
            0,
            0,
            rc.right,
            rc.bottom,
            hWnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_PAGE_LIST)),
            GetModuleHandleW(nullptr),
            nullptr
        );

        SetupListView(g_hList);
        SetMainWindowStatus(hWnd);

        return 0;
    }

    case WM_SIZE:
    {
        if (g_hList)
        {
            MoveWindow(
                g_hList,
                0,
                0,
                LOWORD(lParam),
                HIWORD(lParam),
                TRUE
            );
        }

        return 0;
    }

    case WM_COMMAND:
    {
        int id = LOWORD(wParam);

        switch (id)
        {
        case IDM_EXIT:
        {
            DestroyWindow(hWnd);
            break;
        }

        case IDM_OPEN:
        {
            vector<wstring> files;

            if (!OpenPdfFiles(hWnd, files))
            {
                break;
            }

            vector<PageRef> oldPages = g_pages;

            g_pages.clear();
            g_engine.ClearCache();

            string error;

            if (AppendFiles(files, error))
            {
                RefreshPageList(g_hList);
            }
            else
            {
                g_pages = oldPages;
                RefreshPageList(g_hList);
                ShowErrorAnsi(hWnd, error);
            }

            break;
        }

        case IDM_ADD:
        {
            vector<wstring> files;

            if (!OpenPdfFiles(hWnd, files))
            {
                break;
            }

            string error;

            if (AppendFiles(files, error))
            {
                RefreshPageList(g_hList);
            }
            else
            {
                ShowErrorAnsi(hWnd, error);
            }

            break;
        }

        case IDM_SAVEAS:
        {
            SaveCurrentPages(hWnd);
            break;
        }

        case IDM_ROTATE_LEFT:
        {
            RotateSelectedPage(hWnd, -90);
            break;
        }

        case IDM_ROTATE_RIGHT:
        {
            RotateSelectedPage(hWnd, 90);
            break;
        }

        case IDM_MOVE_UP:
        {
            MoveSelectedPage(hWnd, -1);
            break;
        }

        case IDM_MOVE_DOWN:
        {
            MoveSelectedPage(hWnd, 1);
            break;
        }

        case IDM_DELETE_PAGE:
        {
            DeleteSelectedPage(hWnd);
            break;
        }

        case IDM_EXTRACT_SELECTED:
        {
            ExtractSelectedPage(hWnd);
            break;
        }

        case IDM_SPLIT_ALL_PAGES:
        {
            SplitAllPagesToSeparateFiles(hWnd);
            break;
        }
        }

        return 0;
    }

    case WM_DESTROY:
    {
        PostQuitMessage(0);
        return 0;
    }

    default:
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }
}

///////////////////////////////////////////////////////////////////////////////
// WinMain
///////////////////////////////////////////////////////////////////////////////

int WINAPI wWinMain(
    HINSTANCE hInstance,
    HINSTANCE hPrevInstance,
    PWSTR lpCmdLine,
    int nCmdShow)
{
    SetProcessDPIAware();

    WNDCLASSW wc = {};

    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = L"PdfEditorMainWindowClass";
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);

    if (!RegisterClassW(&wc))
    {
        return 0;
    }

    HMENU mainMenu = CreateMainMenu();

    HWND hWnd = CreateWindowExW(
        0,
        wc.lpszClassName,
        L"PDF Editor",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        900,
        600,
        nullptr,
        mainMenu,
        hInstance,
        nullptr
    );

    if (!hWnd)
    {
        return 0;
    }

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    MSG msg = {};

    while (GetMessageW(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return static_cast<int>(msg.wParam);
}