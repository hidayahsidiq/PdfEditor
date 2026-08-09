#pragma once

#include <string>

struct PdfWord
{
    std::wstring text;

    double left = 0.0;
    double bottom = 0.0;
    double right = 0.0;
    double top = 0.0;

    double fontSize = 12.0;

    int firstCharIndex = 0;
    int charCount = 0;
};

struct EditTextPatch
{
    int pageIndex = 0;

    double left = 0.0;
    double bottom = 0.0;
    double right = 0.0;
    double top = 0.0;

    double fontSize = 12.0;

    std::wstring oldText;
    std::wstring newText;
};