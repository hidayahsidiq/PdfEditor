#pragma once

#include <string>

struct PageRef
{
    std::wstring sourcePath;
    int pageIndex = 0;
    int rotateDelta = 0;
    int thumbnailIndex = 0;
};

enum CommandId
{
    ID_CMD_OPEN = 5000,
    ID_CMD_ADD,
    ID_CMD_SAVEAS,
    ID_CMD_EXIT,
    ID_CMD_ROTATE_LEFT,
    ID_CMD_ROTATE_RIGHT,
    ID_CMD_MOVE_UP,
    ID_CMD_MOVE_DOWN,
    ID_CMD_DELETE_PAGE,
    ID_CMD_EXTRACT_SELECTED,
    ID_CMD_SPLIT_ALL,
    ID_CMD_ABOUT,

    ID_CMD_EDIT_PAGE_TEXT,
    ID_CMD_BACK_TO_PAGES
};

constexpr int IDC_EDIT_VIEW = 9001;