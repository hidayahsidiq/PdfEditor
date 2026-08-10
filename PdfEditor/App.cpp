#include "App.h"
#include "StringUtils.h"

#include <commctrl.h>
#include <wchar.h>
#include <algorithm>
#include <string>

int App::Run(HINSTANCE hInstance)
{
    m_hInstance = hInstance;

    m_thumbnailGenerator.Initialize();

    if (!m_mainWindow.Create(hInstance, this))
    {
        return 0;
    }

    InitializeImageList();

    if (!m_editView.Create(m_mainWindow.GetHwnd(), hInstance))
    {
        return 0;
    }

    ShowWindow(m_editView.GetHwnd(), SW_HIDE);

    UpdateStatus();

    MSG msg = {};

    while (GetMessageW(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    m_thumbnailGenerator.Shutdown();

    return static_cast<int>(msg.wParam);
}

void App::InitializeImageList()
{
    m_imageList = ImageList_Create(
        120,
        160,
        ILC_COLOR32,
        1,
        100
    );

    HBITMAP placeholder = ThumbnailGenerator::CreatePlaceholderBitmap(120, 160);

    if (placeholder)
    {
        ImageList_Add(m_imageList, placeholder, nullptr);
        DeleteObject(placeholder);
    }

    m_mainWindow.GetPageView().SetImageList(m_imageList);
}

void App::OnCommand(int commandId)
{
    switch (commandId)
    {
    case ID_CMD_EXIT:
    {
        DestroyWindow(m_mainWindow.GetHwnd());
        break;
    }

    case ID_CMD_OPEN:
    {
        OpenFiles(true);
        break;
    }

    case ID_CMD_ADD:
    {
        OpenFiles(false);
        break;
    }

    case ID_CMD_SAVEAS:
    {
        if (m_mode == UiMode::EditPage)
        {
            m_editView.SaveEditsAs();
        }
        else
        {
            SaveAs();
        }

        break;
    }

    case ID_CMD_EXTRACT_SELECTED:
    {
        ExtractSelected();
        break;
    }

    case ID_CMD_SPLIT_ALL:
    {
        SplitAll();
        break;
    }

    case ID_CMD_ROTATE_LEFT:
    {
        RotateSelected(-90);
        break;
    }

    case ID_CMD_ROTATE_RIGHT:
    {
        RotateSelected(90);
        break;
    }

    case ID_CMD_MOVE_UP:
    {
        MoveSelected(-1);
        break;
    }

    case ID_CMD_MOVE_DOWN:
    {
        MoveSelected(1);
        break;
    }

    case ID_CMD_DELETE_PAGE:
    {
        DeleteSelected();
        break;
    }

    case ID_CMD_ABOUT:
    {
        ShowInfoW(
            m_mainWindow.GetHwnd(),
            L"Offline PDF Editor\n\nQPDF + PDFium + Win32"
        );

        break;
    }

    case ID_CMD_EDIT_PAGE_TEXT:
    {
        EditSelectedPage();
        break;
    }

    case ID_CMD_BACK_TO_PAGES:
    {
        ShowThumbnails();
        break;
    }
    }
}

void App::OpenFiles(bool clearExisting)
{
    std::vector<std::wstring> files;

    if (!OpenPdfFiles(m_mainWindow.GetHwnd(), files))
    {
        return;
    }

    if (clearExisting)
    {
        m_pages.clear();
        m_pdfEngine.ClearCache();

        if (m_imageList)
        {
            ImageList_RemoveAll(m_imageList);

            HBITMAP placeholder = ThumbnailGenerator::CreatePlaceholderBitmap(120, 160);

            if (placeholder)
            {
                ImageList_Add(m_imageList, placeholder, nullptr);
                DeleteObject(placeholder);
            }
        }
    }

    size_t originalSize = m_pages.size();

    try
    {
        for (const std::wstring& file : files)
        {
            int pageCount = m_pdfEngine.GetPageCount(file);

            for (int i = 0; i < pageCount; i++)
            {
                PageRef ref;
                ref.sourcePath = file;
                ref.pageIndex = i;
                ref.rotateDelta = 0;
                ref.thumbnailIndex = 0;

                m_pages.push_back(ref);
            }
        }
    }
    catch (const std::exception& e)
    {
        m_pages.resize(originalSize);
        ShowErrorAnsi(m_mainWindow.GetHwnd(), e.what());
        return;
    }

    RefreshUi();
    GenerateThumbnails();
}

void App::SaveAs()
{
    if (m_pages.empty())
    {
        ShowInfoW(m_mainWindow.GetHwnd(), L"No pages loaded.");
        return;
    }

    std::wstring savePath;

    if (!SavePdfFile(m_mainWindow.GetHwnd(), savePath))
    {
        return;
    }

    if (IsCurrentSourceFile(savePath))
    {
        ShowErrorW(
            m_mainWindow.GetHwnd(),
            L"You cannot save over a source file while it is loaded.\n"
            L"Please choose a different file name."
        );

        return;
    }

    std::string error;

    if (m_pdfEngine.BuildPdf(m_pages, savePath, error))
    {
        ShowInfoW(
            m_mainWindow.GetHwnd(),
            L"PDF saved successfully:\n" + savePath
        );
    }
    else
    {
        ShowErrorAnsi(m_mainWindow.GetHwnd(), error);
    }
}

void App::ExtractSelected()
{
    int index = m_mainWindow.GetPageView().GetSelectedIndex();

    if (index < 0)
    {
        ShowInfoW(m_mainWindow.GetHwnd(), L"Select a page first.");
        return;
    }

    std::wstring savePath;

    if (!SavePdfFile(m_mainWindow.GetHwnd(), savePath))
    {
        return;
    }

    if (IsCurrentSourceFile(savePath))
    {
        ShowErrorW(
            m_mainWindow.GetHwnd(),
            L"You cannot save over a source file while it is loaded.\n"
            L"Please choose a different file name."
        );

        return;
    }

    std::vector<PageRef> onePage;
    onePage.push_back(m_pages[index]);

    std::string error;

    if (m_pdfEngine.BuildPdf(onePage, savePath, error))
    {
        ShowInfoW(
            m_mainWindow.GetHwnd(),
            L"Selected page saved successfully:\n" + savePath
        );
    }
    else
    {
        ShowErrorAnsi(m_mainWindow.GetHwnd(), error);
    }
}

void App::SplitAll()
{
    if (m_pages.empty())
    {
        ShowInfoW(m_mainWindow.GetHwnd(), L"No pages loaded.");
        return;
    }

    std::wstring basePath;

    if (!SavePdfFile(m_mainWindow.GetHwnd(), basePath))
    {
        return;
    }

    basePath = RemoveExtension(basePath);

    std::string error;

    for (size_t i = 0; i < m_pages.size(); i++)
    {
        std::wstring outputPath =
            basePath + L"_" + std::to_wstring(i + 1) + L".pdf";

        if (IsCurrentSourceFile(outputPath))
        {
            ShowErrorW(
                m_mainWindow.GetHwnd(),
                L"One of the output files is also a loaded source file:\n" +
                outputPath +
                L"\nPlease choose a different base output file."
            );

            return;
        }

        std::vector<PageRef> onePage;
        onePage.push_back(m_pages[i]);

        if (!m_pdfEngine.BuildPdf(onePage, outputPath, error))
        {
            ShowErrorAnsi(m_mainWindow.GetHwnd(), error);
            return;
        }
    }

    ShowInfoW(
        m_mainWindow.GetHwnd(),
        L"All pages exported as separate PDF files."
    );
}

void App::RotateSelected(int amount)
{
    int index = m_mainWindow.GetPageView().GetSelectedIndex();

    if (index < 0)
    {
        ShowInfoW(m_mainWindow.GetHwnd(), L"Select a page first.");
        return;
    }

    m_pages[index].rotateDelta =
        NormalizeRotation(m_pages[index].rotateDelta + amount);

    RefreshUi();
}

void App::MoveSelected(int direction)
{
    int index = m_mainWindow.GetPageView().GetSelectedIndex();

    if (index < 0)
    {
        ShowInfoW(m_mainWindow.GetHwnd(), L"Select a page first.");
        return;
    }

    int newIndex = index + direction;

    if (newIndex < 0 || newIndex >= static_cast<int>(m_pages.size()))
    {
        return;
    }

    std::iter_swap(m_pages.begin() + index, m_pages.begin() + newIndex);

    RefreshUi();
    m_mainWindow.GetPageView().SelectIndex(newIndex);
}

void App::DeleteSelected()
{
    int index = m_mainWindow.GetPageView().GetSelectedIndex();

    if (index < 0)
    {
        ShowInfoW(m_mainWindow.GetHwnd(), L"Select a page first.");
        return;
    }

    m_pages.erase(m_pages.begin() + index);

    RefreshUi();
}

void App::RefreshUi()
{
    m_mainWindow.GetPageView().RefreshPages(m_pages);
    UpdateStatus();
}

void App::GenerateThumbnails()
{
    if (!m_imageList)
    {
        return;
    }

    for (size_t i = 0; i < m_pages.size(); i++)
    {
        if (m_pages[i].thumbnailIndex > 0)
        {
            continue;
        }

        HBITMAP bitmap = nullptr;
        std::string error;

        bool ok = m_thumbnailGenerator.GenerateThumbnail(
            m_pages[i].sourcePath,
            m_pages[i].pageIndex,
            120,
            bitmap,
            error
        );

        if (ok && bitmap)
        {
            int imageIndex = ImageList_Add(m_imageList, bitmap, nullptr);
            DeleteObject(bitmap);

            m_pages[i].thumbnailIndex = imageIndex;
        }
    }

    RefreshUi();
}

void App::UpdateStatus()
{
    std::wstring text =
        L"Status: " +
        std::to_wstring(m_pages.size()) +
        L" pages loaded";

    m_mainWindow.SetStatus(text);
}

bool App::IsCurrentSourceFile(const std::wstring& path) const
{
    for (const PageRef& page : m_pages)
    {
        if (_wcsicmp(page.sourcePath.c_str(), path.c_str()) == 0)
        {
            return true;
        }
    }

    return false;
}

void App::EditSelectedPage()
{
    int index = m_mainWindow.GetPageView().GetSelectedIndex();

    if (index < 0)
    {
        ShowInfoW(
            m_mainWindow.GetHwnd(),
            L"Select a page first."
        );

        return;
    }

    if (index >= static_cast<int>(m_pages.size()))
    {
        return;
    }

    m_editView.OpenPage(m_pages[index]);

    ShowWindow(m_mainWindow.GetPageView().GetHwnd(), SW_HIDE);
    ShowWindow(m_editView.GetHwnd(), SW_SHOW);

    m_mode = UiMode::EditPage;

    SendMessageW(m_mainWindow.GetHwnd(), WM_SIZE, 0, 0);
    InvalidateRect(m_mainWindow.GetHwnd(), nullptr, TRUE);
}

void App::ShowThumbnails()
{
    ShowWindow(m_editView.GetHwnd(), SW_HIDE);
    ShowWindow(m_mainWindow.GetPageView().GetHwnd(), SW_SHOW);

    m_mode = UiMode::Thumbnails;

    SendMessageW(m_mainWindow.GetHwnd(), WM_SIZE, 0, 0);
    InvalidateRect(m_mainWindow.GetHwnd(), nullptr, TRUE);
}