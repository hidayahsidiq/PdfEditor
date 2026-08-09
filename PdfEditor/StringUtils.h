#pragma once

#include <windows.h>
#include <string>
#include <vector>

std::string WideToAnsi(const std::wstring& wide);
std::wstring AnsiToWide(const std::string& ansi);

std::wstring FileNameFromPath(const std::wstring& path);
std::wstring RemoveExtension(const std::wstring& path);
std::wstring EnsurePdfExtension(const std::wstring& path);
std::wstring GetShortPath(const std::wstring& path);

int NormalizeRotation(int rotation);

bool ReadFileToVector(
    const std::wstring& path,
    std::vector<unsigned char>& out,
    std::string& error
);

void ShowErrorW(HWND hwnd, const std::wstring& message);
void ShowErrorAnsi(HWND hwnd, const std::string& message);
void ShowInfoW(HWND hwnd, const std::wstring& message);

bool OpenPdfFiles(HWND hwnd, std::vector<std::wstring>& files);
bool SavePdfFile(HWND hwnd, std::wstring& savePath);