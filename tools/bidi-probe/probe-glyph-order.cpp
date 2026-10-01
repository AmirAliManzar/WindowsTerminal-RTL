// Probes IDWriteTextAnalyzer::GetGlyphs to determine, empirically, whether the
// glyphs of an RTL run come back in logical order or visual order.
//
// This decides the value of `reverseWithinSameLevelRun` in
// AtlasEngine::_applyBidiVisualOrder().
//
// Build (no Windows Terminal dependencies needed):
//   cl /nologo /EHsc /std:c++20 probe-glyph-order.cpp /link dwrite.lib
//
// Run:  probe-glyph-order.exe

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <dwrite.h>
#include <wrl/client.h>
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
    std::vector<DWRITE_SCRIPT_ANALYSIS> scripts;
    std::vector<std::pair<UINT32, UINT8>> levels;

    ULONG __stdcall AddRef() noexcept override { return 1; }
    ULONG __stdcall Release() noexcept override { return 1; }
    HRESULT __stdcall QueryInterface(const IID& riid, void** ppv) noexcept override
    {
        if (IsEqualGUID(riid, __uuidof(IDWriteTextAnalysisSink))) { *ppv = this; return S_OK; }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    HRESULT __stdcall SetScriptAnalysis(UINT32 pos, UINT32 len, const DWRITE_SCRIPT_ANALYSIS* a) noexcept override
    {
        scripts.push_back(*a);
        (void)pos;
        (void)len;
        return S_OK;
    }
    HRESULT __stdcall SetLineBreakpoints(UINT32, UINT32, const DWRITE_LINE_BREAKPOINT*) noexcept override { return S_OK; }
    HRESULT __stdcall SetBidiLevel(UINT32 pos, UINT32, UINT8, UINT8 resolved) noexcept override
    {
        levels.push_back({ pos, resolved });
        return S_OK;
    }
    HRESULT __stdcall SetNumberSubstitution(UINT32, UINT32, IDWriteNumberSubstitution*) noexcept override { return S_OK; }
};

static const char* U16(char32_t cp)
{
    static thread_local char buf[16];
    if (cp < 0x80) { buf[0] = (char)cp; buf[1] = 0; }
    else { snprintf(buf, sizeof(buf), "U+%04X", (unsigned)cp); }
    return buf;
}

