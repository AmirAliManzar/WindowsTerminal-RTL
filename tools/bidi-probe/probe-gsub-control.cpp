// Control test: does GetGlyphs apply GSUB at all, for ANY script?
//
// If shaping is broken generally, then even an English ligature like "fi" will
// come back as two glyphs. If "fi" fuses into one glyph but Arabic does not
// shape, then GSUB works and the Arabic script is the special case.
//
//   cl /nologo /EHsc /std:c++20 probe-gsub-control.cpp /Fe:probe-gsub-control.exe /link dwrite.lib ole32.lib
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <cstdio>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

static std::vector<UINT16> Shape(IDWriteTextAnalyzer* analyzer, IDWriteFontFace* face,
    const std::wstring& text, DWRITE_SCRIPT_ANALYSIS sa, const wchar_t* locale, bool rtl)
{
    std::vector<UINT16> clusterMap(text.size(), 0);
    std::vector<DWRITE_SHAPING_TEXT_PROPERTIES> textProps(text.size(), DWRITE_SHAPING_TEXT_PROPERTIES{});
    std::vector<UINT16> glyphs(text.size() * 2 + 16, 0);
    std::vector<DWRITE_SHAPING_GLYPH_PROPERTIES> glyphProps(text.size() * 2 + 16, DWRITE_SHAPING_GLYPH_PROPERTIES{});
    UINT32 glyphCount = 0;
    HRESULT hr = analyzer->GetGlyphs(
        text.c_str(), (UINT32)text.size(), face, false, rtl, &sa,
        locale, nullptr, nullptr, nullptr, 0,
        (UINT32)glyphs.size(), clusterMap.data(), textProps.data(),
        glyphs.data(), glyphProps.data(), &glyphCount);
    if (FAILED(hr)) { printf("  GetGlyphs FAILED 0x%08X\n", (unsigned)hr); return {}; }
    glyphs.resize(glyphCount);
    return glyphs;
}

static void Prn(const char* tag, const std::vector<UINT16>& v)
{
    printf("  %-30s chars->glyphs %zu->%zu ids=", tag, 1, v.size());
    for (auto g : v) printf("%u ", g);
    printf("\n");
}

int main(int argc, char** argv)
{
    if (argc < 2) { printf("usage: probe-gsub-control.exe <familyName>\n"); return 1; }
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

    printf("family: %s\n\n", argv[1]);

    DWRITE_SCRIPT_ANALYSIS latin{};  // script 0 = unspecified/latin
    latin.shapes = DWRITE_SCRIPT_SHAPES_DEFAULT;
    DWRITE_SCRIPT_ANALYSIS arabic{};
    arabic.script = 0x0600;
    arabic.shapes = DWRITE_SCRIPT_SHAPES_DEFAULT;

    // English ligature. A font with a "liga" table fuses f+i into one glyph.
    Prn("fi (latin ligature)", Shape(analyzer.Get(), face.Get(), L"fi", latin, L"en-US", false));
    Prn("ff (latin ligature)", Shape(analyzer.Get(), face.Get(), L"ff", latin, L"en-US", false));

    // Arabic, for comparison.
    Prn("lam alone", Shape(analyzer.Get(), face.Get(), L"\u0644", arabic, L"ar", true));
    Prn("salam", Shape(analyzer.Get(), face.Get(), L"\u0633\u0644\u0627\u0645", arabic, L"ar", true));
    return 0;
}
