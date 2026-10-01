// Does IDWriteTextAnalyzer reverse the glyph order for Arabic when asked for
// left-to-right?
//
// The renderer's output makes this worth pinning down. With bidi rule L2 switched
// off - so the cluster sequence is left exactly as DirectWrite produced it - an
// Arabic run still came out character by character reversed: سلام rendered as مالس
// and لا as ال. _mapComplex passes isRightToLeft straight from the resolved bidi
// level, so "typed order" mode asks for FALSE. If DirectWrite reverses anyway, the
// reversal is coming from the shaper and no amount of reordering in the renderer
// can undo it.
//
// Build:
//   cl /nologo /EHsc /std:c++20 probe-glyph-direction.cpp /Fe:probe-glyph-direction.exe /link dwrite.lib
// Run:
//   probe-glyph-direction.exe

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
    DWRITE_READING_DIRECTION __stdcall GetParagraphReadingDirection() noexcept override
    {
        return DWRITE_READING_DIRECTION_LEFT_TO_RIGHT;
    }
    HRESULT __stdcall GetLocaleName(UINT32, UINT32* len, const WCHAR** name) noexcept override
    {
        *len = 5;
        static const WCHAR en[] = L"en-US";
        *name = en;
        return S_OK;
    }
    HRESULT __stdcall GetNumberSubstitution(UINT32, UINT32*, IDWriteNumberSubstitution**) noexcept override
    {
        return E_NOTIMPL;
    }
};

static void dump(IDWriteTextAnalyzer* analyzer,
                 IDWriteFontFace* face,
                 const std::wstring& label,
                 const std::wstring& text,
                 BOOL rtl)
{
    DWRITE_SCRIPT_ANALYSIS sa{};
    sa.script = 0x0600; // Arabic
    sa.shapes = DWRITE_SCRIPT_SHAPES_DEFAULT;

    const UINT32 cap = 64;
    std::vector<UINT16> glyphs(cap);
    std::vector<UINT16> cmap(cap + 1);
    std::vector<DWRITE_SHAPING_TEXT_PROPERTIES> props(cap);
    std::vector<DWRITE_SHAPING_GLYPH_PROPERTIES> gprops(cap);
    UINT32 n = 0;

    HRESULT hr = analyzer->GetGlyphs(text.c_str(), (UINT32)text.size(), face, FALSE, rtl, &sa, L"ar",
                                     nullptr, nullptr, nullptr, 0,
                                     cap, cmap.data(), props.data(), glyphs.data(), gprops.data(), &n);
    printf("%-14s isRightToLeft=%-3s glyphCount=%u\n", label.c_str(), rtl ? "TRUE" : "FALSE", n);
    if (FAILED(hr)) { printf("    GetGlyphs failed 0x%08X\n", (unsigned)hr); return; }

    // Which logical character does each glyph come from?
    std::wstring order;
    for (UINT32 g = 0; g < n; ++g)
    {
        UINT32 owner = (UINT32)text.size();
        for (UINT32 t = 0; t < text.size(); ++t)
        {
            if (cmap[t] <= g && g < cmap[t + 1]) { owner = t; break; }
        }
        order.push_back(owner < text.size() ? text[owner] : L'?');
    }
    printf("    glyph order as characters: ");
    for (wchar_t w : order)
    {
        if (w == L' ') printf("SP ");
        else if (w < 128) printf("%c ", (char)w);
        else printf("U+%04X ", (unsigned)w);
    }
    printf("\n");

    // And where do the placements put them?
    std::vector<float> adv(n);
    std::vector<DWRITE_GLYPH_OFFSET> off(n);
    if (SUCCEEDED(analyzer->GetGlyphPlacements(text.c_str(), cmap.data(), props.data(), (UINT32)text.size(),
                                               glyphs.data(), gprops.data(), n, face, 12.0f, FALSE, rtl, &sa, L"ar",
                                               nullptr, nullptr, 0, adv.data(), off.data())))
    {
        
    }
    printf("\n");
}

int wmain()
{
    ComPtr<IDWriteFactory> factory;
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown**)&factory);
    ComPtr<IDWriteFontCollection> collection;
    factory->GetSystemFontCollection(&collection, FALSE);
    ComPtr<IDWriteTextAnalyzer> analyzer;
    factory->CreateTextAnalyzer(&analyzer);

    ComPtr<IDWriteFontFace> face;
    for (UINT32 i = 0; i < collection->GetFontFamilyCount() && !face; ++i)
    {
        ComPtr<IDWriteFontFamily> family;
        if (FAILED(collection->GetFontFamily(i, &family))) continue;
        ComPtr<IDWriteFont> font;
        if (FAILED(family->GetFirstMatchingFont(DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STRETCH_NORMAL, DWRITE_FONT_STYLE_NORMAL, &font))) continue;
        BOOL has = FALSE;
        if (FAILED(font->HasCharacter(0x0633, &has)) || !has) continue;
        font->CreateFontFace(&face);
    }
    if (!face) { printf("no Arabic-capable font\n"); return 1; }

    printf("Arabic, asked both ways. If the glyph order is the same in both, the\n");
    printf("shaper always returns logical order and the renderer owns the reversal.\n");
    printf("If FALSE comes back reversed, the shaper is doing it.\n\n");

    dump(analyzer.Get(), face.Get(), L"salaam", L"\u0633\u0644\u0627\u0645", TRUE);
    dump(analyzer.Get(), face.Get(), L"salaam", L"\u0633\u0644\u0627\u0645", FALSE);
    dump(analyzer.Get(), face.Get(), L"laa", L"\u0644\u0627", TRUE);
    dump(analyzer.Get(), face.Get(), L"laa", L"\u0644\u0627", FALSE);
    return 0;
}