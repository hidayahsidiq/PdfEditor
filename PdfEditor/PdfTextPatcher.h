#pragma once

#include <string>
#include <vector>

#include "EditTextModel.h"

class PdfTextPatcher
{
public:
    static bool ApplyPatches(
        const std::wstring& inputPath,
        const std::wstring& outputPath,
        const std::vector<EditTextPatch>& patches,
        std::string& error
    );
};