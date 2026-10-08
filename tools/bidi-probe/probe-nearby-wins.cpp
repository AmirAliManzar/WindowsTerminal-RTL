// The critical question for font self-sufficiency.
//
// Two different files on this machine are both called "Cascadia Code":
//   C:\WINDOWS\Fonts\CascadiaCode.ttf     388 KB, cmap lam = 0   (no Arabic)
//   <install>\CascadiaCode.ttf            736 KB, cmap lam = 1270 (Arabic)
//
// The renderer builds a font set with the nearby .ttf files added first and
// the system set added last, on the theory that IDWriteFontSetBuilder keeps the
// first family it sees. This probe reproduces exactly that construction and
// asks the resulting set for "Cascadia Code", then reports which glyph the
// resolved face maps lam to. If the answer is 0, the nearby fonts are NOT
// winning and Persian text would render as boxes whenever the family name in
// settings.json collides with an installed system font.
//
//   cl /nologo /EHsc /std:c++20 probe-nearby-wins.cpp /Fe:probe-nearby-wins.exe /link dwrite.lib ole32.lib
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <dwrite_3.h>
#include <wrl/client.h>
#include <cstdio>
#include <string>

using Microsoft::WRL::ComPtr;

int main(int argc, char** argv)
{
    if (argc < 2) { printf("usage: probe-nearby-wins.exe <font.ttf> [<font2.ttf> ...]\n"); return 1; }

    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ComPtr<IDWriteFactory5> factory;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory5), (IUnknown**)&factory)))
    { printf("needs IDWriteFactory5 (Win10 15021+)\n"); return 1; }

    // Build the font set the way FontCache.h does: nearby first, system last.
    ComPtr<IDWriteFontSetBuilder1> builder;
    if (FAILED(factory->CreateFontSetBuilder(&builder))) { printf("no set builder\n"); return 1; }

    // Nearby .ttf files first, passed on the command line.
    int nearby = 0;
    for (int i = 1; i < argc; i++)
    {
        std::wstring path;
        int n = MultiByteToWideChar(CP_UTF8, 0, argv[i], -1, nullptr, 0);
        path.resize(n); MultiByteToWideChar(CP_UTF8, 0, argv[i], -1, &path[0], n);

        ComPtr<IDWriteFontFile> fontFile;
        HRESULT ref = factory->CreateFontFileReference(path.c_str(), nullptr, &fontFile);
        if (FAILED(ref)) { printf("CreateFontFileReference failed 0x%08X: %s\n", (unsigned)ref, argv[i]); continue; }
        HRESULT hr = builder->AddFontFile(fontFile.Get());
        if (SUCCEEDED(hr)) { printf("nearby added: %s\n", argv[i]); nearby++; }
        else printf("AddFontFile failed 0x%08X: %s\n", (unsigned)hr, argv[i]);
    }
    if (nearby == 0) { printf("no nearby fonts added\n"); return 1; }

    // The system set last, exactly as FontCache.h does it.
    ComPtr<IDWriteFontSet> systemSet;
    if (FAILED(factory->GetSystemFontSet(&systemSet))) { printf("no system set\n"); return 1; }
    HRESULT hr = builder->AddFontSet(systemSet.Get());
    printf("AddFontSet(system): 0x%08X\n", (unsigned)hr);

    ComPtr<IDWriteFontSet> fontSet;
    if (FAILED(builder->CreateFontSet(&fontSet))) { printf("could not create set\n"); return 1; }
    printf("combined set has %u fonts\n\n", (unsigned)fontSet->GetFontCount());

    // Turn it into a collection the way FontCache.h does, then look the family
    // up by name, which is how the renderer resolves settings.json.
    ComPtr<IDWriteFontCollection1> collection;
    if (FAILED(factory->CreateFontCollectionFromFontSet(fontSet.Get(), &collection)))
    { printf("could not build collection\n"); return 1; }

    const wchar_t* family = L"Cascadia Code";
    UINT32 famIdx = 0; BOOL famExists = FALSE;
    hr = collection->FindFamilyName(family, &famIdx, &famExists);
    printf("FindFamilyName(L\"%S\"): 0x%08X exists=%d\n", family, (unsigned)hr, (int)famExists);
    if (!famExists)
    {
        printf("  -> the family is not in the combined set at all\n");
        return 0;
    }

    ComPtr<IDWriteFontFamily> fam;
    collection->GetFontFamily(famIdx, &fam);
    ComPtr<IDWriteFont> font;
    fam->GetFirstMatchingFont(DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STRETCH_NORMAL, DWRITE_FONT_STYLE_NORMAL, &font);
    ComPtr<IDWriteFontFace> face;
    font->CreateFontFace(&face);

    UINT32 lam = 0x0644, alef = 0x0627, seen = 0x0633;
    UINT16 lamG = 0, alefG = 0, seenG = 0;
    face->GetGlyphIndices(&lam, 1, &lamG);
    face->GetGlyphIndices(&alef, 1, &alefG);
    face->GetGlyphIndices(&seen, 1, &seenG);

    printf("\nresolved face: lam=%u alef=%u seen=%u\n", lamG, alefG, seenG);
    if (lamG != 0 && seenG != 0)
        printf("RESULT: nearby font WINS - Persian/Arabic coverage is present\n");
    else if (lamG == 0 && seenG == 0)
        printf("RESULT: SYSTEM font WINS - Persian would render as .notdef boxes\n");
    else
        printf("RESULT: mixed - partial coverage\n");
    return 0;
}
