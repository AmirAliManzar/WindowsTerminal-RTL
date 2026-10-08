// Dumps the largest RT_ICON resource from a PE file to a .png so it can be
// inspected as text art.
//   cl /nologo /EHsc /std:c++20 extract-icon.cpp /Fe:extract-icon.exe /link dwrite.lib
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <cstdio>
#include <vector>
#include <string>

using Microsoft::WRL::ComPtr;

static std::vector<unsigned char> ReadFile(const wchar_t* path)
{
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return {};
    LARGE_INTEGER sz; GetFileSizeEx(h, &sz);
    std::vector<unsigned char> b((size_t)sz.QuadPart);
    DWORD got = 0; ReadFile(h, b.data(), (DWORD)b.size(), &got, nullptr);
    CloseHandle(h);
    return b;
}

int main(int argc, char** argv)
{
    if (argc < 3) { printf("usage: extract-icon.exe <exe> <out.png>\n"); return 1; }
    std::wstring exe, out;
    int n = MultiByteToWideChar(CP_UTF8, 0, argv[1], -1, nullptr, 0);
    exe.resize(n); MultiByteToWideChar(CP_UTF8, 0, argv[1], -1, &exe[0], n);
    n = MultiByteToWideChar(CP_UTF8, 0, argv[2], -1, nullptr, 0);
    out.resize(n); MultiByteToWideChar(CP_UTF8, 0, argv[2], -1, &out[0], n);

    HMODULE mod = LoadLibraryExW(exe.c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE);
    if (!mod) { printf("cannot load %S\n", exe.c_str()); return 1; }

    // RT_ICON resources are named "1", "2", ... under RT_ICON. Find the biggest.
    struct Ctx { int best; DWORD bestSize; };
    Ctx ctx{ -1, 0 };
    EnumResourceNamesW(mod, MAKEINTRESOURCEW((ULONG_PTR)RT_ICON), [](HMODULE m, LPCWSTR type, LPWSTR name, LONG_PTR param) -> BOOL {
        Ctx* c = (Ctx*)param;
        HRSRC r = FindResourceW(m, name, type);
        if (!r) return TRUE;
        DWORD sz = SizeofResource(m, r);
        if (sz > c->bestSize) { c->bestSize = sz; c->best = (int)(LONG_PTR)name; }
        return TRUE;
    }, (LONG_PTR)&ctx);

    if (ctx.best < 0) { printf("no RT_ICON\n"); FreeLibrary(mod); return 1; }

    HRSRC r = FindResourceW(mod, MAKEINTRESOURCEW(ctx.best), MAKEINTRESOURCEW((ULONG_PTR)RT_ICON));
    HGLOBAL g = LoadResource(mod, r);
    DWORD sz = SizeofResource(mod, r);
    void* p = LockResource(g);

    // RT_ICON data is a raw icon image: either a BITMAPINFOHEADER DIB or a PNG.
    const unsigned char* d = (const unsigned char*)p;
    bool isPng = (sz >= 8 && d[0] == 0x89 && d[1] == 0x50 && d[2] == 0x4E && d[3] == 0x47);
    printf("best RT_ICON id=%d size=%u %s\n", ctx.best, sz, isPng ? "(PNG)" : "(DIB)");

    if (isPng)
    {
        HANDLE hf = CreateFileW(out.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
        DWORD w; WriteFile(hf, d, sz, &w, nullptr);
        CloseHandle(hf);
        printf("wrote %S\n", out.c_str());
    }
    else
    {
        // DIB: decode to PNG with WIC.
        // WICImagingFactory CLSID differs between SDK versions; resolve it from the
        // CLSID_WICImagingFactory define that wincodec.h exposes.
        ComPtr<IWICImagingFactory> wic;
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        CLSID factoryClsid = CLSID_WICImagingFactory;
        IWICImagingFactory* rawWic = nullptr;
        HRESULT hr = CoCreateInstance(factoryClsid, nullptr, CLSCTX_INPROC_SERVER, __uuidof(IWICImagingFactory), (void**)&rawWic);
        if (FAILED(hr) || !rawWic) { printf("no WIC (0x%08X)\n", (unsigned)hr); return 1; }
        wic = rawWic;
        ComPtr<IWICBitmapDecoder> dec;
        // Wrap the DIB in an ICO container so WIC's ICO decoder can read it.
        std::vector<unsigned char> ico;
        ico.resize(6 + 16 + sz);
        ico[0] = 0; ico[1] = 0; ico[2] = 1; ico[3] = 0; ico[4] = 1; ico[5] = 0;
        size_t e = 6;
        BITMAPINFOHEADER bih{};
        CopyMemory(&bih, d, sizeof(bih));
        ico[e + 0] = (unsigned char)(bih.biWidth & 0xFF);
        ico[e + 1] = 0;
        ico[e + 2] = (unsigned char)((bih.biHeight / 2) & 0xFF);
        ico[e + 3] = 0;
        ico[e + 4] = 1; ico[e + 5] = 0;
        ico[e + 6] = (unsigned char)bih.biBitCount; ico[e + 7] = (unsigned char)(bih.biBitCount >> 8);
        ico[e + 8] = (unsigned char)((sz & 0xFF));
        ico[e + 9] = (unsigned char)((sz >> 8) & 0xFF);
        ico[e + 10] = (unsigned char)((sz >> 16) & 0xFF);
        ico[e + 11] = (unsigned char)((sz >> 24) & 0xFF);
        ico[e + 12] = 22; // offset 6+16
        CopyMemory(&ico[22], d, sz);

        ComPtr<IWICStream> stream;
        wic->CreateStream(&stream);
        stream->InitializeFromMemory(ico.data(), (DWORD)ico.size());
        hr = wic->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, &dec);
        if (FAILED(hr)) { printf("decode failed 0x%08X\n", (unsigned)hr); return 1; }
        ComPtr<IWICBitmapFrameDecode> frame;
        dec->GetFrame(0, &frame);
        ComPtr<IWICBitmapEncoder> enc;
        wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc);
        ComPtr<IWICStream> outStream;
        wic->CreateStream(&outStream);
        outStream->InitializeFromFilename(out.c_str(), GENERIC_WRITE);
        enc->Initialize(outStream.Get(), WICBitmapEncoderNoCache);
        ComPtr<IWICBitmapFrameEncode> fe;
        enc->CreateNewFrame(&fe, nullptr);
        fe->Initialize(nullptr);
        fe->WriteSource(frame.Get(), nullptr);
        fe->Commit();
        enc->Commit();
        printf("wrote %S\n", out.c_str());
    }

    FreeLibrary(mod);
    return 0;
}
