#pragma once

#include <string>
#include <vector>

#include <fpdfview.h>
#include <fpdf_text.h>

#include "EditTextModel.h"

class PdfTextExtractor
{
public:
    bool ExtractWords(
        FPDF_PAGE page,
        std::vector<PdfWord>& words,
        std::string& error
    );
};