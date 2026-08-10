#include "ThumbnailGenerator.h"
#include "StringUtils.h"

#include <fpdfview.h>
#include <vector>
#include <string.h>

// If your PDFium import library is named pdfium.lib,
// change this line to:
// #pragma comment(lib, "pdfium.lib")
#pragma comment(lib, "pdfium.dll.lib")

///////////////////////////////////////////////////////////////////////////////
// Helper: create fixed-size white canvas and center the rendered PDF bitmap.
///////////////////////////////////////////////////////////////////////////////

static HBITMAP CreateCenteredHBitmapFromFPDFBitmap(
    FPDF_BITMAP srcBitmap,
    int canvasWidth,
    int canvasHeight)
{
    if (!srcBitmap || canvasWidth <= 0 || canvasHeight <= 0)
    {
        return nullptr;
    }

    int srcWidth = FPDFBitmap_GetWidth(srcBitmap);
    int srcHeight = FPDFBitmap_GetHeight(srcBitmap);

    if (srcWidth <= 0 || srcHeight <= 0)
    {
        return nullptr;
    }

    const BYTE* srcBuffer = reinterpret_cast<const BYTE*>(
        FPDFBitmap_GetBuffer(srcBitmap)
        );

    if (!srcBuffer)
    {
        return nullptr;
    }

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = canvasWidth;
    bmi.bmiHeader.biHeight = -canvasHeight; // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;

    HBITMAP hBitmap = CreateDIBSection(
        nullptr,
        &bmi,
        DIB_RGB_COLORS,
        &bits,
        nullptr,
        0
    );

    if (!hBitmap)
    {
        return nullptr;
    }

    if (!bits)
    {
        DeleteObject(hBitmap);
        return nullptr;
    }

    DWORD* canvas = reinterpret_cast<DWORD*>(bits);

    // Fill with white.
    for (int i = 0; i < canvasWidth * canvasHeight; i++)
    {
        canvas[i] = 0xFFFFFFFF;
    }

    int copyWidth = srcWidth;
    int copyHeight = srcHeight;

    if (copyWidth > canvasWidth)
    {
        copyWidth = canvasWidth;
    }

    if (copyHeight > canvasHeight)
    {
        copyHeight = canvasHeight;
    }

    int offsetX = (canvasWidth - copyWidth) / 2;
    int offsetY = (canvasHeight - copyHeight) / 2;

    if (offsetX < 0)
    {
        offsetX = 0;
    }

    if (offsetY < 0)
    {
        offsetY = 0;
    }

    int srcStride = FPDFBitmap_GetStride(srcBitmap);
    int dstStride = canvasWidth * 4;

    for (int y = 0; y < copyHeight; y++)
    {
        const BYTE* srcRow =
            srcBuffer + (y * srcStride);

        BYTE* dstRow =
            reinterpret_cast<BYTE*>(bits) +
            ((y + offsetY) * dstStride) +
            (offsetX * 4);

        memcpy(dstRow, srcRow, copyWidth * 4);
    }

    return hBitmap;
}

///////////////////////////////////////////////////////////////////////////////
// ThumbnailGenerator
///////////////////////////////////////////////////////////////////////////////

ThumbnailGenerator::~ThumbnailGenerator()
{
    Shutdown();
}

void ThumbnailGenerator::Initialize()
{
    if (!m_initialized)
    {
        FPDF_InitLibrary();
        m_initialized = true;
    }
}

void ThumbnailGenerator::Shutdown()
{
    if (m_initialized)
    {
        FPDF_DestroyLibrary();
        m_initialized = false;
    }
}

