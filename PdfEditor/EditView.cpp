#include "EditView.h"
#include "StringUtils.h"

#include <windowsx.h>
#include <commctrl.h>
#include <math.h>
#include <algorithm>
#include <string.h>
#include <stdio.h>



#pragma comment(lib, "pdfium.lib")
#pragma comment(lib, "comctl32.lib")

#ifndef FPDF_FILLMODE_WINDING
#define FPDF_FILLMODE_WINDING 1
#endif

///////////////////////////////////////////////////////////////////////////////
// Helper: create HBITMAP from PDFium bitmap
///////////////////////////////////////////////////////////////////////////////

static HBITMAP CreateDIBFromFPDFBitmap(FPDF_BITMAP fpdfBitmap)
{
    int width = FPDFBitmap_GetWidth(fpdfBitmap);
    int height = FPDFBitmap_GetHeight(fpdfBitmap);
    int stride = FPDFBitmap_GetStride(fpdfBitmap);

    const BYTE* srcBuffer = reinterpret_cast<const BYTE*>(
        FPDFBitmap_GetBuffer(fpdfBitmap)
        );

    if (width <= 0 || height <= 0 || stride <= 0 || !srcBuffer)
    {
        return nullptr;
    }

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height; // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;

    HBITMAP hBitmap = CreateDIBSection(
        nullptr,
        &bmi,
        DIB_RGB_COLORS,
        &bits,
        nullptr,
        0
    );

    if (!hBitmap || !bits)
    {
        if (hBitmap)
        {
            DeleteObject(hBitmap);
        }

        return nullptr;
    }

    int dstStride = width * 4;

    for (int y = 0; y < height; y++)
    {
        const BYTE* srcRow = srcBuffer + (y * stride);

        BYTE* dstRow =
            reinterpret_cast<BYTE*>(bits) + (y * dstStride);

        memcpy(dstRow, srcRow, dstStride);
    }

    return hBitmap;
}

///////////////////////////////////////////////////////////////////////////////
// PDF save helper
///////////////////////////////////////////////////////////////////////////////



static int PdfWriteBlock(
    FPDF_FILEWRITE* pThis,
    const void* pData,
    unsigned long size)
{
    PdfFileWriter* writer = reinterpret_cast<PdfFileWriter*>(pThis);

    if (!writer || !writer->file)
    {
        return 0;
    }

    size_t written = fwrite(pData, 1, size, writer->file);

    return written == size ? 1 : 0;
}

///////////////////////////////////////////////////////////////////////////////
// EditView
///////////////////////////////////////////////////////////////////////////////

bool EditView::Create(HWND parent, HINSTANCE hInstance)
{
    m_parent = parent;
    m_hInstance = hInstance;

    WNDCLASSW wc = {};

    wc.lpfnWndProc = EditView::WndProcThunk;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = L"PdfEditorEditView";

    RegisterClassW(&wc);

    m_hwnd = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        wc.lpszClassName,
        L"",
        WS_CHILD | WS_BORDER,
        0,
        0,
        100,
        100,
        parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_EDIT_VIEW)),
        hInstance,
        this
    );

    if (!m_hwnd)
    {
        return false;
    }

    return true;
}