int main()
{
    ComPtr<IDWriteFactory> factory;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown**)&factory)))
    {
        printf("DWriteCreateFactory failed\n");
        return 1;
    }

    ComPtr<IDWriteFontCollection> collection;
    factory->GetSystemFontCollection(&collection);

    // "سلام" = U+0633 U+0644 U+0627 U+0645  (seen, lam, alef, meem)
    const std::wstring arabic = L"\u0633\u0644\u0627\u0645";
    const char32_t cps[] = { 0x0633, 0x0644, 0x0627, 0x0645 };
    const UINT32 n = (UINT32)arabic.size();

    ComPtr<IDWriteTextAnalyzer> analyzer;
    if (FAILED(factory->CreateTextAnalyzer(&analyzer)))
    {
        printf("CreateTextAnalyzer failed\n");
        return 1;
    }

    // Find a font that can render the word.
    ComPtr<IDWriteFontFace> face;
    ComPtr<IDWriteFontFamily> chosen;
    for (UINT32 i = 0; i < collection->GetFontFamilyCount(); ++i)
    {
        ComPtr<IDWriteFontFamily> family;
        if (FAILED(collection->GetFontFamily(i, &family))) continue;
        ComPtr<IDWriteFont> font;
        if (FAILED(family->GetFirstMatchingFont(DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STRETCH_NORMAL, DWRITE_FONT_STYLE_NORMAL, &font))) continue;
        BOOL exists = FALSE;
        if (FAILED(font->HasCharacter(0x0633, &exists)) || !exists) continue;
        if (FAILED(font->CreateFontFace(&face))) continue;
        chosen = family;
        break;
    }
    if (!face)
    {
        printf("no font with Arabic support found\n");
        return 1;
    }
    printf("using a system font that has U+0633\n\n");

    // Confirm the resolved bidi level of the run.
    {
        Source src{ arabic };
        Sink sink;
        analyzer->AnalyzeScript(&src, 0, n, &sink);
        printf("AnalyzeScript only -> %zu script run(s), %zu bidi level(s)  <-- SetBidiLevel NOT called\n",
               sink.scripts.size(), sink.levels.size());
    }
    {
        Source src{ arabic };
        Sink sink;
        analyzer->AnalyzeBidi(&src, 0, n, &sink);
        printf("AnalyzeBidi       -> %zu bidi level(s)\n", sink.levels.size());
        for (auto& [pos, lvl] : sink.levels)
        {
            printf("  text[%u..%u) level = %u (%s)\n", pos, pos + n, lvl, (lvl & 1) ? "RTL" : "LTR");
        }
    }

    // A mixed line, which is what actually exercises L2.
    {
        const std::wstring mixed = L"hello \u0633\u0644\u0627\u0645";  // "hello سلام"
        Source src{ mixed };
        Sink sink;
        analyzer->AnalyzeBidi(&src, 0, (UINT32)mixed.size(), &sink);
        printf("\nMixed line \"hello سلام\" -> %zu bidi level(s)\n", sink.levels.size());
        for (auto& [pos, lvl] : sink.levels)
        {
            const UINT32 len = (pos < sink.levels.size()) ? sink.levels[pos + 1].first - pos : 1;
            printf("  text[%u..%u) level = %u (%s)  \"%.*ls\"\n",
                   pos, pos + len, lvl, (lvl & 1) ? "RTL" : "LTR", (int)len, mixed.c_str() + pos);
        }
    }
    printf("\n");

    const BOOL rtl = TRUE;
    std::vector<UINT16> glyphs(n);
    std::vector<UINT16> cluster(n + 1);
    std::vector<DWRITE_SHAPING_TEXT_PROPERTIES> props(n);
    std::vector<DWRITE_SHAPING_GLYPH_PROPERTIES> gprops(n);
    UINT32 glyphCount = 0;

    DWRITE_SCRIPT_ANALYSIS analysis{};
    analysis.script = 0x0600;   // Unicode script index for Arabic
    analysis.shapes = DWRITE_SCRIPT_SHAPES_DEFAULT;

    HRESULT hr = analyzer->GetGlyphs(
        arabic.c_str(), n, face.Get(), FALSE, rtl, &analysis, L"ar", nullptr, nullptr, nullptr, 0,
        (UINT32)glyphs.size(), cluster.data(), props.data(), glyphs.data(), gprops.data(), &glyphCount);

    if (FAILED(hr)) { printf("GetGlyphs failed: 0x%08X\n", (unsigned)hr); return 1; }

    printf("\nGetGlyphs(isRightToLeft=TRUE) -> %u glyph(s)\n", glyphCount);
    printf("clusterMap: ");
    for (UINT32 i = 0; i <= n; ++i) printf("%u ", cluster[i]);
    printf("\n\n");

    // The decisive question: which logical character does each glyph correspond to?
    printf("glyph index -> source character:\n");
    for (UINT32 g = 0; g < glyphCount; ++g)
    {
        // clusterMap is monotonic; find the text range this glyph belongs to.
        UINT32 cp = 0xFFFF;
        for (UINT32 t = 0; t < n; ++t)
        {
            if (cluster[t] <= g && g < cluster[t + 1]) { cp = (UINT32)(UINT16)arabic[t]; break; }
        }
        printf("  glyph[%u] = font glyph %5u  <- source char %s\n", g, glyphs[g], U16((char32_t)cp));
    }

    // If glyph[0] maps to the FIRST logical char (seen, U+0633), DirectWrite
    // returned them in logical order, so L2 must reverse the clusters.
    bool logicalOrder = false;
    for (UINT32 g = 0; g < glyphCount; ++g)
    {
        for (UINT32 t = 0; t < n; ++t)
        {
            if (cluster[t] <= g && g < cluster[t + 1])
            {
                if (g == 0 && t == 0) { logicalOrder = true; }
                break;
            }
        }
        if (g == 0) break;
    }

    printf("\n==================== VERDICT ====================\n");
    if (logicalOrder)
    {
        printf("glyph[0] corresponds to the FIRST logical character.\n");
        printf("=> DirectWrite returns RTL runs in LOGICAL order.\n");
        printf("=> reverseWithinSameLevelRun must be TRUE  (current default)\n");
    }
    else
    {
        printf("glyph[0] corresponds to the LAST logical character.\n");
        printf("=> DirectWrite returns RTL runs in VISUAL order.\n");
        printf("=> reverseWithinSameLevelRun must be FALSE\n");
    }
    printf("==================================================\n");
    return 0;
}
