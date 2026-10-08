// Does the Cascadia font we ship actually shape Persian/Arabic, and does it
// fuse lam-alef into one glyph?
//
// Loads the .ttf from the command line directly through CreateFontFileReference,
// so this tests exactly the file that ships beside the portable EXE rather than
// whatever happens to be installed.
//
//   cl /nologo /EHsc /std:c++20 probe-bundled-font.cpp /Fe:probe-bundled-font.exe /link dwrite.lib

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
    DWRITE_READING_DIRECTION __stdcall GetParagraphReadingDirection() noexcept override { return DWRITE_READING_DIRECTION_RIGHT_TO_LEFT; }
    HRESULT __stdcall GetLocaleName(UINT32, UINT32* len, const WCHAR** name) noexcept override
    {
        *len = 5; static const WCHAR fa[] = L"fa-IR"; *name = fa; return S_OK;
    }
    HRESULT __stdcall GetNumberSubstitution(UINT32, UINT32*, IDWriteNumberSubstitution**) noexcept override { return E_NOTIMPL; }
};

struct Sink final : IDWriteTextAnalysisSink
{
    std::vector<UINT32> pos;
    std::vector<UINT8> lvl;
    ULONG __stdcall AddRef() noexcept override { return 1; }
    ULONG __stdcall Release() noexcept override { return 1; }
    HRESULT __stdcall QueryInterface(const IID& riid, void** ppv) noexcept override
    {
        if (IsEqualGUID(riid, __uuidof(IDWriteTextAnalysisSink))) { *ppv = this; return S_OK; }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    HRESULT __stdcall SetScriptAnalysis(UINT32, UINT32, const DWRITE_SCRIPT_ANALYSIS*) noexcept override { return S_OK; }
    HRESULT __stdcall SetLineBreakpoints(UINT32, UINT32, const DWRITE_LINE_BREAKPOINT*) noexcept override { return S_OK; }
    HRESULT __stdcall SetBidiLevel(UINT32 p, UINT32, UINT8, UINT8 resolved) noexcept override { pos.push_back(p); lvl.push_back(resolved); return S_OK; }
    HRESULT __stdcall SetNumberSubstitution(UINT32, UINT32, IDWriteNumberSubstitution*) noexcept override { return S_OK; }
};

static void PrintRun(const char* label, const std::wstring& text, IDWriteTextAnalyzer* analyzer, IDWriteFontFace* face)
{
    Source src{ text };
    Sink sink;
    if (FAILED(analyzer->AnalyzeBidi(&src, 0, (UINT32)text.size(), &sink)))
    {
        printf("  %-14s AnalyzeBidi failed\n", label);
        return;
    }

    std::vector<UINT8> charLevel(text.size(), 0);
    for (size_t r = 0; r < sink.pos.size(); r++)
    {
        UINT32 start = sink.pos[r];
        UINT32 end = (r + 1 < sink.pos.size()) ? sink.pos[r + 1] : (UINT32)text.size();
        for (UINT32 t = start; t < end; t++) charLevel[t] = sink.lvl[r];
    }

    UINT32 glyphCount = 0;
    std::vector<UINT16> glyphs(text.size() * 2 + 16, 0);
    std::vector<UINT16> clusterMap(text.size(), 0);
    std::vector<DWRITE_SHAPING_TEXT_PROPERTIES> textProps(text.size(), DWRITE_SHAPING_TEXT_PROPERTIES{});
    std::vector<DWRITE_SHAPING_GLYPH_PROPERTIES> glyphProps(text.size() * 2 + 16, DWRITE_SHAPING_GLYPH_PROPERTIES{});
    DWRITE_SCRIPT_ANALYSIS script{};
    script.script = 0x0600; // Arabic
    script.shapes = DWRITE_SCRIPT_SHAPES_DEFAULT;
    wchar_t locale[64] = L"fa-IR";
    HRESULT hr = analyzer->GetGlyphs(
        text.c_str(), (UINT32)text.size(), face, false, true, &script,
        locale, nullptr, nullptr, nullptr, 0,
        (UINT32)glyphs.size(), clusterMap.data(), textProps.data(),
        glyphs.data(), glyphProps.data(), &glyphCount);
    if (FAILED(hr))
    {
        printf("  %-14s GetGlyphs failed: 0x%08X\n", label, (unsigned)hr);
        return;
    }
    printf("  %-14s chars=%zu glyphs=%u", label, text.size(), glyphCount);

    // Print the actual glyph indices so we can see whether lam+alef collapsed.
    printf(" ids=");
    for (UINT32 g = 0; g < glyphCount && g < 8; g++) printf("%u ", glyphs[g]);

    if (glyphCount > 0)
    {
        std::vector<DWRITE_GLYPH_OFFSET> offsets(glyphCount, DWRITE_GLYPH_OFFSET{});
        std::vector<float> advances(glyphCount, 0.0f);
        // GetGlyphPlacements consumes the glyph properties GetGlyphs filled in;
        // a fresh zeroed buffer is not a valid substitution and the call rejects
        // it with E_INVALIDARG.
        hr = analyzer->GetGlyphPlacements(
            text.c_str(), clusterMap.data(), textProps.data(),
            (UINT32)text.size(), glyphs.data(), glyphProps.data(),
            (UINT32)glyphCount, face, 16.0f,
            false, true, &script, locale, nullptr, nullptr, 0,
            advances.data(), offsets.data());
        if (SUCCEEDED(hr))
        {
            printf("  per-glyph:");
            double total = 0.0;
            for (UINT32 g = 0; g < glyphCount; g++)
            {
                printf(" [id=%u adv=%.2f dx=%.2f dy=%.2f]", glyphs[g], advances[g], offsets[g].advanceOffset, offsets[g].ascenderOffset);
                total += advances[g];
            }
            printf("\n  total advance=%.2f", total);
        }
        else
        {
            printf("  GetGlyphPlacements FAILED: 0x%08X", (unsigned)hr);
        }
    }
    printf("\n");
}

int main(int argc, char** argv)
{
    if (argc < 2) { printf("usage: probe-bundled-font.exe <file.ttf>\n"); return 1; }

    std::wstring path;
    int wlen = MultiByteToWideChar(CP_UTF8, 0, argv[1], -1, nullptr, 0);
    path.resize(wlen);
    MultiByteToWideChar(CP_UTF8, 0, argv[1], -1, &path[0], wlen);

    ComPtr<IDWriteFactory> factory;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown**)&factory))) { printf("no DWrite\n"); return 1; }

    ComPtr<IDWriteFontFile> fontFile;
    HRESULT hr = factory->CreateFontFileReference(path.c_str(), nullptr, &fontFile);
    if (FAILED(hr)) { printf("CreateFontFileReference failed: 0x%08X\n", (unsigned)hr); return 1; }

    IDWriteFontFile* files[] = { fontFile.Get() };
    ComPtr<IDWriteFontFace> face;
    hr = factory->CreateFontFace(DWRITE_FONT_FACE_TYPE_TRUETYPE, 1, files, 0, DWRITE_FONT_SIMULATIONS_NONE, &face);
    if (FAILED(hr)) { printf("CreateFontFace failed: 0x%08X\n", (unsigned)hr); return 1; }

    printf("font: %s\n", argv[1]);

    // Probe a few Persian/Arabic codepoints through the cmap directly.
    const UINT32 probes[] = { 0x0627, 0x0623, 0x0628, 0x067E, 0x062A, 0x062B,
                              0x062C, 0x0686, 0x062D, 0x062E, 0x062F, 0x0630,
                              0x0631, 0x0632, 0x0698, 0x0633, 0x0634, 0x0635,
                              0x0636, 0x0637, 0x0638, 0x0639, 0x063A, 0x0641,
                              0x0642, 0x06A9, 0x06AF, 0x0644, 0x0645, 0x0646,
                              0x0648, 0x0647, 0x0649, 0x06CC, 0x0621, 0x061F,
                              0x06F0, 0x06F1, 0x06F2, 0x06F3, 0x06F4, 0x06F5,
                              0x06F6, 0x06F7, 0x06F8, 0x06F9 };
    std::vector<UINT16> probeGlyphs(_countof(probes), 0);
    hr = face->GetGlyphIndices(probes, (UINT32)probeGlyphs.size(), probeGlyphs.data());
    int present = 0;
    if (SUCCEEDED(hr))
    {
        for (size_t i = 0; i < probeGlyphs.size(); i++)
        {
            if (probeGlyphs[i] != 0) ++present;
        }
    }
    printf("cmap coverage of 45 core Persian/Arabic letters: %d present\n", present);

    ComPtr<IDWriteTextAnalyzer> analyzer;
    factory->CreateTextAnalyzer(&analyzer);

    printf("\nshaping (fa-IR, RTL paragraph):\n");
    PrintRun("lam-alef", L"\u0644\u0627", analyzer.Get(), face.Get());              // لا
    PrintRun("lam-alef-hamza", L"\u0644\u0623", analyzer.Get(), face.Get());        // لأ
    PrintRun("salam", L"\u0633\u0644\u0627\u0645", analyzer.Get(), face.Get());     // سلام
    PrintRun("khoobi?", L"\u062E\u0648\u0628\u06CC\u061F", analyzer.Get(), face.Get()); // خوبی؟

    // Positional-shaping test. GSUB init/medi/fina substitution is what makes
    // Arabic letters connect. "lam" standing alone should take the isolated
    // form, the same letter inside "salam" should take something else. If the
    // glyph ids are identical the font's shaping features are not being applied
    // and the text would render disconnected.
    printf("\npositional shaping (identical ids across runs = not shaping):\n");
    PrintRun("lam alone", L"\u0644", analyzer.Get(), face.Get());                   // ل
    PrintRun("lam in salam", L"\u0633\u0644\u0627\u0645", analyzer.Get(), face.Get()); // سلام
    PrintRun("alef alone", L"\u0627", analyzer.Get(), face.Get());                   // ا
    return 0;
}
