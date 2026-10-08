// Re-runs the lam-alef survey with the script analysis shape value that
// AnalyzeScript actually produces for Arabic text. The original probe-lamalef
// hand-set shapes = DWRITE_SCRIPT_SHAPES_DEFAULT, which the survey above showed
// suppresses positional shaping entirely; every font scored 0/22 because of
// that, not because no font fuses lam-alef.
//
//   cl /nologo /EHsc /std:c++20 probe-lamalef-real.cpp /Fe:probe-lamalef-real.exe /link dwrite.lib ole32.lib
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
    DWRITE_SCRIPT_ANALYSIS sa{};
    HRESULT __stdcall SetScriptAnalysis(UINT32, UINT32, const DWRITE_SCRIPT_ANALYSIS* s) noexcept override
    { sa = *s; return S_OK; }
    HRESULT __stdcall SetLineBreakpoints(UINT32, UINT32, const DWRITE_LINE_BREAKPOINT*) noexcept override { return S_OK; }
    HRESULT __stdcall SetBidiLevel(UINT32, UINT32, UINT8, UINT8) noexcept override { return S_OK; }
    HRESULT __stdcall SetNumberSubstitution(UINT32, UINT32, IDWriteNumberSubstitution*) noexcept override { return S_OK; }
};

struct AnalysisSource final : IDWriteTextAnalysisSource
{
    const wchar_t* text;
    AnalysisSource(const wchar_t* t) : text(t) {}
    ULONG __stdcall AddRef() noexcept override { return 1; }
    ULONG __stdcall Release() noexcept override { return 1; }
    HRESULT __stdcall QueryInterface(const IID& riid, void** ppv) noexcept override
    {
        if (IsEqualGUID(riid, __uuidof(IDWriteTextAnalysisSource))) { *ppv = this; return S_OK; }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    HRESULT __stdcall GetTextAtPosition(UINT32 pos, const wchar_t** out, UINT32* len) noexcept override
    { *out = text + pos; *len = (UINT32)wcslen(text) - pos; return S_OK; }
    HRESULT __stdcall GetTextBeforePosition(UINT32 pos, const wchar_t** out, UINT32* len) noexcept override
    { *out = text; *len = pos; return S_OK; }
    DWRITE_READING_DIRECTION __stdcall GetParagraphReadingDirection() noexcept override { return DWRITE_READING_DIRECTION_RIGHT_TO_LEFT; }
    HRESULT __stdcall GetLocaleName(UINT32, UINT32* len, const wchar_t** name) noexcept override
    { static const wchar_t l[] = L"ar"; *len = 2; *name = l; return S_OK; }
    HRESULT __stdcall GetNumberSubstitution(UINT32, UINT32* len, IDWriteNumberSubstitution** sub) noexcept override
    { *len = 0; *sub = nullptr; return S_OK; }
};

static std::vector<UINT16> Shape(IDWriteTextAnalyzer* analyzer, IDWriteFontFace* face, const std::wstring& text, const DWRITE_SCRIPT_ANALYSIS& sa)
{
    std::vector<UINT16> clusterMap(text.size(), 0);
    std::vector<DWRITE_SHAPING_TEXT_PROPERTIES> textProps(text.size(), DWRITE_SHAPING_TEXT_PROPERTIES{});
    std::vector<UINT16> glyphs(text.size() * 2 + 16, 0);
    std::vector<DWRITE_SHAPING_GLYPH_PROPERTIES> glyphProps(text.size() * 2 + 16, DWRITE_SHAPING_GLYPH_PROPERTIES{});
    UINT32 glyphCount = 0;
    HRESULT hr = analyzer->GetGlyphs(
        text.c_str(), (UINT32)text.size(), face, false, true, &sa,
        L"ar", nullptr, nullptr, nullptr, 0,
        (UINT32)glyphs.size(), clusterMap.data(), textProps.data(),
        glyphs.data(), glyphProps.data(), &glyphCount);
    if (FAILED(hr)) return {};
    glyphs.resize(glyphCount);
    return glyphs;
}

static ComPtr<IDWriteFontFace> FaceFromCollection(IDWriteFactory* factory, const std::wstring& family)
{
    ComPtr<IDWriteFontCollection> coll;
    factory->GetSystemFontCollection(&coll, FALSE);
    UINT32 idx = 0; BOOL exists = FALSE;
    if (FAILED(coll->FindFamilyName(family.c_str(), &idx, &exists)) || !exists) return nullptr;
    ComPtr<IDWriteFontFamily> fam;
    coll->GetFontFamily(idx, &fam);
    ComPtr<IDWriteFont> font;
    fam->GetFirstMatchingFont(DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STRETCH_NORMAL, DWRITE_FONT_STYLE_NORMAL, &font);
    ComPtr<IDWriteFontFace> face;
    font->CreateFontFace(&face);
    return face;
}

int main()
{
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ComPtr<IDWriteFactory> factory;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown**)&factory))) { printf("no DWrite\n"); return 1; }
    ComPtr<IDWriteTextAnalyzer> analyzer;
    factory->CreateTextAnalyzer(&analyzer);

    // Resolve the real script analysis for Arabic the way the terminal does.
    const std::wstring probe = L"\u0644\u0627";
    AnalysisSource src(probe.c_str());
    AnalysisSink sink;
    if (FAILED(analyzer->AnalyzeScript(&src, 0, (UINT32)probe.size(), &sink))) { printf("AnalyzeScript failed\n"); return 1; }
    printf("resolved script: 0x%04X shapes=%u\n\n", (unsigned)sink.sa.script, (unsigned)sink.sa.shapes);

    ComPtr<IDWriteFontCollection> coll;
    factory->GetSystemFontCollection(&coll, FALSE);

    printf("%-30s %s\n", "family", "lam-alef glyphs");
    printf("%-30s %s\n", "------", "--------------");

    int tested = 0, fused = 0;
    for (UINT32 i = 0; i < coll->GetFontFamilyCount(); i++)
    {
        ComPtr<IDWriteFontFamily> fam;
        coll->GetFontFamily(i, &fam);
        ComPtr<IDWriteLocalizedStrings> names;
        fam->GetFamilyNames(&names);
        UINT32 nameLen = 0;
        names->GetStringLength(0, &nameLen);
        std::wstring name(nameLen, L' ');
        names->GetString(0, &name[0], nameLen + 1);

        ComPtr<IDWriteFont> font;
        fam->GetFirstMatchingFont(DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STRETCH_NORMAL, DWRITE_FONT_STYLE_NORMAL, &font);
        ComPtr<IDWriteFontFace> face;
        if (FAILED(font->CreateFontFace(&face))) continue;

        UINT32 lamCode = 0x0644, alefCode = 0x0627;
        UINT16 both[2]{};
        face->GetGlyphIndices(&lamCode, 1, &both[0]);
        face->GetGlyphIndices(&alefCode, 1, &both[1]);
        if (both[0] == 0 || both[1] == 0) continue; // no Arabic coverage

        tested++;
        auto g = Shape(analyzer.Get(), face.Get(), probe, sink.sa);
        bool isFused = (g.size() == 1 && g[0] != 0);
        if (isFused) fused++;
        printf("%-30S n=%zu ids=", name.c_str(), g.size());
        for (auto x : g) printf("%u ", x);
        if (isFused) printf(" FUSED");
        printf("\n");
    }

    printf("\nfuses lam-alef: %d of %d families with Arabic coverage\n", fused, tested);
    return 0;
}
