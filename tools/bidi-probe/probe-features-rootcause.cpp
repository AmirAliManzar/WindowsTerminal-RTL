// Isolates which argument makes IDWriteTextAnalyzer::GetGlyphs skip Arabic
// shaping. The terminal sends features=nullptr when the user has no "features"
// setting. Notepad, via IDWriteTextLayout, sends an explicit feature list.
//
// This shapes "salam" four ways, varying one argument at a time:
//   A. features = nullptr, featureRanges = 0        <- what the terminal sends
//   B. features = &emptyList, featureRanges = 0
//   C. features = &oneFeature(ccmp), featureRanges = 1
//   D. features = &arabicSet (ccmp init medi fina rlig), featureRanges = 1
//
// If only D shapes, then the terminal's features=nullptr path is why Arabic
// letters come out disconnected, and the fix is to send the Arabic feature set
// whenever the run's script is Arabic.
//
//   cl /nologo /EHsc /std:c++20 probe-features-rootcause.cpp /Fe:probe-features-rootcause.exe /link dwrite.lib ole32.lib
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <cstdio>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

static std::vector<UINT16> ShapeVariants(IDWriteTextAnalyzer* analyzer, IDWriteFontFace* face,
    const std::wstring& text,
    const DWRITE_TYPOGRAPHIC_FEATURES* features, const UINT32* rangeLens, UINT32 rangeCount)
{
    DWRITE_SCRIPT_ANALYSIS sa{};
    sa.script = 0x0600; // Arabic
    sa.shapes = DWRITE_SCRIPT_SHAPES_DEFAULT;
    std::vector<UINT16> clusterMap(text.size(), 0);
    std::vector<DWRITE_SHAPING_TEXT_PROPERTIES> textProps(text.size(), DWRITE_SHAPING_TEXT_PROPERTIES{});
    std::vector<UINT16> glyphs(text.size() * 2 + 16, 0);
    std::vector<DWRITE_SHAPING_GLYPH_PROPERTIES> glyphProps(text.size() * 2 + 16, DWRITE_SHAPING_GLYPH_PROPERTIES{});
    UINT32 glyphCount = 0;
    // GetGlyphs takes the feature list as a pointer-to-array-of-pointers: one
    // pointer per range. Build that view, or pass nullptr for the no-range case.
    const DWRITE_TYPOGRAPHIC_FEATURES* onePtr = features;
    const DWRITE_TYPOGRAPHIC_FEATURES** featArray = (rangeCount > 0) ? &onePtr : nullptr;
    HRESULT hr = analyzer->GetGlyphs(
        text.c_str(), (UINT32)text.size(), face, false, true, &sa,
        L"ar", nullptr, featArray, rangeLens, rangeCount,
        (UINT32)glyphs.size(), clusterMap.data(), textProps.data(),
        glyphs.data(), glyphProps.data(), &glyphCount);
    if (FAILED(hr)) { printf("  GetGlyphs FAILED 0x%08X\n", (unsigned)hr); return {}; }
    glyphs.resize(glyphCount);
    return glyphs;
}

static void Prn(const char* tag, const std::vector<UINT16>& v)
{
    printf("  %-40s n=%zu ids=", tag, v.size());
    for (auto g : v) printf("%u ", g);
    printf("\n");
}

int main(int argc, char** argv)
{
    if (argc < 2) { printf("usage: probe-features-rootcause.exe <file.ttf>\n"); return 1; }
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

    const std::wstring salam = L"\u0633\u0644\u0627\u0645"; // salam in Arabic letters
    printf("font: %s\nshaping salam\n\n", argv[1]);

    // A: what the terminal sends
    auto a = ShapeVariants(analyzer.Get(), face.Get(), salam, nullptr, nullptr, 0);
    Prn("A features=nullptr (terminal)", a);

    // B: an empty feature list
    DWRITE_TYPOGRAPHIC_FEATURES emptyList{};
    auto b = ShapeVariants(analyzer.Get(), face.Get(), salam, &emptyList, nullptr, 0);
    Prn("B features=&emptyList", b);

    // C: one feature
    DWRITE_FONT_FEATURE ccmpFeature = { DWRITE_FONT_FEATURE_TAG_CONTEXTUAL_ALTERNATES, 1 };
    DWRITE_TYPOGRAPHIC_FEATURES oneFeature{};
    oneFeature.features = &ccmpFeature;
    oneFeature.featureCount = 1;
    UINT32 oneLen = (UINT32)salam.size();
    auto c = ShapeVariants(analyzer.Get(), face.Get(), salam, &oneFeature, &oneLen, 1);
    Prn("C features=ccmp", c);

    // D: the Arabic shaping set
    DWRITE_FONT_FEATURE arabic[] = {
        { DWRITE_FONT_FEATURE_TAG_CONTEXTUAL_ALTERNATES, 1 },
        { DWRITE_FONT_FEATURE_TAG_STANDARD_LIGATURES, 1 },
        { DWRITE_FONT_FEATURE_TAG_CONTEXTUAL_LIGATURES, 1 },
    };
    DWRITE_TYPOGRAPHIC_FEATURES arabicList{};
    arabicList.features = arabic;
    arabicList.featureCount = _countof(arabic);
    UINT32 arabicLen = (UINT32)salam.size();
    auto d = ShapeVariants(analyzer.Get(), face.Get(), salam, &arabicList, &arabicLen, 1);
    Prn("D features=Arabic set (ccmp liga clig)", d);

    // Report the raw cmap ids as the unshaped baseline
    UINT32 codes[] = { 0x0633, 0x0644, 0x0627, 0x0645 };
    UINT16 cmap[4]{};
    face->GetGlyphIndices(codes, 4, cmap);
    printf("\n  raw cmap:              n=4 ids=%u %u %u %u\n", cmap[0], cmap[1], cmap[2], cmap[3]);

    auto shaped = [&](const std::vector<UINT16>& v) {
        if (v.size() != 4) return false;
        return v[1] != cmap[1]; // the lam must differ from its isolated form
    };
    printf("\n");
    printf("A (terminal) shapes : %s\n", shaped(a) ? "YES" : "no");
    printf("B shapes            : %s\n", shaped(b) ? "YES" : "no");
    printf("C shapes            : %s\n", shaped(c) ? "YES" : "no");
    printf("D shapes            : %s\n", shaped(d) ? "YES" : "no");
    return 0;
}
