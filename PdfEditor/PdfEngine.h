#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>

#include "PageModel.h"

class QPDF;

class PdfEngine
{
public:
    PdfEngine() = default;
    ~PdfEngine();

    void ClearCache();

    int GetPageCount(const std::wstring& path);

    bool BuildPdf(
        const std::vector<PageRef>& pagesToWrite,
        const std::wstring& outputPath,
        std::string& error
    );

private:
    std::shared_ptr<QPDF> GetDocument(const std::wstring& path);

    std::map<std::wstring, std::shared_ptr<QPDF>> m_cache;
};