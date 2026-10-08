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

static void Prn(const char* tag, size_t chars, const std::vector<UINT16>& v)
{
    printf("  %-30s chars->glyphs %zu->%zu ids=", tag, chars, v.size());
    for (auto g : v) printf("%u ", g);
    printf("\n");
}

int main(int argc, char** argv)
{
    std::wstring family;
    if (argc >= 2)
    {
        int n = MultiByteToWideChar(CP_UTF8, 0, argv[1], -1, nullptr, 0);
        family.resize(n); MultiByteToWideChar(CP_UTF8, 0, argv[1], -1, &family[0], n);
    }

    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ComPtr<IDWriteFactory> factory;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown**)&factory))) { printf("no DWrite\n"); return 1; }
    ComPtr<IDWriteTextAnalyzer> analyzer;
    factory->CreateTextAnalyzer(&analyzer);
    ComPtr<IDWriteFontCollection> coll;
    factory->GetSystemFontCollection(&coll, FALSE);

    // The optional Arabic fonts this probe prefers are not installed on hosted
    // CI runners, so fall back to families every Windows box has and pick the
    // first one that is actually there.
    const wchar_t* candidates[] = { L"Segoe UI", L"Arial", L"Tahoma", L"Times New Roman" };
    bool chosen = false;
    if (!family.empty())
    {
        UINT32 i = 0; BOOL ex = FALSE;
        chosen = SUCCEEDED(coll->FindFamilyName(family.c_str(), &i, &ex)) && ex;
    }
    if (!chosen)
    {
        family.clear();
        for (const wchar_t* c : candidates)
        {
            UINT32 i = 0; BOOL ex = FALSE;
            if (SUCCEEDED(coll->FindFamilyName(c, &i, &ex)) && ex) { family = c; chosen = true; break; }
        }
    }
    if (!chosen) { printf("no usable font family on this machine; skipping\n"); return 0; }

    UINT32 idx = 0; BOOL exists = FALSE;
    coll->FindFamilyName(family.c_str(), &idx, &exists);
    ComPtr<IDWriteFontFamily> fam;
    coll->GetFontFamily(idx, &fam);
    ComPtr<IDWriteFont> font;
    fam->GetFirstMatchingFont(DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STRETCH_NORMAL, DWRITE_FONT_STYLE_NORMAL, &font);
    ComPtr<IDWriteFontFace> face;
    font->CreateFontFace(&face);

    // Report which family we ended up on, since it may not be the one requested.
    {
        int blen = WideCharToMultiByte(CP_UTF8, 0, family.c_str(), -1, nullptr, 0, nullptr, nullptr);
        std::string buf(blen, '\0');
        WideCharToMultiByte(CP_UTF8, 0, family.c_str(), -1, &buf[0], blen, nullptr, nullptr);
        printf("family: %s\n\n", buf.c_str());
    }

    DWRITE_SCRIPT_ANALYSIS latin{};  // script 0 = unspecified/latin
    latin.shapes = DWRITE_SCRIPT_SHAPES_DEFAULT;
    DWRITE_SCRIPT_ANALYSIS arabic{};
    arabic.script = 0x0600;
    arabic.shapes = DWRITE_SCRIPT_SHAPES_DEFAULT;

    // English ligature. A font with a "liga" table fuses f+i into one glyph.
    Prn("fi (latin ligature)", 2, Shape(analyzer.Get(), face.Get(), L"fi", latin, L"en-US", false));
    Prn("ff (latin ligature)", 2, Shape(analyzer.Get(), face.Get(), L"ff", latin, L"en-US", false));

    // Arabic, for comparison.
    Prn("lam alone", 1, Shape(analyzer.Get(), face.Get(), L"\u0644", arabic, L"ar", true));
    Prn("salam", 4, Shape(analyzer.Get(), face.Get(), L"\u0633\u0644\u0627\u0645", arabic, L"ar", true));
    return 0;
}
