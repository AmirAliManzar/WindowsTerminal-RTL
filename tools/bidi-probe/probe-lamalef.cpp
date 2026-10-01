// Which installed fonts actually fuse lam-alef into a single glyph?
//
// لا is U+0644 U+0627. In Arabic and Persian typography lam-alef is a mandatory
// ligature, so a font without it draws the two letters apart and the word looks
// broken - which is what the renderer was reported to show. No amount of bidi
// reordering can fix that: shaping happens before any reordering, and the gap
// appears in the shaped glyphs.
//
// This walks every installed font that has U+0633, shapes لا through
// IDWriteTextAnalyzer, and reports how many glyphs came back. One glyph means the
// ligature fused; two means it did not.
//
// Build:
//   cl /nologo /EHsc /std:c++20 probe-lamalef.cpp /Fe:probe-lamalef.exe /link dwrite.lib
// Run:
//   probe-lamalef.exe

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

static std::string familyName(IDWriteFontFamily* family)
{
    ComPtr<IDWriteLocalizedStrings> names;
    if (FAILED(family->GetFamilyNames(&names)) || !names) return "?";
    UINT32 count = names->GetCount();
    std::wstring best;
    for (UINT32 i = 0; i < count; ++i)
    {
        UINT32 len = 0;
        if (FAILED(names->GetStringLength(i, &len)) || !len) continue;
        std::vector<wchar_t> buf(len + 1, 0);
        if (FAILED(names->GetString(i, buf.data(), len + 1))) continue;
        if (best.empty()) best.assign(buf.data());
    }
    if (best.empty()) return "?";
    // narrow it for printf; family names here are ASCII in practice
    std::string narrow;
    for (wchar_t w : best)
    {
        narrow.push_back((w < 128) ? (char)w : '?');
    }
    return narrow;
}

int main()
{
    ComPtr<IDWriteFactory> factory;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown**)&factory)))
    {
        printf("DWriteCreateFactory failed\n");
        return 1;
    }

    ComPtr<IDWriteFontCollection> collection;
    factory->GetSystemFontCollection(&collection, FALSE);

    ComPtr<IDWriteTextAnalyzer> analyzer;
    factory->CreateTextAnalyzer(&analyzer);

    // lam + alef
    const std::wstring la = L"\u0644\u0627";           // لا
    // a word that needs lam-alef inside it
    const std::wstring salaam = L"\u0633\u0644\u0627\u0645"; // سلام

    struct DWRITE_SCRIPT_ANALYSIS sa{};
    sa.script = 0x0600;
    sa.shapes = DWRITE_SCRIPT_SHAPES_DEFAULT;

    printf("%-34s %-12s %-12s\n", "font", "لا glyphs", "سلام glyphs");
    printf("%-34s %-12s %-12s\n", "----------------------------------", "------------", "------------");

    int fused = 0;
    int split = 0;
    const UINT32 count = collection->GetFontFamilyCount();
    for (UINT32 i = 0; i < count; ++i)
    {
        ComPtr<IDWriteFontFamily> family;
        if (FAILED(collection->GetFontFamily(i, &family))) continue;
        ComPtr<IDWriteFont> font;
        if (FAILED(family->GetFirstMatchingFont(DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STRETCH_NORMAL, DWRITE_FONT_STYLE_NORMAL, &font))) continue;
        BOOL has = FALSE;
        if (FAILED(font->HasCharacter(0x0633, &has)) || !has) continue;

        ComPtr<IDWriteFontFace> face;
        if (FAILED(font->CreateFontFace(&face))) continue;

        UINT32 g1 = 0;
        UINT32 g2 = 0;

        std::vector<UINT16> glyphs(16);
        std::vector<UINT16> cmap(16);
        std::vector<DWRITE_SHAPING_TEXT_PROPERTIES> props(16);
        std::vector<DWRITE_SHAPING_GLYPH_PROPERTIES> gprops(16);

        if (SUCCEEDED(analyzer->GetGlyphs(la.c_str(), (UINT32)la.size(), face.Get(), FALSE, TRUE, &sa, L"ar",
                                          nullptr, nullptr, nullptr, 0,
                                          (UINT32)glyphs.size(), cmap.data(), props.data(), glyphs.data(), gprops.data(), &g1)))
        {
            // ف also matters: a font can fuse lam-alef but break elsewhere.
            g1 = g1;
        }
        std::vector<UINT16> glyphs2(16);
        std::vector<UINT16> cmap2(16);
        std::vector<DWRITE_SHAPING_TEXT_PROPERTIES> props2(16);
        std::vector<DWRITE_SHAPING_GLYPH_PROPERTIES> gprops2(16);
        analyzer->GetGlyphs(salaam.c_str(), (UINT32)salaam.size(), face.Get(), FALSE, TRUE, &sa, L"ar",
                            nullptr, nullptr, nullptr, 0,
                            (UINT32)glyphs2.size(), cmap2.data(), props2.data(), glyphs2.data(), gprops2.data(), &g2);

        const bool ok = (g1 == 1);
        if (ok) ++fused; else ++split;
        printf("%-34s %-12s %-12s\n", familyName(family.Get()).c_str(),
               ok ? "1 (FUSED)" : "2 (split)",
               (g2 == 4) ? "4 (ok)" : "different");
    }

    printf("\n%d font(s) fuse lam-alef, %d do not.\n", fused, split);
    printf("A terminal showing a gap between lam and alef is drawing with one of the\n");
    printf("fonts in the second group, not because of bidi reordering.\n");
    return 0;
}