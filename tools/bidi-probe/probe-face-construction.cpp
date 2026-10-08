// THE DECISIVE TEST, second edition.
//
// The first edition of this probe built the font face from a file path with
// CreateFontFileReference, and the analyzer path never shaped. But the terminal
// does not do that: it resolves the family through GetSystemFontCollection and
// calls IDWriteFont::CreateFontFace, which is exactly what IDWriteTextLayout
// does internally. So the earlier probe may have been testing a different code
// path in DirectWrite, not the one the renderer uses.
//
// This probe resolves the face BOTH ways and shapes through the analyzer with
// each, so we can tell whether the face construction or the call itself is what
// skips GSUB.
//
//   cl /nologo /EHsc /std:c++20 probe-face-construction.cpp /Fe:probe-face-construction.exe /link dwrite.lib ole32.lib
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <cstdio>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

static std::vector<UINT16> ShapeAnalyzer(IDWriteTextAnalyzer* analyzer, IDWriteFontFace* face, const std::wstring& text)
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
    if (FAILED(hr)) { printf("  GetGlyphs FAILED 0x%08X\n", (unsigned)hr); return {}; }
    glyphs.resize(glyphCount);
    return glyphs;
}

static void Prn(const char* tag, const std::vector<UINT16>& v)
{
    printf("  %-34s n=%zu ids=", tag, v.size());
    for (auto g : v) printf("%u ", g);
    printf("\n");
}

int main(int argc, char** argv)
{
    if (argc < 2) { printf("usage: probe-face-construction.exe <familyName>\n"); return 1; }
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
    if (FAILED(coll->FindFamilyName(family.c_str(), &idx, &exists)) || !exists)
    {
        printf("family \"%s\" not found\n", argv[1]);
        return 1;
    }
    ComPtr<IDWriteFontFamily> fam;
    coll->GetFontFamily(idx, &fam);
    ComPtr<IDWriteFont> font;
    fam->GetFirstMatchingFont(DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STRETCH_NORMAL, DWRITE_FONT_STYLE_NORMAL, &font);

    // Way 1: IDWriteFont::CreateFontFace, the terminal's path.
    ComPtr<IDWriteFontFace> faceFromCollection;
    if (FAILED(font->CreateFontFace(&faceFromCollection))) { printf("CreateFontFace failed\n"); return 1; }

    // Way 2: pull the underlying file out of the face and rebuild from scratch,
    // which is what the original probe did via a hard-coded path.
    ComPtr<IDWriteFontFile> fontFile;
    UINT32 fileCount = 1;
    faceFromCollection->GetFiles(&fileCount, &fontFile);
    ComPtr<IDWriteFontFace> faceFromFile;
    if (fontFile)
    {
        IDWriteFontFile* files[] = { fontFile.Get() };
        factory->CreateFontFace(DWRITE_FONT_FACE_TYPE_TRUETYPE, 1, files, 0,
            DWRITE_FONT_SIMULATIONS_NONE, &faceFromFile);
    }

    const std::wstring salam = L"\u0633\u0644\u0627\u0645"; // s-l-a-m
    const std::wstring lamAlone = L"\u0644";

    printf("family: %s\n\n", argv[1]);

    UINT32 code = 0x0644;
    UINT16 lamCmap = 0;
    faceFromCollection->GetGlyphIndices(&code, 1, &lamCmap);
    printf("  raw cmap lam = %u\n\n", lamCmap);

    auto a = ShapeAnalyzer(analyzer.Get(), faceFromCollection.Get(), salam);
    auto b = ShapeAnalyzer(analyzer.Get(), faceFromFile.Get(), salam);
    auto aLam = ShapeAnalyzer(analyzer.Get(), faceFromCollection.Get(), lamAlone);
    auto bLam = ShapeAnalyzer(analyzer.Get(), faceFromFile.Get(), lamAlone);

    Prn("analyzer, face=COLLECTION, salam", a);
    Prn("analyzer, face=COLLECTION, lam", aLam);
    Prn("analyzer, face=FILE,       salam", b);
    Prn("analyzer, face=FILE,       lam", bLam);

    auto check = [&](const std::vector<UINT16>& s, const std::vector<UINT16>& l, const char* tag) {
        bool shaping = s.size() >= 2 && l.size() >= 1 &&
            (s[1] != l[0] || s.size() != salam.size());
        printf("\n%s: salam[1]=%s lam[0]=%s -> shaping %s\n",
            tag,
            s.size() > 1 ? std::to_string(s[1]).c_str() : "?",
            l.size() > 0 ? std::to_string(l[0]).c_str() : "?",
            shaping ? "YES" : "no");
    };
    check(a, aLam, "COLLECTION face");
    check(b, bLam, "FILE face");
    return 0;
}
