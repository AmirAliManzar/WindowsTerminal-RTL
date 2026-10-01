// What does a Persian line actually look like after AtlasEngine's reordering?
//
// The renderer reverses cluster sequences (Unicode bidi rule L2) and, since the
// intra-cluster fix, also the glyphs inside an odd-level cluster. This probe runs
// the same pipeline over real strings and prints:
//
//   - the resolved bidi level of every run,
//   - where DirectWrite put the cluster boundaries,
//   - how many glyphs each cluster got,
//   - the cell-by-cell result after reordering,
//   - and the logical text you get back by reading those cells right to left.
//
// The last line is the one that matters: if reading the cells right to left does
// not give the original string back, the reordering is wrong, whatever the glyph
// order looked like on paper.
//
// Build:
//   cl /nologo /EHsc /std:c++20 probe-rtl-layout.cpp /link dwrite.lib
// Run:
//   probe-rtl-layout.exe

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

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

// One cell: the character it came from, and the glyph drawn in it.
struct Cell
{
    char32_t ch;
    UINT16 glyph;
    float advance;
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

    ComPtr<IDWriteFontCollection> collection;
    factory->GetSystemFontCollection(&collection);

    ComPtr<IDWriteTextAnalyzer> analyzer;
    factory->CreateTextAnalyzer(&analyzer);

    ComPtr<IDWriteFontFace> face;
    for (UINT32 i = 0; i < collection->GetFontFamilyCount(); ++i)
    {
        ComPtr<IDWriteFontFamily> family;
        if (FAILED(collection->GetFontFamily(i, &family))) continue;
        ComPtr<IDWriteFont> font;
        if (FAILED(family->GetFirstMatchingFont(DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STRETCH_NORMAL, DWRITE_FONT_STYLE_NORMAL, &font))) continue;
        BOOL has = FALSE;
        if (FAILED(font->HasCharacter(0x0633, &has)) || !has) continue;
        if (FAILED(font->CreateFontFace(&face))) continue;
        break;
    }
    if (!face) { printf("no Arabic-capable font\n"); return 1; }

    struct Case { const char* label; const wchar_t* text; };
    const Case cases[] = {
        { "salaam",        L"\u0633\u0644\u0627\u0645" },                                   // سلام
        { "laa",           L"\u0644\u0627" },                                               // لا
        { "madar bozorg",  L"\u0645\u0627\u062F\u0631 \u0628\u0632\u0631\u06AF" },             // مادر بزرگ
        { "after prompt",  L"PS> \u0633\u0644\u0627\u0645 \u062E\u0648\u0628\u06CC" },          // PS> سلام خوبی
    };

    for (const auto& c : cases)
    {
        const std::wstring text{ c.text };
        const UINT32 n = (UINT32)text.size();

        printf("================ %s ================\n", c.label);
        printf("logical: ");
        for (UINT32 t = 0; t < n; ++t) printCp((char32_t)(UINT16)text[t]);
        printf("\n");

        Source src{ text };
        Sink sink;
        analyzer->AnalyzeBidi(&src, 0, n, &sink);

        // Shape every run at its own resolved level, like AtlasEngine does.
        std::vector<Cell> cells;          // logical order, one entry per text char
        std::vector<UINT8> charLevel(n, 0);

        for (size_t r = 0; r < sink.pos.size(); ++r)
        {
            const UINT32 start = sink.pos[r];
            const UINT32 end = (r + 1 < sink.pos.size()) ? sink.pos[r + 1] : n;
            const UINT8 level = sink.lvl[r];
            printf("  run [%u..%u) level=%u %s : ", start, end, level, (level & 1) ? "RTL" : "LTR");
            for (UINT32 t = start; t < end; ++t) printCp((char32_t)(UINT16)text[t]);
            printf("\n");

            const UINT32 runLen = end - start;
            std::vector<UINT16> glyphs(runLen * 2);
            std::vector<UINT16> cmap(runLen + 1);
            std::vector<DWRITE_SHAPING_TEXT_PROPERTIES> props(runLen);
            std::vector<DWRITE_SHAPING_GLYPH_PROPERTIES> gprops(runLen * 2);
            UINT32 glyphCount = 0;

            DWRITE_SCRIPT_ANALYSIS sa{};
            sa.script = 0x0600;
            sa.shapes = DWRITE_SCRIPT_SHAPES_DEFAULT;

            HRESULT hr = analyzer->GetGlyphs(
                text.c_str() + start, runLen, face.Get(), FALSE, (level & 1) ? TRUE : FALSE,
                &sa, L"ar", nullptr, nullptr, nullptr, 0,
                (UINT32)glyphs.size(), cmap.data(), props.data(), glyphs.data(), gprops.data(), &glyphCount);

            if (FAILED(hr)) { printf("  GetGlyphs failed 0x%08X\n", (unsigned)hr); continue; }

            printf("    glyphCount=%u clusterMap=", glyphCount);
            for (UINT32 i = 0; i <= runLen; ++i) printf("%u ", cmap[i]);
            printf("\n");

            // DirectWrite gives one entry per glyph; a terminal needs one per
            // character. Where a character produced several glyphs, or none,
            // record what actually happened rather than pretending.
            for (UINT32 g = 0; g < glyphCount; ++g)
            {
                UINT32 owner = runLen;
                for (UINT32 t = 0; t < runLen; ++t)
                {
                    if (cmap[t] <= g && g < cmap[t + 1]) { owner = t; break; }
                }
                                printf("    glyph[%u] glyph=%5u char=",
                       g, glyphs[g]);
                if (owner < runLen) printCp((char32_t)(UINT16)text[start + owner]);
                else printf("(unmapped)");
                printf("\n");

                // Keep the FIRST glyph of each character; that is what a
                // one-cell-per-character terminal can actually show.
                if (owner < runLen && cmap[owner] == g)
                {
                    cells.push_back({ (char32_t)(UINT16)text[start + owner], glyphs[g], 0.0f });
                }
            }

            // Pad if the run produced fewer glyphs than characters.
            while (cells.size() < end)
            {
                const UINT32 t = (UINT32)cells.size();
                cells.push_back({ (char32_t)(UINT16)text[t], 0, 0.0f });
            }
            for (UINT32 t = start; t < end; ++t) charLevel[t] = level;
        }

        printf("  clusters: %zu (one per character)\n", cells.size());

        // ---- what the renderer emits: reverse odd-level cluster runs (L2) ----
        std::vector<UINT32> order;
        for (UINT32 t = 0; t < (UINT32)cells.size(); ++t) order.push_back(t);

        UINT8 maxLevel = 0;
        for (UINT8 l : charLevel) { if (l > maxLevel) { maxLevel = l; } }
        for (UINT32 level = maxLevel; level >= 1; --level)
        {
            if ((level & 1) == 0) continue;
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

        printf("  screen left to right: ");
        for (UINT32 idx : order) printCp(cells[idx].ch);
        printf("\n");

        printf("  read right to left  : ");
        for (size_t i = order.size(); i-- > 0;) printCp(cells[order[i]].ch);
        printf("\n");

        std::wstring back;
        for (size_t i = order.size(); i-- > 0;) back.push_back((WCHAR)cells[order[i]].ch);
        printf("  %s reading the cells right to left reproduces the input\n",
               (back == text) ? "OK  " : "MISMATCH");
        if (back != text)
        {
            printf("    got : ");
            for (WCHAR w : back) printCp((char32_t)(UINT16)w);
            printf("\n    want: ");
            for (WCHAR w : text) printCp((char32_t)(UINT16)w);
            printf("\n");
        }
        printf("\n");
    }
    return 0;
}