LRESULT CALLBACK EditView::WndProcThunk(
    HWND hwnd,
    UINT msg,
    WPARAM wParam,
    LPARAM lParam)
{
    EditView* self = nullptr;

    if (msg == WM_NCCREATE)
    {
        CREATESTRUCTW* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);

        self = reinterpret_cast<EditView*>(cs->lpCreateParams);

        SetWindowLongPtrW(
            hwnd,
            GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(self)
        );

        self->m_hwnd = hwnd;
    }
    else
    {
        self = reinterpret_cast<EditView*>(
            GetWindowLongPtrW(hwnd, GWLP_USERDATA)
            );
    }

    if (self)
    {
        return self->WndProc(msg, wParam, lParam);
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT EditView::WndProc(UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE:
    {
        m_hFont = CreateFontW(
            20,
            0,
            0,
            0,
            FW_NORMAL,
            FALSE,
            FALSE,
            FALSE,
            DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE,
            L"Segoe UI"
        );

        return 0;
    }

    case WM_ERASEBKGND:
    {
        return 1;
    }

    case WM_SIZE:
    {
        CalculateLayout();
        InvalidateRect(m_hwnd, nullptr, TRUE);
        return 0;
    }

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(m_hwnd, &ps);

        RECT rc;
        GetClientRect(m_hwnd, &rc);

        HBRUSH backgroundBrush = CreateSolidBrush(RGB(220, 220, 220));
        FillRect(hdc, &rc, backgroundBrush);
        DeleteObject(backgroundBrush);

        if (m_pageBitmap)
        {
            HDC memDC = CreateCompatibleDC(hdc);

            HBITMAP oldBitmap = reinterpret_cast<HBITMAP>(
                SelectObject(memDC, m_pageBitmap)
                );

            BitBlt(
                hdc,
                static_cast<int>(m_offsetX),
                static_cast<int>(m_offsetY),
                m_pageBitmapSize.cx,
                m_pageBitmapSize.cy,
                memDC,
                0,
                0,
                SRCCOPY
            );

            SelectObject(memDC, oldBitmap);
            DeleteDC(memDC);
        }

        DrawOverlay(hdc);

        EndPaint(m_hwnd, &ps);
        return 0;
    }

    case WM_LBUTTONDOWN:
    {
        if (!m_page)
        {
            return 0;
        }

        if (m_editorVisible)
        {
            CommitEdit();
            return 0;
        }

        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);

        double pdfX = ClientXToPdfX(x);
        double pdfY = ClientYToPdfY(y);

        int wordIndex = HitTestWord(pdfX, pdfY);

        if (wordIndex >= 0)
        {
            ShowEditorForWord(wordIndex);
        }
        else
        {
            ShowEditorForNewText(pdfX, pdfY);
        }

        return 0;
    }

    case WM_DESTROY:
    {
        if (m_hEdit)
        {
            DestroyWindow(m_hEdit);
            m_hEdit = nullptr;
        }

        if (m_hFont)
        {
            DeleteObject(m_hFont);
            m_hFont = nullptr;
        }

        if (m_pageBitmap)
        {
            DeleteObject(m_pageBitmap);
            m_pageBitmap = nullptr;
        }

        CleanupPdf();

        return 0;
    }

    default:
        return DefWindowProcW(m_hwnd, msg, wParam, lParam);
    }
}

LRESULT CALLBACK EditView::EditSubProc(
    HWND hWnd,
    UINT msg,
    WPARAM wParam,
    LPARAM lParam,
    UINT_PTR uIdSubclass,
    DWORD_PTR dwRefData)
{
    EditView* view = reinterpret_cast<EditView*>(dwRefData);

    if (!view)
    {
        return DefSubclassProc(hWnd, msg, wParam, lParam);
    }

    if (msg == WM_KEYDOWN)
    {
        if (wParam == VK_RETURN)
        {
            view->CommitEdit();
            return 0;
        }

        if (wParam == VK_ESCAPE)
        {
            view->CancelEdit();
            return 0;
        }
    }

    if (msg == WM_DESTROY)
    {
        RemoveWindowSubclass(hWnd, EditSubProc, uIdSubclass);
    }

    return DefSubclassProc(hWnd, msg, wParam, lParam);
}

void EditView::CreateEditControl()
{
    if (m_hEdit)
    {
        return;
    }

    m_hEdit = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        L"EDIT",
        L"",
        WS_CHILD | ES_AUTOHSCROLL,
        0,
        0,
        200,
        28,
        m_hwnd,
        nullptr,
        m_hInstance,
        nullptr
    );

    if (!m_hEdit)
    {
        return;
    }

    if (m_hFont)
    {
        SendMessageW(
            m_hEdit,
            WM_SETFONT,
            reinterpret_cast<WPARAM>(m_hFont),
            TRUE
        );
    }

    SetWindowSubclass(
        m_hEdit,
        EditSubProc,
        1,
        reinterpret_cast<DWORD_PTR>(this)
    );
}

