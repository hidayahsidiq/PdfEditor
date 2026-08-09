#include "StringUtils.h"

#include <commdlg.h>
#include <wchar.h>

std::string WideToAnsi(const std::wstring& wide)
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

    std::string result(static_cast<size_t>(sizeNeeded), 0);

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

std::wstring AnsiToWide(const std::string& ansi)
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

    std::wstring result(static_cast<size_t>(sizeNeeded), 0);

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

std::wstring FileNameFromPath(const std::wstring& path)
{
    size_t pos = path.find_last_of(L"\\/");

    if (pos == std::wstring::npos)
    {
        return path;
    }

    return path.substr(pos + 1);
}

std::wstring RemoveExtension(const std::wstring& path)
{
    size_t dot = path.find_last_of(L'.');
    size_t slash = path.find_last_of(L"\\/");

    if (dot != std::wstring::npos)
    {
        if (slash == std::wstring::npos || dot > slash)
        {
            return path.substr(0, dot);
        }
    }

    return path;
}

std::wstring EnsurePdfExtension(const std::wstring& path)
{
    if (path.size() >= 4)
    {
        std::wstring ext = path.substr(path.size() - 4);

        if (_wcsicmp(ext.c_str(), L".pdf") == 0)
        {
            return path;
        }
    }

    return path + L".pdf";
}

std::wstring GetShortPath(const std::wstring& path)
{
    DWORD needed = GetShortPathNameW(path.c_str(), nullptr, 0);

    if (needed == 0)
    {
        return path;
    }

    std::vector<wchar_t> buffer(static_cast<size_t>(needed), 0);

    DWORD length = GetShortPathNameW(
        path.c_str(),
        buffer.data(),
        needed
    );

    if (length == 0)
    {
        return path;
    }

    return std::wstring(buffer.data(), length);
}

int NormalizeRotation(int rotation)
{
    rotation %= 360;

    if (rotation < 0)
    {
        rotation += 360;
    }

    return rotation;
}

bool ReadFileToVector(
    const std::wstring& path,
    std::vector<unsigned char>& out,
    std::string& error)
{
    out.clear();

    HANDLE hFile = CreateFileW(
        path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    if (hFile == INVALID_HANDLE_VALUE)
    {
        error = "Cannot open file: " + WideToAnsi(path);
        return false;
    }

    LARGE_INTEGER fileSize;

    if (!GetFileSizeEx(hFile, &fileSize))
    {
        CloseHandle(hFile);
        error = "Cannot get file size: " + WideToAnsi(path);
        return false;
    }

    if (fileSize.QuadPart == 0)
    {
        CloseHandle(hFile);
        error = "File is empty: " + WideToAnsi(path);
        return false;
    }

    out.resize(static_cast<size_t>(fileSize.QuadPart));

    DWORD bytesRead = 0;

    BOOL ok = ReadFile(
        hFile,
        out.data(),
        static_cast<DWORD>(out.size()),
        &bytesRead,
        nullptr
    );

    CloseHandle(hFile);

    if (!ok || bytesRead != static_cast<DWORD>(out.size()))
    {
        error = "Cannot read file: " + WideToAnsi(path);
        return false;
    }

    return true;
}

void ShowErrorW(HWND hwnd, const std::wstring& message)
{
    MessageBoxW(hwnd, message.c_str(), L"PDF Editor", MB_ICONERROR);
}

void ShowErrorAnsi(HWND hwnd, const std::string& message)
{
    MessageBoxW(hwnd, AnsiToWide(message).c_str(), L"PDF Editor", MB_ICONERROR);
}

void ShowInfoW(HWND hwnd, const std::wstring& message)
{
    MessageBoxW(hwnd, message.c_str(), L"PDF Editor", MB_ICONINFORMATION);
}

bool OpenPdfFiles(HWND hwnd, std::vector<std::wstring>& files)
{
    files.clear();

    const DWORD bufferSize = 65536;
    std::vector<wchar_t> buffer(bufferSize, 0);

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

    std::wstring first = buffer.data();
    size_t pos = first.size() + 1;

    if (pos >= bufferSize || buffer[pos] == 0)
    {
        files.push_back(first);
        return true;
    }

    while (pos < bufferSize && buffer[pos] != 0)
    {
        std::wstring fileName = &buffer[pos];

        std::wstring fullPath = first;

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

bool SavePdfFile(HWND hwnd, std::wstring& savePath)
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