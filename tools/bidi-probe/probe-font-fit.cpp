// Which installed font will actually look right in a monospace terminal?
//
// Two things decide that, and both are measurable rather than a matter of taste:
//
//   1. lam-alef fusion. Lam-alef is a mandatory ligature in Arabic script, so a
//      font without it draws the two letters apart.
//   2. Glyph advance against the cell width. A terminal grid has one cell width,
//      taken from the primary font, and _mapComplex forces every shaped cluster to
//      exactly one cell:
//
//          _api.glyphAdvances[nextCluster - 1] += expectedAdvance - actualAdvance;
//
//      So an Arabic font whose glyphs are narrower than the cell does not get drawn
//      narrow - each letter is padded out to a full cell, and that is precisely what
//      reads as a loose gap between two letters. The ratio is the number to look at.
//
// Advances come from IDWriteTextAnalyzer::GetGlyphPlacements rather than from
// IDWriteFontFace::GetGlyphMetrics: the renderer gets them that way too, and
// GetGlyphMetrics is not declared by the dwrite.h both this machine and the
// Actions runner resolve.
//
// Build:
//   cl /nologo /EHsc /std:c++20 probe-font-fit.cpp /Fe:probe-font-fit.exe /link dwrite.lib
// Run:
//   probe-font-fit.exe [primary-font-name]      default: Cascadia Mono

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

struct Shaped
{
    UINT32 glyphCount = 0;
    float totalAdvance = 0.0f;
};

static Shaped shape(IDWriteTextAnalyzer* analyzer,
                    IDWriteFontFace* face,
                    const std::wstring& text,
                    bool rtl)
{
    Shaped out;
    DWRITE_SCRIPT_ANALYSIS sa{};
    sa.script = rtl ? 0x0600 : 0x0400;
    sa.shapes = DWRITE_SCRIPT_SHAPES_DEFAULT;

    const UINT32 cap = 128;
    std::vector<UINT16> glyphs(cap);
    std::vector<UINT16> cmap(cap + 1);
    std::vector<DWRITE_SHAPING_TEXT_PROPERTIES> props(cap);
    std::vector<DWRITE_SHAPING_GLYPH_PROPERTIES> gprops(cap);
    UINT32 n = 0;

    HRESULT hr = analyzer->GetGlyphs(text.c_str(), (UINT32)text.size(), face, FALSE, rtl ? TRUE : FALSE,
                                     &sa, rtl ? L"ar" : L"en-US", nullptr, nullptr, nullptr, 0,
                                     cap, cmap.data(), props.data(), glyphs.data(), gprops.data(), &n);
    if (FAILED(hr)) return out;
    out.glyphCount = n;

    std::vector<float> advances(n);
    std::vector<DWRITE_GLYPH_OFFSET> offsets(n);
    hr = analyzer->GetGlyphPlacements(text.c_str(), cmap.data(), props.data(), (UINT32)text.size(),
                                      glyphs.data(), gprops.data(), n, face,
                                      12.0f /* em size; only ratios matter here */,
                                      FALSE, rtl ? TRUE : FALSE, &sa, rtl ? L"ar" : L"en-US",
                                      nullptr, nullptr, 0,
                                      advances.data(), offsets.data());
    if (FAILED(hr)) return out;

    for (float a : advances) { out.totalAdvance += a; }
    return out;
}

static std::string narrow(const std::wstring& w)
{
    std::string s;
    for (wchar_t c : w) { s.push_back((c < 128) ? (char)c : '?'); }
    return s;
}

static std::wstring familyName(IDWriteFontFamily* family)
{
    ComPtr<IDWriteLocalizedStrings> names;
    if (FAILED(family->GetFamilyNames(&names)) || !names) return L"?";
    const UINT32 count = names->GetCount();
    for (UINT32 i = 0; i < count; ++i)
    {
        UINT32 len = 0;
        if (FAILED(names->GetStringLength(i, &len)) || !len) continue;
        std::vector<wchar_t> buf(len + 1, 0);
        if (FAILED(names->GetString(i, buf.data(), len + 1))) continue;
        return std::wstring(buf.data());
    }
    return L"?";
}

