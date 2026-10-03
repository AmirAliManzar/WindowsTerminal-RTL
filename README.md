# Windows Terminal RTL

A patch that fixes right-to-left text rendering in [Windows Terminal](https://github.com/microsoft/terminal).

On Windows, Windows Terminal has problems rendering Persian, Arabic and Hebrew. Words appear in the wrong order, letters can come apart, and the caret can land in the wrong cell.

This patch fixes the problem at the text-shaping layer, in the Atlas renderer, so it applies everywhere Windows Terminal draws text: the tab bar, the settings UI, and anything running inside the terminal - shells, `claude code`, `opencode`, editors over SSH.

## Downloads

Prebuilt files are on the [Releases](../../releases) page:

- `WindowsTerminal-RTL-x64.zip` portable build for 64-bit Intel/AMD
- `WindowsTerminal-RTL-arm64.zip` portable build for Windows on ARM
- `WindowsTerminal-RTL-patcher-x64.exe` patcher for x64 installations
- `WindowsTerminal-RTL-patcher-arm64.exe` patcher for ARM64 installations
- `windowsterminal-rtl.patch` the RTL patch, for building Windows Terminal yourself
- `build-unpackaged.patch` the build changes needed to produce an unpackaged build

The portable build needs no installation. It reads your existing Windows Terminal
configuration, so your profiles, themes, colour schemes and fonts carry over:

```text
%LOCALAPPDATA%\Microsoft\Windows Terminal
```

Download the build for your architecture, unzip it anywhere, and run
`WindowsTerminal.exe`. Nothing is installed and nothing outside that folder is
touched.

## Patching an installed copy

If you would rather keep using the terminal you already have from the Microsoft
Store, run the patcher for your architecture. It asks for administrator rights,
so you will see a UAC prompt - that is normal, and it is required because the
installed files live in `C:\Program Files\WindowsApps`.

**This release is built for Windows Terminal `1.24.11911.0`** (see
`REQUIRED_VERSION`). The patcher refuses to patch any other version, because the
Atlas renderer is C++ with no ABI between releases: dropping a `1.24`
`TerminalApp.dll` into a `1.25` install is not a patch, it is a corruption. If
your Store copy is a different version, either install `1.24.11911.0` or build
from the matching upstream ref yourself (see "Building from source").

The patcher:

1. Checks the installed version matches `REQUIRED_VERSION`, and stops if it does not
2. Backs up the DLLs it is about to replace into
   `%ProgramData%\WindowsTerminal-RTL\backup\`
3. Replaces them with the patched ones from the matching portable build
4. Relaunches the terminal

You can also run it from PowerShell:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\patch-wt.ps1 -LocalDir C:\path\to\WindowsTerminal-RTL-x64
```

Useful options:

```powershell
# Put the original files back
.\WindowsTerminal-RTL-patcher.exe -Revert

# Patch a copy somewhere else, e.g. a backup install
.\WindowsTerminal-RTL-patcher.exe -PackageDir "C:\Path\To\Microsoft.WindowsTerminal_1.24.11911.0_x64__8wekyb3d8bbwe"

# Do not relaunch the terminal afterwards
.\WindowsTerminal-RTL-patcher.exe -NoRestart
```

### A caveat, stated plainly

The portable build is built **unpackaged**, while the Store version is packaged.
The DLLs are therefore not byte-identical builds - measured on this machine,
`Microsoft.Terminal.UI.dll` is 230 KB in the Store install and 215 KB here. They
are however built from the **same upstream commit**, `v1.24.11911.0`, so the
interfaces line up; the patcher backs everything up first and `-Revert` puts it
back.

If the patched terminal will not start, run the patcher again with `-Revert`.

### Windows Terminal updates

A Store update replaces the DLLs, which removes the patch and very likely changes
the version, so the patcher will then refuse rather than patch across versions.
After an update, either roll the Store copy back to `1.24.11911.0`, or wait for a
release of this project built against the new version.

## What the patch changes

Five files, all in the Atlas renderer:

- `src/renderer/atlas/AtlasEngine.cpp`
  Records a bidi level per cluster while shaping, applies Unicode bidi rule L2 to
  put the clusters into visual order, emits an odd-level cluster's glyphs back to
  front, permutes the colour planes into visual space, and maps the caret through
  the same permutation.
- `src/renderer/atlas/AtlasEngine.h` and `common.h`
  The `ShapedRow` cluster record, the level range type, and the logical-to-visual
  column map.
- `src/renderer/atlas/DWriteTextAnalysis.cpp` and `.h`
  `AnalyzeBidi` is actually called - `AnalyzeScript` on its own never runs the
  bidi algorithm, so without it every cluster would come back level 0 - and the
  sink now collects the resolved levels.

The complete patch is at:

```text
patch/windowsterminal-rtl.patch
```

Readable copies of the patched files are at:

```text
patch/AtlasEngine.cpp
patch/AtlasEngine.h
patch/DWriteTextAnalysis.cpp
patch/DWriteTextAnalysis.h
patch/common.h
```

Rows containing no RTL cluster at all are left untouched, so lines of only English
text or numbers render bit-for-bit as before.

### Only part of the bidi algorithm

Rule L2 is implemented, which is what reorders whole words. The rules that decide
paragraph direction (L1), mirror brackets and numbers (L3/L4), and place neutral
characters such as spaces (N1/N2) are not. Pure Persian or Arabic text is
correct; mixed content such as `ABC سلام 123` may put the Latin run or the digits
in the wrong place.

This is a deliberate stopping point rather than an oversight: each rule has to be
correct on its own before it is layered on, and getting L2 right is the part that
makes a Persian line readable at all.

### How it was verified

`tools/bidi-probe` holds five small programs that answer questions about the
rendering that cannot be answered by looking at a screen. They run in CI and their
output ships in the release:

| probe | question |
|---|---|
| `probe-rtl-layout` | Runs the renderer's own pipeline and checks that reading the emitted cells in the corresponding direction reproduces the input. This is what distinguishes "the words moved" from "the words are wrong". |
| `probe-glyph-order` | Whether DirectWrite returns the glyphs of an RTL run in logical or visual order. This decides whether rule L2 has to reverse the clusters. |
| `probe-glyph-direction` | Whether DirectWrite reverses them anyway when asked for left to right. |
| `probe-lamalef` | Which installed fonts fuse lam-alef, the mandatory ligature in Arabic script. On this machine: none of them, 0 of 22. |
| `probe-font-fit` | Which font fits a terminal's cell grid. A terminal forces every shaped cluster to exactly one cell, so a font whose Arabic glyphs are narrower than the cell gets stretched, and that is what reads as a gap between letters. |

They need no Windows Terminal to build:

```
cl /nologo /EHsc /std:c++20 probe-rtl-layout.cpp /link dwrite.lib
probe-rtl-layout.exe
```

## Building from source

Requirements:

- Visual Studio 2022 or newer with the C++ workload
- The Windows 10 SDK
- Python 3
- Rust, for the patcher

The Windows Terminal version being patched is in `UPSTREAM_REF`. The build script
clones it, applies the patch, and produces the portable build:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build-portable.ps1 -Arch x64
```

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build-portable.ps1 -Arch arm64
```

Build the patcher:

```powershell
cargo build --release --manifest-path patcher\Cargo.toml
```

Builds are done in CI on `windows-latest` and `windows-11-arm`.

### A note on the build system

Two things about building Windows Terminal unpackaged are not obvious and cost a
long time to find, so they are recorded in `patch/build-unpackaged.patch`:

- Do **not** pass `-p:WindowsStoreApp=false` or `-p:ApplicationType=`. A `-p:`
  value is a global property that overrides the project, and those two silently
  disable the XAML compiler, the generated-files directory, the UAP platform
  winmds and the include paths for generated XAML sources.
- The build must run on a Visual Studio whose version matches the Windows SDK.
  An older one pairs its `Microsoft.Build.Tasks.Xaml` task with a newer SDK's
  compiler, and the settings editor's XAML then dies with
  `WMC9999: Value cannot be null. Parameter name: name` - an internal exception,
  not a diagnosable error. This is why CI builds on `windows-latest`.

## Status

This repository is a workaround until right-to-left support is merged into
Windows Terminal itself.

## License

Windows Terminal is MIT licensed; the license text is in `LICENSE`.

The binaries in the releases are modified versions of Windows Terminal and remain
subject to its license terms.

## Contributors

- AmirAliManzar
- Claude

The RTL patch under `patch/` was written with AI assistance from Claude.

---

## فارسی

این پروژه مشکل نمایش متن‌های راست‌به‌چپ، به‌ویژه فارسی، عربی و عبری، رو در نسخهٔ ویندوز Windows Terminal برطرف می‌کنه.

### دانلود

از بخش [Releases](../../releases) نسخهٔ مناسب سیستمتون رو بگیرید:

- `WindowsTerminal-RTL-x64.zip` برای پردازنده‌های ۶۴ بیتی اینتل/AMD
- `WindowsTerminal-RTL-arm64.zip` برای ویندوز روی ARM
- `WindowsTerminal-RTL-patcher-x64.exe` و `WindowsTerminal-RTL-patcher-arm64.exe` برای اصلاح نسخهٔ نصب‌شده

نسخهٔ **پورتابل** نیازی به نصب ندارد. تنظیمات فعلی Windows Terminal شما را هم می‌خواند، پروفایل‌ها، تم‌ها، طرح‌رنگ‌ها و فونت‌هایتان حفظ می‌شوند:

```text
%LOCALAPPDATA%\Microsoft\Windows Terminal
```

فایل zip را هرجا باز کنید و `WindowsTerminal.exe` را اجرا کنید.

### اصلاح نسخهٔ نصب‌شده

اگر ترمینال نصب‌شده از Store را نگه می‌دارید، Patcher مربوط به معماری سیستمتان را اجرا کنید. پنجرهٔ UAC ظاهر می‌شود؛ این طبیعی است، چون فایل‌های نصب‌شده داخل `C:\Program Files\WindowsApps` هستند و نوشتن در آن‌ها دسترسی ادمین می‌خواهد.

**این نسخه برای Windows Terminal `1.24.11911.0` ساخته شده** (فایل `REQUIRED_VERSION`). Patcher از اصلاح هر نسخهٔ دیگری خودداری می‌کند، چون رندرکنندهٔ Atlas به زبان C++ نوشته شده و بین نسخه‌ها ABI پایداری ندارد: گذاشتن `TerminalApp.dll` نسخهٔ `1.24` داخل یک نصب `1.25` اصلاح نیست، خراب کردن است. اگر نسخهٔ Store شما چیز دیگری است، یا `1.24.11911.0` را نصب کنید یا خودتان از همان ref نسخهٔ بالا بیلد بگیرید (بخش «بیلد از سورس» را ببینید).

مراحل کار Patcher:

۱. نسخهٔ نصب‌شده را با `REQUIRED_VERSION` چک می‌کند و اگر یکی نباشد متوقف می‌شود
۲. قبل از هر تغییری از DLLها پشتیبان می‌گیرد در `%ProgramData%\WindowsTerminal-RTL\backup\`
۳. فایل‌ها را با نسخهٔ اصلاح‌شده جایگزین می‌کند
۴. ترمینال را دوباره باز می‌کند

⚠️ **یک نکتهٔ مهم:** بیلد پورتابل ما **unpackaged** است ولی نسخهٔ Store **packaged**، پس DLLها بایت‌به‌بایت یکی نیستند (مثلاً `Microsoft.Terminal.UI.dll` در Store حدود ۲۳۰ کیلوبایت و در اینجا ۲۱۵ کیلوبایت است). ولی هر دو از **همان کامیتی upstream** یعنی `v1.24.11911.0` ساخته شده‌اند، پس رابط‌ها با هم می‌خوانند. در هر حال Patcher ابتدا پشتیبان می‌گیرد و `-Revert` همه‌چیز را برمی‌گرداند.

اگر ترمینال اصلاح‌شده بالا نیامد، دوباره با `-Revert` اجرا کنید.

با هر به‌روزرسانی رسمی، DLLها جایگزین می‌شوند و پچ از بین می‌رود و احتمالاً نسخه هم عوض می‌شود، پس Patcher به‌جای اینکه نسخه‌های مختلف را با هم مخلوط کند، متوقف می‌شود. بعد از آپدیت یا Store را به `1.24.11911.0` برگردانید، یا منتظر نسخهٔ جدید این پروژه بمانید.

### چه چیزی تغییر کرده

پنج فایل، همه در رندرکنندهٔ Atlas:

- `src/renderer/atlas/AtlasEngine.cpp`
- `src/renderer/atlas/AtlasEngine.h`
- `src/renderer/atlas/DWriteTextAnalysis.cpp`
- `src/renderer/atlas/DWriteTextAnalysis.h`
- `src/renderer/atlas/common.h`

قاعدهٔ L2 یونیکد پیاده شده که ترتیب کلمات را درست می‌کند. سطرهایی که هیچ متن راست‌به‌چپ ندارند دست‌نخورده می‌مانند، پس متن انگلیسی و عدد دقیقاً مثل قبل رسم می‌شود.

قاعده‌های دیگر یونیکد (تعیین جهت پاراگراف L1، آینه‌کردن پرانتز و اعداد L3/L4، و جای‌گذاری فاصله‌ها N1/N2) هنوز پیاده نشده‌اند. متن کاملاً فارسی درست است؛ محتوای ترکیبی مثل `ABC سلام 123` ممکن است جای بخش لاتین یا عددها جابه‌جا شود.

پچ کامل در `patch/windowsterminal-rtl.patch` و نسخهٔ خوانای فایل‌های تغییرکرده در `patch/` است.

### آزمون‌ها

پنج برنامهٔ کوچک در `tools/bidi-probe` هست که به سؤال‌هایی جواب می‌دهند که با نگاه کردن به صفحه نمی‌شود پرسید — از جمله اینکه آیا خواندنِ سلول‌های رسم‌شده متن اصلی را برمی‌گرداند یا نه، و کدام فونت با شبکهٔ سلولی ترمینال جور درمی‌آید. خروجی‌شان در هر بیلد اجرا و داخل ریلیز منتشر می‌شود.

دو نکته که با اندازه‌گیری ثابت شده و ممکن است انتظارش را نداشته باشید:

- **هیچ فونت نصب‌شده‌ای لام‌الف را یکی نمی‌کند** (۰ از ۲۲ فونت عربی‌دار). لام‌الف در تایپوگرافی فارسی و عربی یک لیگاتور اجباری است، ولی فونت‌های موجود این سیستم آن را ندارند.
- **فاصلهٔ بین حروف به عرض سلول بستگی دارد.** ترمینال هر cluster را دقیقاً روی یک سلول می‌کشد، پس فونتی که گلیف‌های عربی‌اش باریک‌تر از سلول باشد کشیده می‌شود. نسبت گلیف عربی به سلول بین ۰.۵۹ تا ۱.۲۱ varies است؛ `Vazir Code Hack` با ۱.۰۳ نزدیک‌ترین گزینهٔ نصب‌شده است.

### ساخت از سورس

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build-portable.ps1 -Arch x64
```

نسخهٔ Windows Terminal که پچ می‌شود در فایل `UPSTREAM_REF` مشخص است.

⚠️ در بیلد Windows Terminal نباید `-p:WindowsStoreApp=false` یا `-p:ApplicationType=` را بدهید؛ این‌ها کامپایلر XAML را از کار می‌اندازند. همچنین بیلد باید روی نسخه‌ای از Visual Studio اجرا شود که با Windows SDK هم‌نسخه باشد، وگرنه بخش تنظیمات با خطای داخلی `WMC9999` می‌افتد. به همین دلیل CI روی `windows-latest` بیلد می‌کند.

### مجوز

Windows Terminal تحت مجوز MIT منتشر شده و متن آن در `LICENSE` موجود است. فایل‌های باینری داخل ریلیز، نسخهٔ تغییرکردهٔ Windows Terminal هستند و تابع همان مجوز می‌مانند.

### مشارکت‌کنندگان

- AmirAliManzar
- Claude

پچ راست‌به‌چپ در `patch/` با کمک Claude نوشته شده است.