void EditView::OpenPage(const PageRef& pageRef)
{
    HideEditor();

    CleanupPdf();

    m_patches.clear();
    m_words.clear();

    std::string error;

    if (!ReadFileToVector(pageRef.sourcePath, m_fileData, error))
    {
        ShowErrorAnsi(m_hwnd, error);
        return;
    }

    m_doc = FPDF_LoadMemDocument(
        m_fileData.data(),
        m_fileData.size(),
        nullptr
    );

    if (!m_doc)
    {
        ShowErrorW(m_hwnd, L"PDFium cannot open document.");
        CleanupPdf();
        return;
    }

    int pageCount = FPDF_GetPageCount(m_doc);

    if (pageRef.pageIndex < 0 || pageRef.pageIndex >= pageCount)
    {
        ShowErrorW(m_hwnd, L"Invalid page index.");
        CleanupPdf();
        return;
    }

    m_pageIndex = pageRef.pageIndex;

    m_page = FPDF_LoadPage(m_doc, m_pageIndex);

    if (!m_page)
    {
        ShowErrorW(m_hwnd, L"PDFium cannot load page.");
        CleanupPdf();
        return;
    }

    m_pageWidth = FPDF_GetPageWidth(m_page);
    m_pageHeight = FPDF_GetPageHeight(m_page);

    if (m_pageWidth <= 0.0 || m_pageHeight <= 0.0)
    {
        m_pageWidth = 612.0;
        m_pageHeight = 792.0;
    }

    std::string extractError;

    m_textExtractor.ExtractWords(
        m_page,
        m_words,
        extractError
    );

    CalculateLayout();
    RenderPage();

    InvalidateRect(m_hwnd, nullptr, TRUE);
}

void EditView::CleanupPdf()
{
    if (m_page)
    {
        FPDF_ClosePage(m_page);
        m_page = nullptr;
    }

    if (m_doc)
    {
        FPDF_CloseDocument(m_doc);
        m_doc = nullptr;
    }

    m_fileData.clear();

    if (m_pageBitmap)
    {
        DeleteObject(m_pageBitmap);
        m_pageBitmap = nullptr;
    }

    m_pageBitmapSize = { 0, 0 };
}

void EditView::CalculateLayout()
{
    if (!m_page)
    {
        return;
    }

    RECT rc;
    GetClientRect(m_hwnd, &rc);

    int clientWidth = rc.right;
    int clientHeight = rc.bottom;

    if (clientWidth <= 0 || clientHeight <= 0)
    {
        return;
    }

    double zoomWidth = static_cast<double>(clientWidth) / m_pageWidth;
    double zoomHeight = static_cast<double>(clientHeight) / m_pageHeight;

    m_zoom = zoomWidth < zoomHeight ? zoomWidth : zoomHeight;

    if (m_zoom <= 0.0)
    {
        m_zoom = 1.0;
    }

    double renderedWidth = m_pageWidth * m_zoom;
    double renderedHeight = m_pageHeight * m_zoom;

    m_offsetX = (clientWidth - renderedWidth) / 2.0;
    m_offsetY = (clientHeight - renderedHeight) / 2.0;

    RenderPage();
}

