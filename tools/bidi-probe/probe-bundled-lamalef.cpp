// The bundled Cascadia fonts and the system Cascadia Code are not the same
// file: the system one reports glyph 0 for lam (no Arabic coverage at all),
// while the one shipped beside WindowsTerminal.exe reports 1270. This probe
// shapes lam-alef through the shipped .ttf directly, using the script analysis
// value AnalyzeScript actually returns, so we can see whether the bundled font
// fuses lam-alef when it is the one in use.
//
//   cl /nologo /EHsc /std:c++20 probe-bundled-lamalef.cpp /Fe:probe-bundled-lamalef.exe /link dwrite.lib ole32.lib
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
    if (FAILED(hr)) { printf("GetGlyphs FAILED 0x%08X\n", (unsigned)hr); return {}; }
    glyphs.resize(glyphCount);
    return glyphs;
}

int main(int argc, char** argv)
{
    if (argc < 2) { printf("usage: probe-bundled-lamalef.exe <file.ttf> [<file2.ttf> ...]\n"); return 1; }

    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ComPtr<IDWriteFactory> factory;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown**)&factory))) { printf("no DWrite\n"); return 1; }
    ComPtr<IDWriteTextAnalyzer> analyzer;
    factory->CreateTextAnalyzer(&analyzer);

    const std::wstring lamAlef = L"\u0644\u0627";
    AnalysisSource src(lamAlef.c_str());
    AnalysisSink sink;
    if (FAILED(analyzer->AnalyzeScript(&src, 0, (UINT32)lamAlef.size(), &sink)))
    { printf("AnalyzeScript failed\n"); return 1; }

    for (int i = 1; i < argc; i++)
    {
        std::wstring path;
        int n = MultiByteToWideChar(CP_UTF8, 0, argv[i], -1, nullptr, 0);
        path.resize(n); MultiByteToWideChar(CP_UTF8, 0, argv[i], -1, &path[0], n);

        ComPtr<IDWriteFontFile> fontFile;
        if (FAILED(factory->CreateFontFileReference(path.c_str(), nullptr, &fontFile))) { printf("%s: no file\n", argv[i]); continue; }
        IDWriteFontFile* files[] = { fontFile.Get() };
        ComPtr<IDWriteFontFace> face;
        if (FAILED(factory->CreateFontFace(DWRITE_FONT_FACE_TYPE_TRUETYPE, 1, files, 0,
            DWRITE_FONT_SIMULATIONS_NONE, &face))) { printf("%s: no face\n", argv[i]); continue; }

        printf("%s\n", argv[i]);
        UINT32 lamCode = 0x0644, alefCode = 0x0627;
        UINT16 lamG = 0, alefG = 0;
        face->GetGlyphIndices(&lamCode, 1, &lamG);
        face->GetGlyphIndices(&alefCode, 1, &alefG);
        printf("  cmap lam=%u alef=%u\n", lamG, alefG);

        // shape with the resolved analysis (what the terminal does)
        auto g = Shape(analyzer.Get(), face.Get(), lamAlef, sink.sa);
        printf("  shaped  n=%zu ids=", g.size());
        for (auto x : g) printf("%u ", x);
        bool fused = (g.size() == 1 && g[0] != 0 && g[0] != lamG && g[0] != alefG);
        if (fused) printf(" FUSED");
        printf("\n");

        // also shape salam so we can see positional shaping in context
        const std::wstring salam = L"\u0633\u0644\u0627\u0645";
        auto gs = Shape(analyzer.Get(), face.Get(), salam, sink.sa);
        UINT32 codes[] = { 0x0633, 0x0644, 0x0627, 0x0645 };
        UINT16 cmap[4]{};
        face->GetGlyphIndices(codes, 4, cmap);
        bool positional = gs.size() >= 3 && (gs[2] != cmap[2] || gs[1] != cmap[1]);
        printf("  salam   n=%zu ids=", gs.size());
        for (auto x : gs) printf("%u ", x);
        printf(" (cmap: %u %u %u %u) positional=%s\n", cmap[0], cmap[1], cmap[2], cmap[3], positional ? "YES" : "no");
        printf("\n");
    }
    return 0;
}