bool ThumbnailGenerator::GenerateThumbnail(
    const std::wstring& pdfPath,
    int pageIndex,
    int maxWidth,
    int maxHeight,
    HBITMAP& outBitmap,
    std::string& error)
{
    outBitmap = nullptr;

    if (!m_initialized)
    {
        error = "PDFium is not initialized.";
        return false;
    }

    if (maxWidth <= 0)
    {
        maxWidth = 120;
    }

    if (maxHeight <= 0)
    {
        maxHeight = 160;
    }

    std::vector<unsigned char> fileData;

    if (!ReadFileToVector(pdfPath, fileData, error))
    {
        return false;
    }

    size_t fileSize = fileData.size();

    FPDF_DOCUMENT doc = FPDF_LoadMemDocument(
        fileData.data(),
        fileSize,
        nullptr
    );

    if (!doc)
    {
        error = "PDFium cannot open document.";
        return false;
    }

    int pageCount = FPDF_GetPageCount(doc);

    if (pageIndex < 0 || pageIndex >= pageCount)
    {
        FPDF_CloseDocument(doc);
        error = "Invalid page index.";
        return false;
    }

    FPDF_PAGE page = FPDF_LoadPage(doc, pageIndex);

    if (!page)
    {
        FPDF_CloseDocument(doc);
        error = "PDFium cannot load page.";
        return false;
    }

    double pageWidthDouble = FPDF_GetPageWidth(page);
    double pageHeightDouble = FPDF_GetPageHeight(page);

    float pageWidth = static_cast<float>(pageWidthDouble);
    float pageHeight = static_cast<float>(pageHeightDouble);

    if (pageWidth <= 0.0f || pageHeight <= 0.0f)
    {
        pageWidth = 612.0f;
        pageHeight = 792.0f;
    }

    float zoomWidth = static_cast<float>(maxWidth) / pageWidth;
    float zoomHeight = static_cast<float>(maxHeight) / pageHeight;

    float zoom = zoomWidth < zoomHeight ? zoomWidth : zoomHeight;

    if (zoom <= 0.0f)
    {
        zoom = 1.0f;
    }

    int renderWidth = static_cast<int>((pageWidth * zoom) + 0.5f);
    int renderHeight = static_cast<int>((pageHeight * zoom) + 0.5f);

    if (renderWidth < 1)
    {
        renderWidth = 1;
    }

    if (renderHeight < 1)
    {
        renderHeight = 1;
    }

    if (renderWidth > maxWidth)
    {
        renderWidth = maxWidth;
    }

    if (renderHeight > maxHeight)
    {
        renderHeight = maxHeight;
    }

    FPDF_BITMAP fpdfBitmap = FPDFBitmap_Create(
        renderWidth,
        renderHeight,
        1
    );

    if (!fpdfBitmap)
    {
        FPDF_ClosePage(page);
        FPDF_CloseDocument(doc);
        error = "PDFium cannot create bitmap.";
        return false;
    }

    FPDFBitmap_FillRect(
        fpdfBitmap,
        0,
        0,
        renderWidth,
        renderHeight,
        0xFFFFFFFF
    );

    FPDF_RenderPageBitmap(
        fpdfBitmap,
        page,
        0,
        0,
        renderWidth,
        renderHeight,
        0,
        FPDF_ANNOT
    );

    HBITMAP result = CreateCenteredHBitmapFromFPDFBitmap(
        fpdfBitmap,
        maxWidth,
        maxHeight
    );

    FPDFBitmap_Destroy(fpdfBitmap);
    FPDF_ClosePage(page);
    FPDF_CloseDocument(doc);

    if (!result)
    {
        error = "Failed to create thumbnail HBITMAP.";
        return false;
    }

    outBitmap = result;
    return true;
}

HBITMAP ThumbnailGenerator::CreatePlaceholderBitmap(int width, int height)
{
    if (width <= 0 || height <= 0)
    {
        return nullptr;
    }

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height; // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;

    HBITMAP hBitmap = CreateDIBSection(
        nullptr,
        &bmi,
        DIB_RGB_COLORS,
        &bits,
        nullptr,
        0
    );

    if (!hBitmap)
    {
        return nullptr;
    }

    if (!bits)
    {
        DeleteObject(hBitmap);
        return nullptr;
    }

    DWORD* pixels = reinterpret_cast<DWORD*>(bits);

    // Light gray placeholder.
    DWORD color = 0xFFF0F0F0;

    for (int i = 0; i < width * height; i++)
    {
        pixels[i] = color;
    }

    return hBitmap;
}