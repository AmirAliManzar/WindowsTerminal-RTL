// Reproduces the terminal's exact shaping chain: AnalyzeBidi + AnalyzeScript,
// then GetGlyphs with the script analysis those calls actually produce.
//
// Earlier probes hand-set script = 0x0600 (Arabic) with shapes = DEFAULT. This
// probe instead lets DirectWrite resolve the script itself, which is what the
// renderer does. If the resolved script analysis differs from the hand-set one,
// that is the missing ingredient for positional shaping.
//
//   cl /nologo /EHsc /std:c++20 probe-real-shaping-chain.cpp /Fe:probe-real-shaping-chain.exe /link dwrite.lib ole32.lib
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <cstdio>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

struct AnalysisSink final : IDWriteTextAnalysisSink
{
    ULONG __stdcall AddRef() noexcept override { return 1; }
    ULONG __stdcall Release() noexcept override { return 1; }
    HRESULT __stdcall QueryInterface(const IID& riid, void** ppv) noexcept override
    {
        if (IsEqualGUID(riid, __uuidof(IDWriteTextAnalysisSink))) { *ppv = this; return S_OK; }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    std::vector<DWRITE_SCRIPT_ANALYSIS> scripts;
    HRESULT __stdcall SetScriptAnalysis(UINT32, UINT32 len, const DWRITE_SCRIPT_ANALYSIS* sa) noexcept override
    {
        scripts.push_back(*sa);
        printf("    SetScriptAnalysis len=%u script=0x%04X shapes=%u\n", len, (unsigned)sa->script, (unsigned)sa->shapes);
        return S_OK;
    }
    HRESULT __stdcall SetLineBreakpoints(UINT32, UINT32, const DWRITE_LINE_BREAKPOINT*) noexcept override { return S_OK; }
    HRESULT __stdcall SetBidiLevel(UINT32, UINT32, UINT8, UINT8) noexcept override { return S_OK; }
    HRESULT __stdcall SetNumberSubstitution(UINT32, UINT32, IDWriteNumberSubstitution*) noexcept override { return S_OK; }
};

struct AnalysisSource final : IDWriteTextAnalysisSource
{
    const wchar_t* text;
    const wchar_t* locale;
    AnalysisSource(const wchar_t* t, const wchar_t* l) : text(t), locale(l) {}
    ULONG __stdcall AddRef() noexcept override { return 1; }
    ULONG __stdcall Release() noexcept override { return 1; }
    HRESULT __stdcall QueryInterface(const IID& riid, void** ppv) noexcept override
    {
        if (IsEqualGUID(riid, __uuidof(IDWriteTextAnalysisSource))) { *ppv = this; return S_OK; }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    HRESULT __stdcall GetTextAtPosition(UINT32 pos, const wchar_t** out, UINT32* len) noexcept override
    {
        if (pos != 0) { *out = L""; *len = 0; return S_OK; }
        *out = text; *len = (UINT32)wcslen(text); return S_OK;
    }
    HRESULT __stdcall GetTextBeforePosition(UINT32, const wchar_t** out, UINT32* len) noexcept override
    { *out = L""; *len = 0; return S_OK; }
    DWRITE_READING_DIRECTION __stdcall GetParagraphReadingDirection() noexcept override { return DWRITE_READING_DIRECTION_RIGHT_TO_LEFT; }
    HRESULT __stdcall GetLocaleName(UINT32, UINT32* len, const wchar_t** name) noexcept override
    { *len = (UINT32)wcslen(locale); *name = locale; return S_OK; }
    HRESULT __stdcall GetNumberSubstitution(UINT32, UINT32* len, IDWriteNumberSubstitution** sub) noexcept override
    { *len = 0; *sub = nullptr; return S_OK; }
};

static std::vector<UINT16> RunGetGlyphs(IDWriteTextAnalyzer* analyzer, IDWriteFontFace* face,
    const std::wstring& text, const DWRITE_SCRIPT_ANALYSIS& sa, bool rtl)
{
    std::vector<UINT16> clusterMap(text.size(), 0);
    std::vector<DWRITE_SHAPING_TEXT_PROPERTIES> textProps(text.size(), DWRITE_SHAPING_TEXT_PROPERTIES{});
    std::vector<UINT16> glyphs(text.size() * 2 + 16, 0);
    std::vector<DWRITE_SHAPING_GLYPH_PROPERTIES> glyphProps(text.size() * 2 + 16, DWRITE_SHAPING_GLYPH_PROPERTIES{});
    UINT32 glyphCount = 0;
    HRESULT hr = analyzer->GetGlyphs(
        text.c_str(), (UINT32)text.size(), face, false, rtl, &sa,
        L"ar", nullptr, nullptr, nullptr, 0,
        (UINT32)glyphs.size(), clusterMap.data(), textProps.data(),
        glyphs.data(), glyphProps.data(), &glyphCount);
    if (FAILED(hr)) { printf("  GetGlyphs FAILED 0x%08X\n", (unsigned)hr); return {}; }
    glyphs.resize(glyphCount);
    return glyphs;
}

int main(int argc, char** argv)
{
    if (argc < 2) { printf("usage: probe-real-shaping-chain.exe <familyName>\n"); return 1; }
    std::wstring family;
    int n = MultiByteToWideChar(CP_UTF8, 0, argv[1], -1, nullptr, 0);
    family.resize(n); MultiByteToWideChar(CP_UTF8, 0, argv[1], -1, &family[0], n);

    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ComPtr<IDWriteFactory> factory;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown**)&factory))) { printf("no DWrite\n"); return 1; }
    ComPtr<IDWriteTextAnalyzer> analyzer;
    factory->CreateTextAnalyzer(&analyzer);
    ComPtr<IDWriteFontCollection> coll;
    factory->GetSystemFontCollection(&coll, FALSE);
    UINT32 idx = 0; BOOL exists = FALSE;
    if (FAILED(coll->FindFamilyName(family.c_str(), &idx, &exists)) || !exists) { printf("family not found\n"); return 1; }
    ComPtr<IDWriteFontFamily> fam;
    coll->GetFontFamily(idx, &fam);
    ComPtr<IDWriteFont> font;
    fam->GetFirstMatchingFont(DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STRETCH_NORMAL, DWRITE_FONT_STYLE_NORMAL, &font);
    ComPtr<IDWriteFontFace> face;
    font->CreateFontFace(&face);

    const std::wstring salam = L"\u0633\u0644\u0627\u0645";
    printf("family: %s\n\n", argv[1]);

    printf("1) hand-set script analysis (what the earlier probes sent):\n");
    {
        DWRITE_SCRIPT_ANALYSIS hand{}; hand.script = 0x0600; hand.shapes = DWRITE_SCRIPT_SHAPES_DEFAULT;
        auto g = RunGetGlyphs(analyzer.Get(), face.Get(), salam, hand, true);
        printf("    salam -> n=%zu", g.size());
        for (auto x : g) printf(" %u", x);
        printf("\n\n");
    }

    printf("2) script analysis resolved by AnalyzeScript (what the terminal sends):\n");
    {
        AnalysisSource src(salam.c_str(), L"ar");
        AnalysisSink sink;
        printf("  AnalyzeScript reports:\n");
        HRESULT hr = analyzer->AnalyzeScript(&src, 0, (UINT32)salam.size(), &sink);
        if (FAILED(hr)) { printf("  AnalyzeScript FAILED 0x%08X\n", (unsigned)hr); return 1; }
        if (sink.scripts.empty()) { printf("  no script runs reported\n"); return 1; }
        auto g = RunGetGlyphs(analyzer.Get(), face.Get(), salam, sink.scripts[0], true);
        printf("  salam -> n=%zu", g.size());
        for (auto x : g) printf(" %u", x);
        printf("\n\n");
    }

    // Baseline for the unshaped expectation.
    UINT32 codes[] = { 0x0633, 0x0644, 0x0627, 0x0645 };
    UINT16 cmap[4]{};
    face->GetGlyphIndices(codes, 4, cmap);
    printf("raw cmap: %u %u %u %u\n", cmap[0], cmap[1], cmap[2], cmap[3]);
    return 0;
}
