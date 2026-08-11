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
// Helper: detect color methods
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
// Helper: detect GraphicsState member/function
///////////////////////////////////////////////////////////////////////////////

template <typename T, typename = void>
struct HasGraphicsStateMember : std::false_type {};

template <typename T>
struct HasGraphicsStateMember<
    T,
    std::void_t<decltype(std::declval<T>().GraphicsState)>
> : std::true_type {
};

template <typename T, typename = void>
struct HasGraphicsStateFunction : std::false_type {};

template <typename T>
struct HasGraphicsStateFunction<
    T,
    std::void_t<decltype(std::declval<T>().GraphicsState())>
> : std::true_type {
};

///////////////////////////////////////////////////////////////////////////////
// Helper: detect SetCanvas argument type
///////////////////////////////////////////////////////////////////////////////

template <typename T, typename = void>
struct HasSetCanvasPageRef : std::false_type {};

template <typename T>
struct HasSetCanvasPageRef<
    T,
    std::void_t<decltype(std::declval<T>().SetCanvas(std::declval<PdfPage&>()))>
> : std::true_type {
};

template <typename T, typename = void>
struct HasSetCanvasPagePtr : std::false_type {};

template <typename T>
struct HasSetCanvasPagePtr<
    T,
    std::void_t<decltype(std::declval<T>().SetCanvas(std::declval<PdfPage*>()))>
> : std::true_type {
};

///////////////////////////////////////////////////////////////////////////////
// Color setter
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

template <typename TPainter>
bool SetPainterFillColor(TPainter& painter, const PdfColor& color)
{
    if (TrySetColorOnTarget(painter, color))
    {
        return true;
    }

    if constexpr (HasGraphicsStateMember<TPainter>::value)
    {
        if (TrySetColorOnTarget(painter.GraphicsState, color))
        {
            return true;
        }
    }

    if constexpr (HasGraphicsStateFunction<TPainter>::value)
    {
        if (TrySetColorOnTarget(painter.GraphicsState(), color))
        {
            return true;
        }
    }

    return false;
}

///////////////////////////////////////////////////////////////////////////////
// Canvas setter
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
// Page/font pointer helpers
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

        decltype(auto) fontResult = fonts.SearchFont("Helvetica");

        const PdfFont* helvetica = ToFontPtr(fontResult);

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

                    // Cover old text with white rectangle.
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

                    // Draw replacement text.
                    if (!patch.newText.empty() && helvetica != nullptr)
                    {
                        double fontSize = patch.fontSize;

                        if (fontSize < 4.0)
                        {
                            fontSize = 12.0;
                        }

                        painter.TextState.SetFont(*helvetica, fontSize);

                        SetPainterFillColor(
                            painter,
                            PdfColor(0.0, 0.0, 0.0)
                        );

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