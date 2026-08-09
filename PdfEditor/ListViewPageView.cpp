#include "ListViewPageView.h"

#include <string>

bool ListViewPageView::Create(HWND parent, HINSTANCE hInstance)
{
    m_hwnd = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        WC_LISTVIEWW,
        L"",
        WS_CHILD |
        WS_VISIBLE |
        WS_BORDER |
        LVS_ICON |
        LVS_SINGLESEL |
        LVS_SHOWSELALWAYS |
        LVS_AUTOARRANGE,
        0,
        0,
        100,
        100,
        parent,
        nullptr,
        hInstance,
        nullptr
    );

    if (!m_hwnd)
    {
        return false;
    }

    ListView_SetExtendedListViewStyle(
        m_hwnd,
        LVS_EX_DOUBLEBUFFER
    );

    SendMessageW(
        m_hwnd,
        LVM_SETICONSPACING,
        0,
        MAKELPARAM(150, 190)
    );

    return true;
}

void ListViewPageView::Resize(int width, int height)
{
    if (m_hwnd)
    {
        MoveWindow(m_hwnd, 0, 0, width, height, TRUE);
    }
}

void ListViewPageView::SetImageList(HIMAGELIST imageList)
{
    if (m_hwnd)
    {
        ListView_SetImageList(m_hwnd, imageList, LVSIL_NORMAL);
    }
}

void ListViewPageView::RefreshPages(const std::vector<PageRef>& pages)
{
    if (!m_hwnd)
    {
        return;
    }

    ListView_DeleteAllItems(m_hwnd);

    for (size_t i = 0; i < pages.size(); i++)
    {
        std::wstring text = L"Page " + std::to_wstring(i + 1);

        LVITEMW item = {};
        item.mask = LVIF_TEXT | LVIF_IMAGE;
        item.iItem = static_cast<int>(i);
        item.iSubItem = 0;
        item.pszText = const_cast<LPWSTR>(text.c_str());
        item.iImage = pages[i].thumbnailIndex;

        ListView_InsertItem(m_hwnd, &item);
    }
}

int ListViewPageView::GetSelectedIndex() const
{
    if (!m_hwnd)
    {
        return -1;
    }

    return ListView_GetNextItem(m_hwnd, -1, LVNI_SELECTED);
}

void ListViewPageView::SelectIndex(int index)
{
    if (!m_hwnd || index < 0)
    {
        return;
    }

    ListView_SetItemState(
        m_hwnd,
        index,
        LVIS_SELECTED | LVIS_FOCUSED,
        LVIS_SELECTED | LVIS_FOCUSED
    );

    ListView_EnsureVisible(m_hwnd, index, FALSE);
}