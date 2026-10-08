// Decides whether DirectWrite applies GSUB init/medi/fina substitution when a
// caller passes no explicit typographic features. This is the exact call shape
// the terminal's analyzer path uses, so the answer is what actually happens at
// render time.
//
//   cl /nologo /EHsc /std:c++20 probe-shaping-features.cpp /Fe:probe-shaping-features.exe /link dwrite.lib ole32.lib
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

// Shape one string and return the glyph indices.
static std::vector<UINT16> Shape(IDWriteTextAnalyzer* analyzer, IDWriteFontFace* face, const std::wstring& text)
{
    DWRITE_SCRIPT_ANALYSIS sa{};
    sa.script = 0x0600; // Arabic
    sa.shapes = DWRITE_SCRIPT_SHAPES_DEFAULT;

    std::vector<UINT16> clusterMap(text.size(), 0);
    std::vector<DWRITE_SHAPING_TEXT_PROPERTIES> textProps(text.size(), DWRITE_SHAPING_TEXT_PROPERTIES{});
    std::vector<UINT16> glyphs(text.size() * 2 + 16, 0);
    std::vector<DWRITE_SHAPING_GLYPH_PROPERTIES> glyphProps(text.size() * 2 + 16, DWRITE_SHAPING_GLYPH_PROPERTIES{});
    UINT32 glyphCount = 0;

    // features = nullptr: the default set, which is what the terminal sends.
    HRESULT hr = analyzer->GetGlyphs(
        text.c_str(), (UINT32)text.size(), face, false, true, &sa,
        L"fa-IR", nullptr, nullptr, nullptr, 0,
        (UINT32)glyphs.size(), clusterMap.data(), textProps.data(),
        glyphs.data(), glyphProps.data(), &glyphCount);
    if (FAILED(hr)) { printf("GetGlyphs failed 0x%08X\n", (unsigned)hr); return {}; }
    glyphs.resize(glyphCount);
    return glyphs;
}

int main(int argc, char** argv)
{
    if (argc < 2) { printf("usage: probe-shaping-features.exe <file.ttf>\n"); return 1; }
    std::wstring path;
    int n = MultiByteToWideChar(CP_UTF8, 0, argv[1], -1, nullptr, 0);
    path.resize(n); MultiByteToWideChar(CP_UTF8, 0, argv[1], -1, &path[0], n);

    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ComPtr<IDWriteFactory> factory;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown**)&factory))) { printf("no DWrite\n"); return 1; }

    ComPtr<IDWriteFontFile> fontFile;
    if (FAILED(factory->CreateFontFileReference(path.c_str(), nullptr, &fontFile))) { printf("no font file\n"); return 1; }
    IDWriteFontFile* files[] = { fontFile.Get() };
    ComPtr<IDWriteFontFace> face;
    if (FAILED(factory->CreateFontFace(DWRITE_FONT_FACE_TYPE_TRUETYPE, 1, files, 0, DWRITE_FONT_SIMULATIONS_NONE, &face))) { printf("no face\n"); return 1; }

    ComPtr<IDWriteTextAnalyzer> analyzer;
    factory->CreateTextAnalyzer(&analyzer);

    printf("font: %s\n\n", argv[1]);

    // The decisive comparison. U+0644 (lam) alone takes the isolated form. The
    // same lam in the middle of "salam" should take a different glyph if init/
    // medi/fina substitution is running.
    auto lamAlone = Shape(analyzer.Get(), face.Get(), L"\u0644");
    auto alefAlone = Shape(analyzer.Get(), face.Get(), L"\u0627");
    auto salam = Shape(analyzer.Get(), face.Get(), L"\u0633\u0644\u0627\u0645");

    printf("lam alone          :");
    for (auto g : lamAlone) printf(" %u", g);
    printf("\nalef alone         :");
    for (auto g : alefAlone) printf(" %u", g);
    printf("\nsalam (s l a m)    :");
    for (auto g : salam) printf(" %u", g);
    printf("\n\n");

    // Position 1 of salam is the lam. If it equals lam-alone, no positional
    // substitution happened.
    if (salam.size() >= 4 && lamAlone.size() >= 1)
    {
        if (salam[1] == lamAlone[0])
            printf("RESULT: lam in salam == lam alone -> positional shaping NOT applied (features=nullptr skips init/medi/fina)\n");
        else
            printf("RESULT: lam in salam (%u) != lam alone (%u) -> positional shaping IS applied\n", salam[1], lamAlone[0]);
    }

    // Also report the raw cmap ids so we can tell substitution from a font that
    // simply has one glyph for every position.
    UINT32 lamCode = 0x0644, alefCode = 0x0627;
    UINT16 lamCmap = 0, alefCmap = 0;
    face->GetGlyphIndices(&lamCode, 1, &lamCmap);
    face->GetGlyphIndices(&alefCode, 1, &alefCmap);
    printf("raw cmap: lam=%u alef=%u\n", lamCmap, alefCmap);
    if (lamAlone.size() >= 1)
        printf("lam alone glyph %u %s the raw cmap id\n", lamAlone[0], lamAlone[0] == lamCmap ? "IS" : "DIFFERS from");

    return 0;
}