int wmain(int argc, wchar_t** argv)
{
    const std::wstring primary = (argc > 1) ? argv[1] : L"Cascadia Mono";

    ComPtr<IDWriteFactory> factory;
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown**)&factory);
    ComPtr<IDWriteFontCollection> collection;
    factory->GetSystemFontCollection(&collection, FALSE);
    ComPtr<IDWriteTextAnalyzer> analyzer;
    factory->CreateTextAnalyzer(&analyzer);

    // The cell width the terminal uses, measured through the same placement call
    // the renderer makes: the mean advance of the ASCII letters.
    const std::wstring ascii = L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    float cellWidth = 0.0f;

    for (UINT32 i = 0; i < collection->GetFontFamilyCount() && cellWidth == 0.0f; ++i)
    {
        ComPtr<IDWriteFontFamily> family;
        if (FAILED(collection->GetFontFamily(i, &family))) continue;
        if (familyName(family.Get()) != primary) continue;
        ComPtr<IDWriteFont> font;
        if (FAILED(family->GetFirstMatchingFont(DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STRETCH_NORMAL, DWRITE_FONT_STYLE_NORMAL, &font))) continue;
        ComPtr<IDWriteFontFace> face;
        if (FAILED(font->CreateFontFace(&face))) continue;
        const Shaped s = shape(analyzer.Get(), face.Get(), ascii, false);
        cellWidth = s.totalAdvance / (float)ascii.size();
    }

    printf("primary font : %ls\n", primary.c_str());
    printf("cell width   : %.3f px at 12px em (mean of 52 ASCII letters)\n\n", cellWidth);
    if (cellWidth <= 0.0f) { printf("could not measure the primary font; is it installed?\n"); return 1; }

    const std::wstring la = L"\u0644\u0627";               // لا
    const std::wstring word = L"\u0633\u0644\u0627\u0645"; // سلام

    printf("%-26s %-11s %-14s %s\n", "font", "لا glyphs", "advance/char", "vs cell");
    printf("%-26s %-11s %-14s %s\n", "--------------------------", "-----------", "--------------", "----------------");

    for (UINT32 i = 0; i < collection->GetFontFamilyCount(); ++i)
    {
        ComPtr<IDWriteFontFamily> family;
        if (FAILED(collection->GetFontFamily(i, &family))) continue;
        ComPtr<IDWriteFont> font;
        if (FAILED(family->GetFirstMatchingFont(DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STRETCH_NORMAL, DWRITE_FONT_STYLE_NORMAL, &font))) continue;
        BOOL has = FALSE;
        if (FAILED(font->HasCharacter(0x0633, &has)) || !has) continue;
        ComPtr<IDWriteFontFace> face;
        if (FAILED(font->CreateFontFace(&face))) continue;

        const Shaped fusion = shape(analyzer.Get(), face.Get(), la, true);
        const Shaped s = shape(analyzer.Get(), face.Get(), word, true);
        if (!s.glyphCount) continue;

        const float perChar = s.totalAdvance / (float)s.glyphCount;
        char ratio[24];
        snprintf(ratio, sizeof(ratio), "%.2fx", perChar / cellWidth);

        printf("%-26s %-11s %-14.3f %s\n",
               narrow(familyName(family.Get())).c_str(),
               (fusion.glyphCount == 1) ? "1 FUSED" : "2 split",
               perChar,
               ratio);
    }

    printf("\nvs cell near 1.00 means the Arabic glyphs are as wide as a cell, so the\n");
    printf("grid does not have to stretch them and the letters sit tight. Far below\n");
    printf("1.00 means every letter is padded out to a full cell, which is what reads\n");
    printf("as a gap between lam and alef.\n");
    return 0;
}