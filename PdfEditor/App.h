#pragma once

#include <windows.h>
#include <vector>

#include "MainWindow.h"
#include "PdfEngine.h"
#include "ThumbnailGenerator.h"
#include "PageModel.h"
#include "EditView.h"

enum class UiMode
{
    Thumbnails,
    EditPage
};

class App
{
public:
    int Run(HINSTANCE hInstance);
    void OnCommand(int commandId);

private:
    void InitializeImageList();

    void OpenFiles(bool clearExisting);
    void SaveAs();
    void ExtractSelected();
    void SplitAll();

    void RotateSelected(int amount);
    void MoveSelected(int direction);
    void DeleteSelected();

    void RefreshUi();
    void GenerateThumbnails();
    void UpdateStatus();

    bool IsCurrentSourceFile(const std::wstring& path) const;

    void EditSelectedPage();
    void ShowThumbnails();

    MainWindow m_mainWindow;
    PdfEngine m_pdfEngine;
    ThumbnailGenerator m_thumbnailGenerator;

    HIMAGELIST m_imageList = nullptr;

    std::vector<PageRef> m_pages;

    HINSTANCE m_hInstance = nullptr;

    EditView m_editView;
    UiMode m_mode = UiMode::Thumbnails;
};