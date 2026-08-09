#pragma once

#include <windows.h>
#include <commctrl.h>

#include <string>
#include <vector>

#include <fpdfview.h>
#include <fpdf_text.h>

#include "EditTextModel.h"
#include "PageModel.h"
#include "PdfTextExtractor.h"

class EditView
{
public:
    bool Create(HWND parent, HINSTANCE hInstance);

    void OpenPage(const PageRef& pageRef);

    void SaveEditsAs();

    HWND GetHwnd() const
    {
        return m_hwnd;
    }

private:
    static LRESULT CALLBACK WndProcThunk(
        HWND hwnd,
        UINT msg,
        WPARAM wParam,
        LPARAM lParam
    );

    LRESULT WndProc(
        UINT msg,
        WPARAM wParam,
        LPARAM lParam
    );

    static LRESULT CALLBACK EditSubProc(
        HWND hWnd,
        UINT msg,
        WPARAM wParam,
        LPARAM lParam,
        UINT_PTR uIdSubclass,
        DWORD_PTR dwRefData
    );

    void CreateEditControl();
    void ShowEditorForWord(int wordIndex);
    void ShowEditorForNewText(double pdfX, double pdfY);
    void ShowEditorForPatch(const EditTextPatch& patch);

    void CommitEdit();
    void CancelEdit();
    void HideEditor();

    void CalculateLayout();
    void RenderPage();

    void DrawOverlay(HDC hdc);

    RECT PdfRectToClientRect(
        double left,
        double bottom,
        double right,
        double top
    ) const;

    double ClientXToPdfX(int clientX) const;
    double ClientYToPdfY(int clientY) const;

    int HitTestWord(double pdfX, double pdfY) const;

    void AddOrUpdatePatch(const EditTextPatch& patch);

    bool ApplyPatchesToDocument(std::string& error);
    bool SaveDocumentToFile(
        const std::wstring& outputPath,
        std::string& error
    );

    void CleanupPdf();

    HWND m_hwnd = nullptr;
    HWND m_parent = nullptr;
    HINSTANCE m_hInstance = nullptr;

    HWND m_hEdit = nullptr;
    HFONT m_hFont = nullptr;

    std::vector<unsigned char> m_fileData;

    FPDF_DOCUMENT m_doc = nullptr;
    FPDF_PAGE m_page = nullptr;

    int m_pageIndex = 0;

    double m_pageWidth = 0.0;
    double m_pageHeight = 0.0;

    double m_zoom = 1.0;
    double m_offsetX = 0.0;
    double m_offsetY = 0.0;

    std::vector<PdfWord> m_words;
    std::vector<EditTextPatch> m_patches;

    EditTextPatch m_currentPatch;
    bool m_hasCurrentPatch = false;
    bool m_editorVisible = false;

    HBITMAP m_pageBitmap = nullptr;
    SIZE m_pageBitmapSize = { 0, 0 };

    PdfTextExtractor m_textExtractor;
};