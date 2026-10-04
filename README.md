# Windows Terminal RTL

A patch that fixes right-to-left text rendering in [Windows Terminal](https://github.com/microsoft/terminal).

On Windows, Windows Terminal has problems rendering Persian, Arabic and Hebrew. Words appear in the wrong order, letters can come apart, and the caret can land in the wrong cell.

This patch fixes the problem at the text-shaping layer, in the Atlas renderer, so it applies everywhere Windows Terminal draws text: the tab bar, the settings UI, and anything running inside the terminal - shells, `claude code`, `opencode`, editors over SSH.

## Downloads

Prebuilt files are on the [Releases](../../releases) page:

- `install-latest.ps1` the small installer — a few kilobytes that download and
  install the right build for you
- `WindowsTerminal-RTL-x64.zip` portable build for 64-bit Intel/AMD
- `WindowsTerminal-RTL-arm64.zip` portable build for Windows on ARM
- `windowsterminal-rtl.patch` the RTL patch, for building Windows Terminal yourself
- `build-unpackaged.patch` the build changes needed to produce an unpackaged build

The portable build needs no installation. It reads your existing Windows Terminal
configuration, so your profiles, themes, colour schemes and fonts carry over:

```text
%LOCALAPPDATA%\Microsoft\Windows Terminal
```

Download the build for your architecture and unzip it anywhere.

- **Small installer:** grab `install-latest.ps1` from the Releases page,
  right-click → *Run with PowerShell*. It detects your architecture, downloads
  the matching build, verifies it, and installs it. This is the lightest way in.
- **No install:** run `WindowsTerminal.exe` from the zip. Nothing is installed
  and nothing outside that folder is touched.
- **With a Start menu entry:** the zip also carries `install.ps1`. Right-click
  it → *Run with PowerShell*. It copies the build to
  `%LOCALAPPDATA%\Programs\WindowsTerminal-RTL`, creates a Start menu shortcut
  (and a desktop one), and needs no admin rights. Run
  `install.ps1 -Uninstall` to remove it. Your settings are never touched either
  way, because a portable build reads the unpackaged settings path above.

## Patching an installed copy — and why it is not possible

An honest note, because the alternative is shipping a tool that fails in a way
that looks like the user's fault: **replacing files inside a Store-installed
terminal is not possible on modern Windows.**

Store packages live in `C:\Program Files\WindowsApps`, which is protected by
the Windows Container Isolation File System (`wcifs`) filter driver. We
verified this directly on a fresh install. Taking ownership of the entire
package recursively (`takeown /R /A /D Y`) succeeds — 265 files now owned by
Administrators. Granting Administrators full control recursively
(`icacls /grant *S-1-5-32-544:F /T`) also succeeds — 265 files processed, zero
failures. Then creating a single new file in that same folder is still denied.
`wcifs` ignores ownership and ACLs entirely; it blocks writes from any process
that is not running inside the package's own container. The `takeown` recipe
that works on Windows 8 and early Windows 10 does not work on anything that
ships `wcifs`.

The only writer `wcifs` trusts is the AppX deployment API — the same path the
Store itself uses. Going through it requires a package signed by Microsoft to
replace the Store install in place, which is not something this project can do.

**The portable build above is the working alternative.** It runs from any
folder, reads your existing settings and fonts, needs no installation, and
touches nothing outside that folder.

An optional `install.ps1` is in the zip and gives the build a Start menu entry
without touching the Store install, so the AppX work above is no longer the
only route to that.

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
rendering that cannot be answered by looking at a screen. They run in CI and
their output is attached to the workflow run as build artifacts:

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

The Windows Terminal version being patched is in `UPSTREAM_REF`. The build script
clones it, applies the patch, and produces the portable build:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build-portable.ps1 -Arch x64
```

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build-portable.ps1 -Arch arm64
```

Builds are done in CI on `windows-latest`.

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
- `windowsterminal-rtl.patch` خود پچ RTL، برای بیلد گرفتن خودتان از Windows Terminal
- `build-unpackaged.patch` تغییرات لازم برای تولید بیلد unpackaged

نسخهٔ **پورتابل** نیازی به نصب ندارد. تنظیمات فعلی Windows Terminal شما را هم می‌خواند، پروفایل‌ها، تم‌ها، طرح‌رنگ‌ها و فونت‌هایتان حفظ می‌شوند:

```text
%LOCALAPPDATA%\Microsoft\Windows Terminal
```

فایل zip را هرجا باز کنید.

- **بدون نصب:** همان `WindowsTerminal.exe` را از پوشه اجرا کنید. چیزی نصب نمی‌شود و خارج از آن پوشه چیزی لمس نمی‌شود.
- **با ورودی در منوی Start:** داخل zip یک `install.ps1` هم هست. روی آن راست‌کلیک کنید و *Run with PowerShell* را بزنید. بیلد را به `%LOCALAPPDATA%\Programs\WindowsTerminal-RTL` کپی می‌کند، یک شورتکات در منوی Start (و یکی روی دسکتاپ) می‌سازد و نیازی به دسترسی مدیر ندارد. با `install.ps1 -Uninstall` هم حذف می‌شود. تنظیمات شما در هر دو حالت دست‌نخورده می‌مانند، چون بیلد پورتابل همان مسیر تنظیمات unpackaged بالا را می‌خواند.

### اصلاح نسخهٔ نصب‌شده — و چرا ممکن نیست

یادداشت صادقانه، چون در غیر این صورت پروژه ابزاری منتشر می‌کند که شکستش شبیه تقصیر خود کاربر است: **جایگزینی فایل‌ها داخل یک ترمینال نصب‌شده از Store در ویندوزهای جدید ممکن نیست.**

بسته‌های Store داخل `C:\Program Files\WindowsApps` هستند که توسط درایور فیلتر «سیستم فایل ایزولهٔ ویندوز» (`wcifs`) محافظت می‌شود. ما این را روی یک نصب تازه خودمان تست کردیم: گرفتن مالکیت کل پکیج به‌صورت بازگشتی (`takeown /R /A /D Y`) موفق می‌شود — ۲۶۵ فایل به مالکیت مدیران درمی‌آید. دادن دسترسی کامل به مدیران به‌صورت بازگشتی (`icacls /grant *S-1-5-32-544:F /T`) هم موفق می‌شود — ۲۶۵ فایل پردازش، صفر شکست. با این حال ساختن یک فایل جدید در همان پوشه همچنان رد می‌شود. `wcifs` اصلاً به مالکیت و ACL نگاه نمی‌کند؛ نوشتن از هر پروسه‌ای که داخل کانتینر خود پکیج اجرا نمی‌شود را مسدود می‌کند. همان دستور `takeown` که در ویندوز ۸ و ویندوز ۱۰ٔ اولیه کار می‌کرد، در سیستمی که `wcifs` دارد دیگر کار نمی‌کند.

تنها نوشتنی که `wcifs` به آن اعتماد می‌کند API استقرار AppX است — همان مسیری که خود Store از آن استفاده می‌کند. استفاده از آن برای جایگزینی نصب Store در جای خودش نیازمند پکیجی امضاشده توسط مایکروسافت است که این پروژه نمی‌تواند تولید کند.

**بیلد پورتابل بالا جایگزین کارآمد است.** از هر پوشه‌ای اجرا می‌شود، تنظیمات و فونت‌های فعلی شما را می‌خواند، نیازی به نصب ندارد و خارج از آن پوشه هیچ‌چیز را لمس نمی‌کند.

یک `install.ps1` اختیاری داخل zip قرار دارد که بدون دست زدن به نصب Store، یک ورودی در منوی Start به بیلد می‌دهد؛ بنابراین کار روی AppX بالا تنها راه رسیدن به آن نیست.

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

پنج برنامهٔ کوچک در `tools/bidi-probe` هست که به سؤال‌هایی جواب می‌دهند که با نگاه کردن به صفحه نمی‌شود پرسید — از جمله اینکه آیا خواندنِ سلول‌های رسم‌شده متن اصلی را برمی‌گرداند یا نه، و کدام فونت با شبکهٔ سلولی ترمینال جور درمی‌آید. خروجی‌شان در هر بیلد اجرا می‌شود و به‌عنوان artifact به همان اجرای CI پیوست می‌شود، ولی دیگر داخل صفحهٔ ریلیز قرار نمی‌گیرد.

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