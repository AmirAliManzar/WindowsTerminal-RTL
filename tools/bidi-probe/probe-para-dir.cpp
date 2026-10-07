// Does the paragraph direction alone fix the word order?
//
// Takes "PS> <RTL words>" and runs the AtlasEngine pipeline (shape per run at
// its resolved level, then Unicode L2 over the clusters) twice: once with the
// paragraph direction from the first strong character (UBA P2/P3, which makes
// this line LTR) and once with "RTL if the row contains any RTL letter"
// (RtlTerminal's rule). Prints the resolved levels and the resulting visual
// order each time, so the difference is visible rather than argued about.
//
// Build (under a vcvars64 window):
//   cl /nologo /EHsc /std:c++20 probe-para-dir.cpp /link dwrite.lib

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

static bool g_anyRtl;

struct Source final : IDWriteTextAnalysisSource
{
    std::wstring text;
    UINT32 length;
    explicit Source(const std::wstring& t) : text(t), length((UINT32)t.size()) {}
    ULONG __stdcall AddRef() noexcept override { return 1; }
    ULONG __stdcall Release() noexcept override { return 1; }
    HRESULT __stdcall QueryInterface(const IID& riid, void** ppv) noexcept override
    {
        if (IsEqualGUID(riid, __uuidof(IDWriteTextAnalysisSource))) { *ppv = this; return S_OK; }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    HRESULT __stdcall GetTextAtPosition(UINT32 pos, const WCHAR** str, UINT32* len) noexcept override
    {
        pos = pos < length ? pos : length;
        *str = text.c_str() + pos;
        *len = length - pos;
        return S_OK;
    }
    HRESULT __stdcall GetTextBeforePosition(UINT32 pos, const WCHAR** str, UINT32* len) noexcept override
    {
        pos = pos < length ? pos : length;
        *str = text.c_str();
        *len = pos;
        return S_OK;
    }
    DWRITE_READING_DIRECTION __stdcall GetParagraphReadingDirection() noexcept override
    {
        if (g_anyRtl)
        {
            // RtlTerminal: any strong RTL letter in the row makes the paragraph RTL.
            for (UINT32 i = 0; i < length; ++i)
            {
                const WCHAR c = text[i];
                if ((c >= 0x0590 && c <= 0x08ff) || (c >= 0xfb1d && c <= 0xfdff) || (c >= 0xfe70 && c <= 0xfeff))
                {
                    return DWRITE_READING_DIRECTION_RIGHT_TO_LEFT;
                }
            }
            return DWRITE_READING_DIRECTION_LEFT_TO_RIGHT;
        }
        // UBA P2/P3: first strong character decides.
        for (UINT32 i = 0; i < length; ++i)
        {
            const WCHAR c = text[i];
            const bool rtl = (c >= 0x0590 && c <= 0x08ff) || (c >= 0xfb1d && c <= 0xfdff) || (c >= 0xfe70 && c <= 0xfeff);
            const bool ltr = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
            if (rtl) return DWRITE_READING_DIRECTION_RIGHT_TO_LEFT;
            if (ltr) return DWRITE_READING_DIRECTION_LEFT_TO_RIGHT;
        }
        return DWRITE_READING_DIRECTION_LEFT_TO_RIGHT;
    }
    HRESULT __stdcall GetLocaleName(UINT32, UINT32* len, const WCHAR** name) noexcept override
    {
        *len = 5;
        static const WCHAR en[] = L"en-US";
        *name = en;
        return S_OK;
    }
    HRESULT __stdcall GetNumberSubstitution(UINT32, UINT32*, IDWriteNumberSubstitution**) noexcept override
    {
        return E_NOTIMPL;
    }
};

struct Sink final : IDWriteTextAnalysisSink
{
    std::vector<UINT32> pos;
    std::vector<UINT8> lvl;
    ULONG __stdcall AddRef() noexcept override { return 1; }
    ULONG __stdcall Release() noexcept override { return 1; }
    HRESULT __stdcall QueryInterface(const IID& riid, void** ppv) noexcept override
    {
        if (IsEqualGUID(riid, __uuidof(IDWriteTextAnalysisSink))) { *ppv = this; return S_OK; }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    HRESULT __stdcall SetScriptAnalysis(UINT32, UINT32, const DWRITE_SCRIPT_ANALYSIS*) noexcept override { return S_OK; }
    HRESULT __stdcall SetLineBreakpoints(UINT32, UINT32, const DWRITE_LINE_BREAKPOINT*) noexcept override { return S_OK; }
    HRESULT __stdcall SetBidiLevel(UINT32 p, UINT32, UINT8, UINT8 resolved) noexcept override
    {
        pos.push_back(p);
        lvl.push_back(resolved);
        return S_OK;
    }
    HRESULT __stdcall SetNumberSubstitution(UINT32, UINT32, IDWriteNumberSubstitution*) noexcept override { return S_OK; }
};

static void printCp(char32_t cp)
{
    if (cp == ' ') { printf("SP"); return; }
    if (cp < 0x80) { printf("%c", (char)cp); return; }
    printf("%04X", (unsigned)cp);
}

int main()
{
    ComPtr<IDWriteFactory> factory;
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown**)&factory);
    ComPtr<IDWriteTextAnalyzer> analyzer;
    factory->CreateTextAnalyzer(&analyzer);

