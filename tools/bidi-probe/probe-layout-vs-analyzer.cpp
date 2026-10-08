// The decisive test for whether Arabic shaping actually happens.
//
// This shapes the same text two ways:
//   1. IDWriteTextAnalyzer::GetGlyphs with features=nullptr, which is the exact
//      call the Atlas renderer makes when the user has not set "features" in
//      their settings.
//   2. IDWriteTextLayout::Draw, which is the path Notepad, Edge and every other
//      normal Windows app uses. This is what "shaping works on Windows" means.
//
// If the two disagree about glyph ids for the same letter in different
// positions, then the analyzer call the terminal makes is not doing the shaping
// the rest of the OS does, and that is a real defect, not a font problem.
//
//   cl /nologo /EHsc /std:c++20 probe-layout-vs-analyzer.cpp /Fe:probe-layout-vs-analyzer.exe /link dwrite.lib ole32.lib
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <cstdio>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

// --------------------------------------------------------------- analyzer path
static std::vector<UINT16> ShapeViaAnalyzer(IDWriteTextAnalyzer* analyzer, IDWriteFontFace* face, const std::wstring& text)
{
    DWRITE_SCRIPT_ANALYSIS sa{};
    sa.script = 0x0600; // Arabic
    sa.shapes = DWRITE_SCRIPT_SHAPES_DEFAULT;
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

// --------------------------------------------------------------- layout path
// A sink that records the glyph runs a TextLayout emits while drawing.
struct GlyphCollector final : IDWriteTextRenderer
{
    ULONG __stdcall AddRef() noexcept override { return 1; }
    ULONG __stdcall Release() noexcept override { return 1; }
    HRESULT __stdcall QueryInterface(const IID& riid, void** ppv) noexcept override
    {
        if (IsEqualGUID(riid, __uuidof(IDWriteTextRenderer)) || IsEqualGUID(riid, __uuidof(IDWritePixelSnapping))) { *ppv = this; return S_OK; }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    HRESULT __stdcall IsPixelSnappingDisabled(void*, BOOL* isDisabled) noexcept override { *isDisabled = FALSE; return S_OK; }
    HRESULT __stdcall GetCurrentTransform(void*, DWRITE_MATRIX* transform) noexcept override { ZeroMemory(transform, sizeof(*transform)); transform->m11 = 1; transform->m22 = 1; return S_OK; }
    HRESULT __stdcall GetPixelsPerDip(void*, float* pixelsPerDip) noexcept override { *pixelsPerDip = 1.0f; return S_OK; }

    std::vector<UINT16> glyphs;
    HRESULT __stdcall DrawGlyphRun(void*, float, float, DWRITE_MEASURING_MODE, const DWRITE_GLYPH_RUN* glyphRun,
        const DWRITE_GLYPH_RUN_DESCRIPTION*, IUnknown*) noexcept override
    {
        for (UINT32 i = 0; i < glyphRun->glyphCount; i++)
            glyphs.push_back(glyphRun->glyphIndices[i]);
        return S_OK;
    }
    HRESULT __stdcall DrawUnderline(void*, float, float, const DWRITE_UNDERLINE*, IUnknown*) noexcept override { return S_OK; }
    HRESULT __stdcall DrawStrikethrough(void*, float, float, const DWRITE_STRIKETHROUGH*, IUnknown*) noexcept override { return S_OK; }
    HRESULT __stdcall DrawInlineObject(void*, float, float, IDWriteInlineObject*, BOOL, BOOL, IUnknown*) noexcept override { return S_OK; }
};

static std::vector<UINT16> ShapeViaLayout(IDWriteFactory* factory, const std::wstring& fontFamily, const std::wstring& text)
{
    ComPtr<IDWriteTextFormat> format;
    if (FAILED(factory->CreateTextFormat(fontFamily.c_str(), nullptr, DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 16.0f, L"ar", &format)))
        return {};
    format->SetReadingDirection(DWRITE_READING_DIRECTION_RIGHT_TO_LEFT);
    format->SetFlowDirection(DWRITE_FLOW_DIRECTION_TOP_TO_BOTTOM);
    format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
    format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);

    ComPtr<IDWriteTextLayout> layout;
    if (FAILED(factory->CreateTextLayout(text.c_str(), (UINT32)text.size(), format.Get(), 200.0f, 40.0f, &layout)))
        return {};

    GlyphCollector collector;
    HRESULT hr = layout->Draw(nullptr, &collector, 0.0f, 0.0f);
    if (FAILED(hr)) return {};
    return collector.glyphs;
}

int main(int argc, char** argv)
{
    if (argc < 2) { printf("usage: probe-layout-vs-analyzer.exe <fontFamilyName>\n"); return 1; }
    std::wstring family;
    int n = MultiByteToWideChar(CP_UTF8, 0, argv[1], -1, nullptr, 0);
    family.resize(n); MultiByteToWideChar(CP_UTF8, 0, argv[1], -1, &family[0], n);

    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ComPtr<IDWriteFactory> factory;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown**)&factory))) { printf("no DWrite\n"); return 1; }

    ComPtr<IDWriteTextAnalyzer> analyzer;
    factory->CreateTextAnalyzer(&analyzer);

    // The analyzer path needs an explicit font face. Resolve it by family name
    // from the system collection so both paths shape the same font.
    ComPtr<IDWriteFontCollection> coll;
    factory->GetSystemFontCollection(&coll, FALSE);
    UINT32 idx = 0; BOOL exists = FALSE;
    if (FAILED(coll->FindFamilyName(family.c_str(), &idx, &exists)) || !exists)
    {
        printf("family \"%s\" not found in the system collection\n", argv[1]);
        return 1;
    }
    ComPtr<IDWriteFontFamily> fam;
    coll->GetFontFamily(idx, &fam);
    ComPtr<IDWriteFont> font;
    fam->GetFirstMatchingFont(DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STRETCH_NORMAL, DWRITE_FONT_STYLE_NORMAL, &font);
    ComPtr<IDWriteFontFace> face;
    font->CreateFontFace(&face);

    const std::wstring lamAlone = L"\u0644";                       // ل
    const std::wstring salam = L"\u0633\u0644\u0627\u0645";         // سلام

    auto aLamAlone = ShapeViaAnalyzer(analyzer.Get(), face.Get(), lamAlone);
    auto aSalam = ShapeViaAnalyzer(analyzer.Get(), face.Get(), salam);
    auto lLamAlone = ShapeViaLayout(factory.Get(), family, lamAlone);
    auto lSalam = ShapeViaLayout(factory.Get(), family, salam);

    auto prn = [](const char* tag, const std::vector<UINT16>& v) {
        printf("  %-22s", tag);
        for (auto g : v) printf(" %u", g);
        printf("\n");
    };

    printf("font: %s\n\n", argv[1]);
    printf("ANALYZER path (features=nullptr, what the Atlas renderer sends):\n");
    prn("lam alone", aLamAlone);
    prn("salam", aSalam);
    printf("\nLAYOUT path (what Notepad/Edge send):\n");
    prn("lam alone", lLamAlone);
    prn("salam", lSalam);

    printf("\n");
    // The signature of shaping is that the same codepoint yields different
    // glyphs in different positions, OR that the glyph count differs from the
    // character count because a ligature fused two characters into one glyph.
    auto analyzerFused = aSalam.size() != salam.size();
    auto layoutFused = lSalam.size() != salam.size();
    bool anShaping = aLamAlone.size() >= 1 && aSalam.size() >= 2 &&
        (aSalam[1] != aLamAlone[0] || analyzerFused);
    bool layShaping = lLamAlone.size() >= 1 && lSalam.size() >= 2 &&
        (lSalam[1] != lLamAlone[0] || layoutFused);
    printf("analyzer: %zu chars -> %zu glyphs  shapes positionally: %s\n", salam.size(), aSalam.size(), anShaping ? "YES" : "no");
    printf("layout  : %zu chars -> %zu glyphs  shapes positionally: %s\n", salam.size(), lSalam.size(), layShaping ? "YES" : "no");
    return 0;
}
