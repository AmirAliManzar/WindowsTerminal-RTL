// Dumps the OpenType GSUB ScriptList / FeatureList / LookupList summary so we can
// see whether a font declares the Arabic shaping features at all:
//   ccmp  (glyph composition), rlig (required ligatures), liga (standard ligatures),
//   init/medi/fina/isol (positional forms).
//
//   cl /nologo /EHsc /std:c++20 gsub-dump.cpp /Fe:gsub-dump.exe
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <cstdio>
#include <cstdint>
#include <vector>
#include <string>

struct R
{
    const uint8_t* b;
    size_t n;
    uint16_t u16(size_t o) const { return (uint16_t)((b[o] << 8) | b[o + 1]); }
    uint32_t u32(size_t o) const { return ((uint32_t)b[o] << 24) | ((uint32_t)b[o + 1] << 16) | ((uint32_t)b[o + 2] << 8) | b[o + 3]; }
    uint16_t u16be(size_t base, size_t o) const { return u16(base + o); }
};

static std::string Tag(const uint8_t* p)
{
    return std::string((const char*)p, 4);
}

int main(int argc, char** argv)
{
    if (argc < 2) { printf("usage: gsub-dump.exe <file.ttf>\n"); return 1; }
    HANDLE h = CreateFileA(argv[1], GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) { printf("cannot open %s\n", argv[1]); return 1; }
    LARGE_INTEGER sz; GetFileSizeEx(h, &sz);
    std::vector<uint8_t> buf((size_t)sz.QuadPart);
    DWORD got; ReadFile(h, buf.data(), (DWORD)buf.size(), &got, nullptr);
    CloseHandle(h);
    R r{ buf.data(), buf.size() };

    uint16_t numTables = r.u16(4);
    uint32_t gsubOff = 0, gsubLen = 0;
    for (uint16_t i = 0; i < numTables; i++)
    {
        size_t o = 12 + (size_t)i * 16;
        std::string tag = Tag(&buf[o]);
        if (tag == "GSUB") { gsubOff = r.u32(o + 8); gsubLen = r.u32(o + 12); }
    }
    if (gsubOff == 0) { printf("%s: no GSUB table\n", argv[1]); return 0; }
    printf("%s: GSUB at %u, length %u\n", argv[1], gsubOff, gsubLen);

    const uint8_t* g = buf.data() + gsubOff;
    R gr{ g, gsubLen };
    uint16_t major = gr.u16(0), minor = gr.u16(2);
    uint16_t scriptListOff = gr.u16(4);
    uint16_t featureListOff = gr.u16(6);
    uint16_t lookupListOff = gr.u16(8);
    printf("  version %u.%u  scripts@%u  features@%u  lookups@%u\n", major, minor, scriptListOff, featureListOff, lookupListOff);

    // Script list
    uint16_t scriptCount = gr.u16(scriptListOff);
    printf("  scripts (%u):", scriptCount);
    for (uint16_t i = 0; i < scriptCount; i++)
    {
        size_t e = scriptListOff + 2 + (size_t)i * 6;
        printf(" %s", Tag(&g[e]).c_str());
    }
    printf("\n");

    // Feature list
    uint16_t featCount = gr.u16(featureListOff);
    printf("  features (%u):", featCount);
    for (uint16_t i = 0; i < featCount; i++)
    {
        size_t e = featureListOff + 2 + (size_t)i * 6;
        printf(" %s", Tag(&g[e]).c_str());
    }
    printf("\n");

    uint16_t lookCount = gr.u16(lookupListOff);
    printf("  lookups: %u\n", lookCount);

    // For the 'arab' script, find its default lang sys and count lookups referenced.
    for (uint16_t i = 0; i < scriptCount; i++)
    {
        size_t e = scriptListOff + 2 + (size_t)i * 6;
        std::string tag = Tag(&g[e]);
        uint16_t off = gr.u16(e + 4);
        if (tag != "arab") continue;
        size_t so = scriptListOff + off;
        uint16_t defOff = gr.u16(so);
        uint16_t langCount = gr.u16(so + 2);
        printf("  'arab' defaultLangSys@%u, %u langs\n", defOff, langCount);
        size_t ls = so + defOff;
        uint16_t lookupOrder = gr.u16(ls);
        uint16_t reqFeatIndex = gr.u16(ls + 2);
        uint16_t featIdxCount = gr.u16(ls + 4);
        printf("    reqFeatureIndex=%u, featureIndices(%u):", reqFeatIndex, featIdxCount);
        for (uint16_t k = 0; k < featIdxCount; k++) printf(" %u", gr.u16(ls + 6 + 2 * k));
        printf("\n");
        if (lookupOrder) printf("    lookupOrder at %u\n", lookupOrder);
    }
    return 0;
}