    const std::wstring text = L"PS> \u0633\u0644\u0627\u0645 \u062E\u0648\u0628\u06CC"; // PS> سلام خوبی
    const UINT32 n = (UINT32)text.size();

    printf("logical: ");
    for (UINT32 t = 0; t < n; ++t) printCp((char32_t)(UINT16)text[t]);
    printf("\n\n");

    for (int mode = 0; mode < 2; ++mode)
    {
        g_anyRtl = (mode == 1);
        printf("================ %s ================\n",
            mode == 0 ? "P2/P3 first-strong (current)" : "any-RTL letter (RtlTerminal)");

        Source src{ text };
        Sink sink;
        analyzer->AnalyzeBidi(&src, 0, n, &sink);

        std::vector<UINT8> charLevel(n, 0);
        for (size_t r = 0; r < sink.pos.size(); ++r)
        {
            const UINT32 start = sink.pos[r];
            const UINT32 end = (r + 1 < sink.pos.size()) ? sink.pos[r + 1] : n;
            const UINT8 level = sink.lvl[r];
            printf("  run [%2u..%2u) level=%u %s : ", start, end, level, (level & 1) ? "RTL" : "LTR");
            for (UINT32 t = start; t < end; ++t) printCp((char32_t)(UINT16)text[t]);
            printf("\n");
            for (UINT32 t = start; t < end; ++t) charLevel[t] = level;
        }

        // Unicode L2 over the cluster sequence, exactly as AtlasEngine does it:
        // from the highest level down to the lowest odd one, reversing every
        // contiguous run at or above each level (even levels included).
        std::vector<UINT32> order;
        for (UINT32 t = 0; t < n; ++t) order.push_back(t);

        UINT8 maxLevel = 0;
        for (UINT8 l : charLevel) { if (l > maxLevel) maxLevel = l; }
        UINT8 minOdd = 0xff;
        for (UINT8 l : charLevel) { if ((l & 1) && l < minOdd) minOdd = l; }
        if (minOdd == 0xff) minOdd = 1;
        for (UINT32 level = maxLevel; level >= minOdd; --level)
        {
            size_t i = 0;
            while (i < order.size())
            {
                if (charLevel[order[i]] >= level)
                {
                    size_t j = i;
                    while (j < order.size() && charLevel[order[j]] >= level) ++j;
                    std::reverse(order.begin() + i, order.begin() + j);
                    i = j;
                }
                else ++i;
            }
        }

        printf("  screen left to right : ");
        for (UINT32 idx : order) printCp((char32_t)(UINT16)text[idx]);
        printf("\n");

        // Read the line back the way a person would: each run in its own
        // direction, left-to-right runs left to right and right-to-left runs
        // right to left. Reading a whole mixed line in a single direction
        // scrambles whichever part runs the other way.
        std::wstring back;
        for (size_t i = 0; i < order.size();)
        {
            const UINT8 lvl = charLevel[order[i]];
            size_t j = i;
            while (j < order.size() && charLevel[order[j]] == lvl) ++j;
            if (lvl & 1)
            {
                for (size_t k = j; k-- > i;) back.push_back((WCHAR)text[order[k]]);
            }
            else
            {
                for (size_t k = i; k < j; ++k) back.push_back((WCHAR)text[order[k]]);
            }
            i = j;
        }
        printf("  read as a person would: ");
        for (WCHAR w : back) printCp((char32_t)(UINT16)w);
        printf("\n");
        printf("  %s\n\n", (back == text) ? "OK: reproduces the input" : "MISMATCH: word order is wrong");
    }
    return 0;
}
