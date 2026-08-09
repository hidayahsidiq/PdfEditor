#include "PdfTextExtractor.h"

#include <math.h>

struct PdfCharInfo
{
    wchar_t ch = 0;

    double left = 0.0;
    double right = 0.0;
    double bottom = 0.0;
    double top = 0.0;

    double fontSize = 12.0;
};

static bool IsSpaceChar(wchar_t ch)
{
    return ch == L' ' ||
        ch == L'\t' ||
        ch == L'\n' ||
        ch == L'\r' ||
        ch == 160;
}

static void FlushWord(
    std::vector<PdfCharInfo>& chars,
    int start,
    int end,
    std::vector<PdfWord>& words)
{
    if (start >= end)
    {
        return;
    }

    PdfWord word;

    word.firstCharIndex = start;
    word.charCount = end - start;

    word.left = chars[start].left;
    word.right = chars[end - 1].right;
    word.bottom = chars[start].bottom;
    word.top = chars[start].top;

    double fontSizeSum = 0.0;

    for (int i = start; i < end; i++)
    {
        word.text.push_back(chars[i].ch);

        if (chars[i].left < word.left)
        {
            word.left = chars[i].left;
        }

        if (chars[i].right > word.right)
        {
            word.right = chars[i].right;
        }

        if (chars[i].bottom < word.bottom)
        {
            word.bottom = chars[i].bottom;
        }

        if (chars[i].top > word.top)
        {
            word.top = chars[i].top;
        }

        fontSizeSum += chars[i].fontSize;
    }

    if (word.charCount > 0)
    {
        word.fontSize = fontSizeSum / word.charCount;
    }

    if (!word.text.empty())
    {
        words.push_back(word);
    }
}

bool PdfTextExtractor::ExtractWords(
    FPDF_PAGE page,
    std::vector<PdfWord>& words,
    std::string& error)
{
    words.clear();

    FPDF_TEXTPAGE textPage = FPDFText_LoadPage(page);

    if (!textPage)
    {
        error = "PDFium cannot load text page.";
        return false;
    }

    int charCount = FPDFText_CountChars(textPage);

    if (charCount <= 0)
    {
        FPDFText_ClosePage(textPage);
        error = "No selectable text found on this page.";
        return false;
    }

    std::vector<PdfCharInfo> chars;
    chars.reserve(static_cast<size_t>(charCount));

    for (int i = 0; i < charCount; i++)
    {
        PdfCharInfo info;

        unsigned short buffer[2] = { 0, 0 };

        FPDFText_GetText(textPage, i, 1, buffer);

        info.ch = static_cast<wchar_t>(buffer[0]);

        FPDFText_GetCharBox(
            textPage,
            i,
            &info.left,
            &info.right,
            &info.bottom,
            &info.top
        );

        info.fontSize = static_cast<double>(
            FPDFText_GetFontSize(textPage, i)
            );

        chars.push_back(info);
    }

    FPDFText_ClosePage(textPage);

    int wordStart = -1;

    for (int i = 0; i < static_cast<int>(chars.size()); i++)
    {
        wchar_t ch = chars[i].ch;

        bool space = IsSpaceChar(ch);

        bool newLine = false;

        if (i > 0)
        {
            double prevCenterY =
                (chars[i - 1].top + chars[i - 1].bottom) / 2.0;

            double currentCenterY =
                (chars[i].top + chars[i].bottom) / 2.0;

            double height = fabs(chars[i].top - chars[i].bottom);

            if (height <= 0.0)
            {
                height = 1.0;
            }

            if (fabs(currentCenterY - prevCenterY) > height * 0.5)
            {
                newLine = true;
            }
        }

        bool largeGap = false;

        if (i > 0 && !space)
        {
            double gap = chars[i].left - chars[i - 1].right;

            double charWidth =
                chars[i - 1].right - chars[i - 1].left;

            if (charWidth <= 0.0)
            {
                charWidth = 1.0;
            }

            if (gap > charWidth * 0.35)
            {
                largeGap = true;
            }
        }

        if (space || newLine || largeGap)
        {
            if (wordStart >= 0)
            {
                FlushWord(chars, wordStart, i, words);
                wordStart = -1;
            }
        }

        if (!space)
        {
            if (wordStart < 0)
            {
                wordStart = i;
            }
        }
    }

    if (wordStart >= 0)
    {
        FlushWord(
            chars,
            wordStart,
            static_cast<int>(chars.size()),
            words
        );
    }

    return true;
}