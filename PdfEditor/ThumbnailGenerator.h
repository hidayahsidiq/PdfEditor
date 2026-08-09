#pragma once
#pragma once

#include <windows.h>
#include <string>

class ThumbnailGenerator
{
public:
    ~ThumbnailGenerator();

    void Initialize();
    void Shutdown();

    bool GenerateThumbnail(
        const std::wstring& pdfPath,
        int pageIndex,
        int maxWidth,
        int maxHeight,
        HBITMAP& outBitmap,
        std::string& error
    );

    // Compatibility overload for your existing App.cpp code.
    bool GenerateThumbnail(
        const std::wstring& pdfPath,
        int pageIndex,
        int targetWidth,
        HBITMAP& outBitmap,
        std::string& error
    )
    {
        return GenerateThumbnail(
            pdfPath,
            pageIndex,
            targetWidth,
            160,
            outBitmap,
            error
        );
    }

    static HBITMAP CreatePlaceholderBitmap(int width, int height);

private:
    bool m_initialized = false;
};