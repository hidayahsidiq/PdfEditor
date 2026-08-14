#include "PdfTextPatcher.h"
#include "StringUtils.h"

#include <podofo/podofo.h>

#include <algorithm>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <memory>

#pragma comment(lib, "podofo.lib")

using namespace PoDoFo;

///////////////////////////////////////////////////////////////////////////////
// Detect possible PoDoFo color APIs
///////////////////////////////////////////////////////////////////////////////

template <typename T, typename = void>
struct HasSetFillColor : std::false_type {};

template <typename T>
struct HasSetFillColor<
    T,
    std::void_t<decltype(std::declval<T>().SetFillColor(std::declval<const PdfColor&>()))>
> : std::true_type {
};

template <typename T, typename = void>
struct HasSetPaintColor : std::false_type {};

template <typename T>
struct HasSetPaintColor<
    T,
    std::void_t<decltype(std::declval<T>().SetPaintColor(std::declval<const PdfColor&>()))>
> : std::true_type {
};

template <typename T, typename = void>
struct HasSetColor : std::false_type {};

template <typename T>
struct HasSetColor<
    T,
    std::void_t<decltype(std::declval<T>().SetColor(std::declval<const PdfColor&>()))>
> : std::true_type {
};

template <typename T, typename = void>
struct HasSetFillAndStrokeColor : std::false_type {};

template <typename T>
struct HasSetFillAndStrokeColor<
    T,
    std::void_t<decltype(std::declval<T>().SetFillAndStrokeColor(std::declval<const PdfColor&>()))>
> : std::true_type {
};

///////////////////////////////////////////////////////////////////////////////
// Try to set color on painter
///////////////////////////////////////////////////////////////////////////////

template <typename T>
bool TrySetColorOnTarget(T&& target, const PdfColor& color)
{
    using Target = std::remove_reference_t<T>;

    if constexpr (HasSetFillColor<Target>::value)
    {
        target.SetFillColor(color);
        return true;
    }
    else if constexpr (HasSetPaintColor<Target>::value)
    {
        target.SetPaintColor(color);
        return true;
    }
    else if constexpr (HasSetColor<Target>::value)
    {
        target.SetColor(color);
        return true;
    }
    else if constexpr (HasSetFillAndStrokeColor<Target>::value)
    {
        target.SetFillAndStrokeColor(color);
        return true;
    }
    else
    {
        return false;
    }
}

static bool SetPainterFillColor(PdfPainter& painter, const PdfColor& color)
{
    return TrySetColorOnTarget(painter, color);
}

///////////////////////////////////////////////////////////////////////////////
// Canvas helper
///////////////////////////////////////////////////////////////////////////////

static void SetPainterCanvas(PdfPainter& painter, PdfPage* page)
{
    if (!page)
    {
        return;
    }

    painter.SetCanvas(*page, PdfPainterFlags{});
}

///////////////////////////////////////////////////////////////////////////////
// Page pointer helpers
///////////////////////////////////////////////////////////////////////////////

static PdfPage* ToPagePtr(PdfPage* page)
{
    return page;
}

static PdfPage* ToPagePtr(PdfPage& page)
{
    return &page;
}

static PdfPage* ToPagePtr(std::unique_ptr<PdfPage>& page)
{
    return page.get();
}

static PdfPage* ToPagePtr(const std::unique_ptr<PdfPage>& page)
{
    return page.get();
}

static PdfPage* ToPagePtr(std::shared_ptr<PdfPage>& page)
{
    return page.get();
}

static PdfPage* ToPagePtr(const std::shared_ptr<PdfPage>& page)
{
    return page.get();
}

///////////////////////////////////////////////////////////////////////////////
// Font pointer helpers
//
// IMPORTANT:
// These helpers only convert to pointer.
// They do NOT copy PdfFont.
///////////////////////////////////////////////////////////////////////////////

static const PdfFont* ToFontPtr(const PdfFont* font)
{
    return font;
}

static const PdfFont* ToFontPtr(PdfFont* font)
{
    return font;
}

static const PdfFont* ToFontPtr(const PdfFont& font)
{
    return &font;
}

static const PdfFont* ToFontPtr(PdfFont& font)
{
    return &font;
}

static const PdfFont* ToFontPtr(std::unique_ptr<PdfFont>& font)
{
    return font.get();
}

static const PdfFont* ToFontPtr(const std::unique_ptr<PdfFont>& font)
{
    return font.get();
}

static const PdfFont* ToFontPtr(std::shared_ptr<PdfFont>& font)
{
    return font.get();
}

static const PdfFont* ToFontPtr(const std::shared_ptr<PdfFont>& font)
{
    return font.get();
}

///////////////////////////////////////////////////////////////////////////////
// Rectangle helper
///////////////////////////////////////////////////////////////////////////////

