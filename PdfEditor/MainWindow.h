#pragma once

#include <windows.h>
#include <commctrl.h>
#include <string>

#include "ListViewPageView.h"
#include "PageModel.h"

class App;

class MainWindow
{
public:
    bool Create(HINSTANCE hInstance, App* app);

    HWND GetHwnd() const
    {
        return m_hwnd;
    }

    ListViewPageView& GetPageView()
    {
        return m_pageView;
    }

    void SetStatus(const std::wstring& text);

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

    HMENU CreateMainMenu();
    void CreateControls();
    void CreateToolbar();
    void CreateStatus();
    void Layout(int width, int height);

    App* m_app = nullptr;
    HINSTANCE hInstance = nullptr;

    HWND m_hwnd = nullptr;
    HWND m_toolbar = nullptr;
    HWND m_status = nullptr;

    ListViewPageView m_pageView;
};