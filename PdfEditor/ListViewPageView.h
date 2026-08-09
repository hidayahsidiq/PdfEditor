#pragma once

#include <windows.h>
#include <commctrl.h>
#include <vector>

#include "PageModel.h"

class ListViewPageView
{
public:
    bool Create(HWND parent, HINSTANCE hInstance);

    void Resize(int width, int height);
    void SetImageList(HIMAGELIST imageList);

    void RefreshPages(const std::vector<PageRef>& pages);

    int GetSelectedIndex() const;
    void SelectIndex(int index);

    HWND GetHwnd() const
    {
        return m_hwnd;
    }

private:
    HWND m_hwnd = nullptr;
};