void EditView::RenderPage()
{
    if (m_pageBitmap)
    {
        DeleteObject(m_pageBitmap);
        m_pageBitmap = nullptr;
    }

    m_pageBitmapSize = { 0, 0 };

    if (!m_page)
    {
        return;
    }

    int width = static_cast<int>((m_pageWidth * m_zoom) + 0.5);
    int height = static_cast<int>((m_pageHeight * m_zoom) + 0.5);

    if (width <= 0 || height <= 0)
    {
        return;
    }

    FPDF_BITMAP fpdfBitmap = FPDFBitmap_Create(width, height, 1);

    if (!fpdfBitmap)
    {
        return;
    }

    FPDFBitmap_FillRect(fpdfBitmap, 0, 0, width, height, 0xFFFFFFFF);

    FPDF_RenderPageBitmap(
        fpdfBitmap,
        m_page,
        0,
        0,
        width,
        height,
        0,
        FPDF_ANNOT
    );

    m_pageBitmap = CreateDIBFromFPDFBitmap(fpdfBitmap);

    if (m_pageBitmap)
    {
        m_pageBitmapSize.cx = width;
        m_pageBitmapSize.cy = height;
    }

    FPDFBitmap_Destroy(fpdfBitmap);
}

RECT EditView::PdfRectToClientRect(
    double left,
    double bottom,
    double right,
    double top) const
{
    if (left > right)
    {
        std::swap(left, right);
    }

    if (bottom > top)
    {
        std::swap(bottom, top);
    }

    double clientLeft = m_offsetX + (left * m_zoom);
    double clientRight = m_offsetX + (right * m_zoom);

    double clientTop =
        m_offsetY + ((m_pageHeight - top) * m_zoom);

    double clientBottom =
        m_offsetY + ((m_pageHeight - bottom) * m_zoom);

    RECT rc;

    rc.left = static_cast<LONG>(floor(clientLeft));
    rc.top = static_cast<LONG>(floor(clientTop));
    rc.right = static_cast<LONG>(ceil(clientRight));
    rc.bottom = static_cast<LONG>(ceil(clientBottom));

    return rc;
}

double EditView::ClientXToPdfX(int clientX) const
{
    if (m_zoom <= 0.0)
    {
        return 0.0;
    }

    return (clientX - m_offsetX) / m_zoom;
}

double EditView::ClientYToPdfY(int clientY) const
{
    if (m_zoom <= 0.0)
    {
        return 0.0;
    }

    double fromTop = (clientY - m_offsetY) / m_zoom;

    return m_pageHeight - fromTop;
}

int EditView::HitTestWord(double pdfX, double pdfY) const
{
    for (size_t i = 0; i < m_words.size(); i++)
    {
        const PdfWord& word = m_words[i];

        if (pdfX >= word.left &&
            pdfX <= word.right &&
            pdfY >= word.bottom &&
            pdfY <= word.top)
        {
            return static_cast<int>(i);
        }
    }

    return -1;
}

void EditView::ShowEditorForWord(int wordIndex)
{
    if (wordIndex < 0 || wordIndex >= static_cast<int>(m_words.size()))
    {
        return;
    }

    const PdfWord& word = m_words[wordIndex];

    EditTextPatch patch;

    patch.pageIndex = m_pageIndex;

    patch.left = word.left;
    patch.bottom = word.bottom;
    patch.right = word.right;
    patch.top = word.top;

    patch.fontSize = word.fontSize;

    patch.oldText = word.text;
    patch.newText = word.text;

    ShowEditorForPatch(patch);
}

void EditView::ShowEditorForNewText(double pdfX, double pdfY)
{
    EditTextPatch patch;

    patch.pageIndex = m_pageIndex;

    patch.left = pdfX;
    patch.bottom = pdfY - 4.0;
    patch.right = pdfX + 150.0;
    patch.top = pdfY + 14.0;

    patch.fontSize = 12.0;

    patch.oldText = L"";
    patch.newText = L"";

    ShowEditorForPatch(patch);
}