static void NormalizePatchRect(
    const EditTextPatch& patch,
    double& left,
    double& bottom,
    double& right,
    double& top)
{
    left = patch.left;
    right = patch.right;
    bottom = patch.bottom;
    top = patch.top;

    if (left > right)
    {
        std::swap(left, right);
    }

    if (bottom > top)
    {
        std::swap(bottom, top);
    }
}

///////////////////////////////////////////////////////////////////////////////
// PdfTextPatcher
///////////////////////////////////////////////////////////////////////////////

bool PdfTextPatcher::ApplyPatches(
    const std::wstring& inputPath,
    const std::wstring& outputPath,
    const std::vector<EditTextPatch>& patches,
    std::string& error)
{
    if (patches.empty())
    {
        return true;
    }

    try
    {
        std::wstring shortInputPath = GetShortPath(inputPath);

        std::string input = WideToAnsi(shortInputPath);
        std::string output = WideToAnsi(outputPath);

        PdfMemDocument document;

        document.Load(input.c_str());

        auto&& pages = document.GetPages();
        auto&& fonts = document.GetFonts();

        const PdfFont* helvetica = nullptr;

        // Preferred method for PoDoFo 0.10+:
        // Get a standard PDF Base-14 font.
        try
        {
            helvetica = ToFontPtr(
                fonts.GetStandard14Font(PdfStandard14FontType::Helvetica)
            );
        }
        catch (...)
        {
            helvetica = nullptr;
        }

        // Fallback: search Helvetica if it already exists in the document.
        if (!helvetica)
        {
            try
            {
                helvetica = ToFontPtr(fonts.SearchFont("Helvetica"));
            }
            catch (...)
            {
                helvetica = nullptr;
            }
        }

        // Fallback: search Arial.
        if (!helvetica)
        {
            try
            {
                helvetica = ToFontPtr(fonts.SearchFont("Arial"));
            }
            catch (...)
            {
                helvetica = nullptr;
            }
        }

        if (!helvetica)
        {
            error =
                "Cannot get Helvetica/Arial font from PoDoFo. "
                "Font API is still not returning a valid font.";
            return false;
        }

        int pageIndex = 0;

        for (auto&& pageItem : pages)
        {
            PdfPage* page = ToPagePtr(pageItem);

            if (!page)
            {
                pageIndex++;
                continue;
            }

            bool hasPatchOnThisPage = false;

            for (const EditTextPatch& patch : patches)
            {
                if (patch.pageIndex == pageIndex)
                {
                    hasPatchOnThisPage = true;
                    break;
                }
            }

            if (hasPatchOnThisPage)
            {
                PdfPainter painter;

                SetPainterCanvas(painter, page);

                bool colorSupported = SetPainterFillColor(
                    painter,
                    PdfColor(0.0, 0.0, 0.0)
                );

                for (const EditTextPatch& patch : patches)
                {
                    if (patch.pageIndex != pageIndex)
                    {
                        continue;
                    }

                    double left = 0.0;
                    double bottom = 0.0;
                    double right = 0.0;
                    double top = 0.0;

                    NormalizePatchRect(
                        patch,
                        left,
                        bottom,
                        right,
                        top
                    );

                    double width = right - left;
                    double height = top - bottom;

                    if (width <= 0.0 || height <= 0.0)
                    {
                        continue;
                    }

                    bool isReplacingExistingText = !patch.oldText.empty();

                    // Cover old text only when replacing existing text.
                    // For newly added text, skip the rectangle to avoid
                    // invisible text if color API is not detected.
                    if (isReplacingExistingText && colorSupported)
                    {
                        SetPainterFillColor(
                            painter,
                            PdfColor(1.0, 1.0, 1.0)
                        );

                        painter.DrawRectangle(
                            left,
                            bottom,
                            width,
                            height
                        );
                    }

                    if (!patch.newText.empty())
                    {
                        double fontSize = patch.fontSize;

                        if (fontSize < 4.0)
                        {
                            fontSize = 12.0;
                        }

                        painter.TextState.SetFont(*helvetica, fontSize);

                        if (colorSupported)
                        {
                            SetPainterFillColor(
                                painter,
                                PdfColor(0.0, 0.0, 0.0)
                            );
                        }

                        double baselineY = bottom + (height * 0.20);

                        std::string text = WideToAnsi(patch.newText);

                        painter.DrawText(
                            std::string_view(text),
                            left,
                            baselineY,
                            PdfDrawTextStyle{}
                        );
                    }
                }

                painter.FinishDrawing();
            }

            pageIndex++;
        }

        document.Save(output.c_str());

        return true;
    }
    catch (const std::exception& e)
    {
        error = e.what();
        return false;
    }
}