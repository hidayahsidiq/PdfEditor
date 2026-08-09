#include "PdfEngine.h"
#include "StringUtils.h"

#include <qpdf/QPDF.hh>
#include <qpdf/QPDFWriter.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFPageObjectHelper.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>

#include <stdio.h>
#include <stdexcept>

PdfEngine::~PdfEngine() = default;

void PdfEngine::ClearCache()
{
    m_cache.clear();
}

std::shared_ptr<QPDF> PdfEngine::GetDocument(const std::wstring& path)
{
    auto it = m_cache.find(path);

    if (it != m_cache.end())
    {
        return it->second;
    }

    auto pdf = std::make_shared<QPDF>();

    std::wstring shortPath = GetShortPath(path);
    std::string ansiPath = WideToAnsi(shortPath);

    pdf->processFile(ansiPath.c_str());

    m_cache[path] = pdf;

    return pdf;
}

int PdfEngine::GetPageCount(const std::wstring& path)
{
    std::shared_ptr<QPDF> doc = GetDocument(path);

    auto pages = QPDFPageDocumentHelper(*doc).getAllPages();

    return static_cast<int>(pages.size());
}

bool PdfEngine::BuildPdf(
    const std::vector<PageRef>& pagesToWrite,
    const std::wstring& outputPath,
    std::string& error)
{
    FILE* outputFile = nullptr;

    errno_t openError = _wfopen_s(
        &outputFile,
        outputPath.c_str(),
        L"wb"
    );

    if (openError != 0 || outputFile == nullptr)
    {
        error = "Cannot create output file.";
        return false;
    }

    try
    {
        QPDF outputPdf;
        outputPdf.emptyPDF();

        for (const PageRef& ref : pagesToWrite)
        {
            std::shared_ptr<QPDF> source = GetDocument(ref.sourcePath);

            auto sourcePages = QPDFPageDocumentHelper(*source).getAllPages();

            if (ref.pageIndex < 0 || ref.pageIndex >= static_cast<int>(sourcePages.size()))
            {
                fclose(outputFile);
                error = "Invalid page index.";
                return false;
            }

            QPDFPageObjectHelper sourcePage = sourcePages[ref.pageIndex];

            outputPdf.addPage(sourcePage, false);

            if (ref.rotateDelta != 0)
            {
                auto outputPages = QPDFPageDocumentHelper(outputPdf).getAllPages();

                if (outputPages.empty())
                {
                    fclose(outputFile);
                    error = "Failed to add page to output PDF.";
                    return false;
                }

                QPDFPageObjectHelper outputPage = outputPages.back();

                int oldRotation = 0;

                QPDFObjectHandle sourceObject = sourcePage.getObjectHandle();
                QPDFObjectHandle rotateObject = sourceObject.getKey("/Rotate");

                if (rotateObject.isNumber())
                {
                    oldRotation = static_cast<int>(rotateObject.getIntValue());
                }

                int newRotation = NormalizeRotation(oldRotation + ref.rotateDelta);

                outputPage.getObjectHandle().replaceKey(
                    "/Rotate",
                    QPDFObjectHandle::newInteger(newRotation)
                );
            }
        }

        std::string ansiOutputPath = WideToAnsi(outputPath);

        QPDFWriter writer(outputPdf);

        writer.setOutputFile(
            ansiOutputPath.c_str(),
            outputFile,
            false
        );

        writer.write();

        fclose(outputFile);

        return true;
    }
    catch (const std::exception& e)
    {
        fclose(outputFile);
        error = e.what();
        return false;
    }
}