void EditView::ShowEditorForPatch(const EditTextPatch& patch)
{
    CreateEditControl();

    if (!m_hEdit)
    {
        return;
    }

    m_currentPatch = patch;
    m_hasCurrentPatch = true;

    RECT rc = PdfRectToClientRect(
        patch.left,
        patch.bottom,
        patch.right,
        patch.top
    );

    int editWidth = rc.right - rc.left + 100;

    if (editWidth < 220)
    {
        editWidth = 220;
    }

    int editHeight = 28;

    int x = rc.left;
    int y = rc.top - editHeight - 4;

    if (y < 0)
    {
        y = rc.bottom + 4;
    }

    SetWindowTextW(m_hEdit, patch.newText.c_str());

    MoveWindow(
        m_hEdit,
        x,
        y,
        editWidth,
        editHeight,
        TRUE
    );

    ShowWindow(m_hEdit, SW_SHOW);
    SetFocus(m_hEdit);

    SendMessageW(m_hEdit, EM_SETSEL, 0, -1);

    m_editorVisible = true;
}

void EditView::CommitEdit()
{
    if (!m_editorVisible || !m_hEdit || !m_hasCurrentPatch)
    {
        return;
    }

    wchar_t buffer[2048] = {};

    GetWindowTextW(m_hEdit, buffer, 2048);

    m_currentPatch.newText = buffer;

    bool changed = m_currentPatch.newText != m_currentPatch.oldText;

    if (changed)
    {
        AddOrUpdatePatch(m_currentPatch);
    }

    HideEditor();

    InvalidateRect(m_hwnd, nullptr, TRUE);
}

void EditView::CancelEdit()
{
    HideEditor();
}

void EditView::HideEditor()
{
    if (m_hEdit)
    {
        ShowWindow(m_hEdit, SW_HIDE);
    }

    m_editorVisible = false;
    m_hasCurrentPatch = false;
}

void EditView::AddOrUpdatePatch(const EditTextPatch& patch)
{
    auto it = std::find_if(
        m_patches.begin(),
        m_patches.end(),
        [&](const EditTextPatch& existing)
        {
            return existing.pageIndex == patch.pageIndex &&
                fabs(existing.left - patch.left) < 0.5 &&
                fabs(existing.right - patch.right) < 0.5 &&
                fabs(existing.bottom - patch.bottom) < 0.5 &&
                fabs(existing.top - patch.top) < 0.5;
        }
    );

    if (it != m_patches.end())
    {
        *it = patch;
    }
    else
    {
        m_patches.push_back(patch);
    }
}

void EditView::DrawOverlay(HDC hdc)
{
    for (const EditTextPatch& patch : m_patches)
    {
        RECT rc = PdfRectToClientRect(
            patch.left,
            patch.bottom,
            patch.right,
            patch.top
        );

        HBRUSH whiteBrush = CreateSolidBrush(RGB(255, 255, 255));
        FillRect(hdc, &rc, whiteBrush);
        DeleteObject(whiteBrush);

        if (!patch.newText.empty())
        {
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(0, 0, 0));

            DrawTextW(
                hdc,
                patch.newText.c_str(),
                -1,
                &rc,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX
            );
        }
    }
}

void EditView::SaveEditsAs()
{
    if (!m_doc || !m_page)
    {
        ShowInfoW(m_hwnd, L"No PDF page is open.");
        return;
    }

    if (m_patches.empty())
    {
        ShowInfoW(m_hwnd, L"No text edits to save.");
        return;
    }

    ShowInfoW(
        m_hwnd,
        L"Text edit preview is working.\n\n"
        L"This PDFium build does not provide reliable text-content saving.\n\n"
        L"To save text edits into PDF, you need one of these:\n"
        L"1. PoDoFo\n"
        L"2. PDF-XChange SDK\n"
        L"3. Foxit PDF SDK\n"
        L"4. Another commercial PDF editing SDK"
    );
}

bool EditView::ApplyPatchesToDocument(std::string& error)
{
    error =
        "This PDFium build does not support saving text edits. "
        "Use PoDoFo or a commercial PDF SDK.";

    return false;
}

bool EditView::SaveDocumentToFile(
    const std::wstring& outputPath,
    std::string& error)
{
    error =
        "This PDFium build does not support saving text edits. "
        "Use PoDoFo or a commercial PDF SDK.";

    return false;
}