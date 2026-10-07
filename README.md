# Windows Terminal RTL

A patch that fixes right-to-left text rendering in [Windows Terminal](https://github.com/microsoft/terminal).

On Windows, Windows Terminal has problems rendering Persian, Arabic and Hebrew. Words appear in the wrong order, letters can come apart, and the caret can land in the wrong cell.

This patch fixes the problem at the text-shaping layer, in the Atlas renderer, so it applies everywhere Windows Terminal draws text: the tab bar, the settings UI, and anything running inside the terminal - shells, `claude code`, `opencode`, editors over SSH.

## Downloads

Prebuilt files are on the [Releases](../../releases) page:

- `WindowsTerminal-RTL-Installer.exe` the installer, one file that downloads
  and installs the right build for you
- `WindowsTerminal-RTL-Portable-x64.exe` the whole x64 build as a single file,
  no installer and no unzip: run it anywhere
- `WindowsTerminal-RTL-Portable-arm64.exe` the same for Windows on ARM
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

- **Installer:** grab `WindowsTerminal-RTL-Installer.exe` from the Releases page
  and double-click it. It opens a short wizard: pick the architecture, pick the
  install folder, choose whether to add Start menu and desktop shortcuts, and it
  downloads the matching build, verifies it against its sha256 and installs it.
  The wizard is English by default and switches to Persian when your Windows
  display language is Persian; the language button in the header corner switches
  between the two at any point. No admin rights, and the Store terminal is
  untouched. Run it again with `--uninstall` to remove it.
- **No install:** run `WindowsTerminal.exe` from the zip. Nothing is installed
  and nothing outside that folder is touched.
- **One file, not even a zip:** run `WindowsTerminal-RTL-Portable-x64.exe` (or
  the `arm64` one) from wherever you put it. The whole build is inside that one
  file. The first run unpacks it into
  `%LOCALAPPDATA%\Programs\WindowsTerminal-RTL-Portable` and starts the terminal,
  and every run after that is instant, because the payload is already on disk and
  the marker still matches. Nothing is registered and no admin rights are asked
  for. A new release replaces the previous folder, so two versions never mix.
- **With a Start menu entry:** the zip also carries `install.ps1`. Right-click
  it → *Run with PowerShell*. It copies the build to
  `%LOCALAPPDATA%\Programs\WindowsTerminal-RTL`, creates a Start menu shortcut
  (and a desktop one), and needs no admin rights. Run
  `install.ps1 -Uninstall` to remove it. Your settings are never touched either
  way, because a portable build reads the unpackaged settings path above.

The build carries its own fonts. `CascadiaCode.ttf` and `CascadiaMono.ttf` sit
next to `WindowsTerminal.exe`, and the terminal loads any `.ttf` beside its own
executable *before* it asks the system for a typeface, so it still renders its
text on a machine that has no fonts installed at all. Nothing is registered for
that and no admin rights are needed. This is what makes the portable build, and
the single-file EXE above, independent of the machine they land on.

**System requirements:** Windows 10 version 2004 (build 19041) or later, the same
floor as the upstream portable distribution. Windows Server 2019 is build 17763
and is below it, so the terminal cannot run there and the installer says so
rather than placing a copy that cannot open. Windows Server 2022 (build 20348)
and later are supported.

## Patching an installed copy, and why it is not possible

An honest note, because the alternative is shipping a tool that fails in a way
that looks like the user's fault: **replacing files inside a Store-installed
terminal is not possible on modern Windows.**

Store packages live in `C:\Program Files\WindowsApps`, which is protected by
the Windows Container Isolation File System (`wcifs`) filter driver. We
verified this directly on a fresh install. Taking ownership of the entire
package recursively (`takeown /R /A /D Y`) succeeds: 265 files now owned by
Administrators. Granting Administrators full control recursively
(`icacls /grant *S-1-5-32-544:F /T`) also succeeds: 265 files processed, zero
failures. Then creating a single new file in that same folder is still denied.
`wcifs` ignores ownership and ACLs entirely; it blocks writes from any process
that is not running inside the package's own container. The `takeown` recipe
that works on Windows 8 and early Windows 10 does not work on anything that
ships `wcifs`.

The only writer `wcifs` trusts is the AppX deployment API, the same path the
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
  bidi algorithm, so without it every cluster would come back level 0 - the sink
  now collects the resolved levels, and a row's paragraph direction follows the
  Unicode rules P2/P3: it is right-to-left when the row's *first* strong
  character is right-to-left.

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

Rule L2 is implemented, which is what reorders whole words, and paragraph
direction is decided here by the Unicode rules P2/P3: a row is right-to-left when
its *first* strong character is right-to-left, and left-to-right otherwise. A
line that starts with a Latin prompt is therefore a left-to-right paragraph, the
prompt keeps its place on the left, and any Persian, Arabic or Hebrew after it is
a right-to-left run nested inside it, laid out the way a browser or a text editor
lays out mixed content. A line that starts with Persian is a right-to-left
paragraph and runs from the right. Where neutral characters such as spaces settle
is resolved by DirectWrite from that paragraph direction. The `probe-para-dir`
probe in `tools/bidi-probe` prints the resolved levels and the final visual order
under both this rule and the older rule where any RTL letter makes the whole row
RTL, so the difference is visible rather than argued about.

Not implemented here: bracket mirroring and number shaping (L3/L4), so mixed
content such as `ABC سلام 123` may put the digits in the wrong place.

This is a deliberate stopping point rather than an oversight: each rule has to be
correct on its own before it is layered on.

### How it was verified

`tools/bidi-probe` holds six small programs that answer questions about the
rendering that cannot be answered by looking at a screen. They run in CI and
their output is attached to the workflow run as build artifacts:

| probe | question |
|---|---|
| `probe-rtl-layout` | Runs the renderer's own pipeline and checks that reading the emitted cells in the corresponding direction reproduces the input. This is what distinguishes "the words moved" from "the words are wrong". |
| `probe-para-dir` | How a row's paragraph direction is decided. Prints the resolved levels and the final visual order for a prompt followed by Persian under both P2/P3, which looks at the first strong character, and the older rule that any strong RTL letter is decisive, so the difference is visible. |
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

- `WindowsTerminal-RTL-Installer.exe` فایل نصب، یک فایل که بیلد مناسب سیستم شما را دانلود و نصب می‌کند
- `WindowsTerminal-RTL-Portable-x64.exe` کل بیلد x64 در یک فایل، بدون نصب و بدون باز کردن zip؛ هرجا اجرا کنید
- `WindowsTerminal-RTL-Portable-arm64.exe` همین برای ویندوز روی ARM
- `WindowsTerminal-RTL-x64.zip` برای پردازنده‌های ۶۴ بیتی اینتل/AMD
- `WindowsTerminal-RTL-arm64.zip` برای ویندوز روی ARM
- `windowsterminal-rtl.patch` خود پچ RTL، برای بیلد گرفتن خودتان از Windows Terminal
- `build-unpackaged.patch` تغییرات لازم برای تولید بیلد unpackaged

نسخهٔ **پورتابل** نیازی به نصب ندارد. تنظیمات فعلی Windows Terminal شما را هم می‌خواند، پروفایل‌ها، تم‌ها، طرح‌رنگ‌ها و فونت‌هایتان حفظ می‌شوند:

```text
%LOCALAPPDATA%\Microsoft\Windows Terminal
```

فایل zip را هرجا باز کنید.

- **نصب:** `WindowsTerminal-RTL-Installer.exe` را از صفحهٔ Releases بگیرید و دوبار‌کلیک کنید. یک ویزارد کوتاه باز می‌شود: معماری را انتخاب می‌کنید، پوشهٔ نصب را مشخص می‌کنید، انتخاب می‌کنید که میانبر منوی Start و دسکتاپ ساخته شود یا نه، و بعد بیلد مناسب دانلود، با sha256 تأیید و نصب می‌شود. زبان ویزارد پیش‌فرض انگلیسی است و اگر زبان نمایش ویندوز فارسی باشد فارسی می‌شود؛ دکمهٔ زبان در گوشهٔ بالای ویزارد هر لحظه بین این دو جابه‌جا می‌شود. نیازی به دسترسی مدیر ندارد و به ترمینال Store دست نمی‌زند. با `--uninstall` هم حذف می‌شود.
- **بدون نصب:** همان `WindowsTerminal.exe` را از پوشه اجرا کنید. چیزی نصب نمی‌شود و خارج از آن پوشه چیزی لمس نمی‌شود.
- **یک فایل، حتی بدون zip:** `WindowsTerminal-RTL-Portable-x64.exe` (یا نسخهٔ `arm64`) را از هرجا که گذاشته‌اید اجرا کنید. کل بیلد داخل همان یک فایل است. اولین اجرا آن را در `%LOCALAPPDATA%\Programs\WindowsTerminal-RTL-Portable` باز می‌کند و ترمینال را بالا می‌آورد، و اجراهای بعدی فوری هستند، چون محتوا از قبل روی دیسک است و نشانگر هنوز با آن مطابقت دارد. چیزی ثبت نمی‌شود و دسترسی مدیر خواسته نمی‌شود. یک ریلیز جدید پوشهٔ نسخهٔ قبلی را جایگزین می‌کند، پس دو نسخه هرگز با هم قاطی نمی‌شوند.
- **با ورودی در منوی Start:** داخل zip یک `install.ps1` هم هست. روی آن راست‌کلیک کنید و *Run with PowerShell* را بزنید. بیلد را به `%LOCALAPPDATA%\Programs\WindowsTerminal-RTL` کپی می‌کند، یک شورتکات در منوی Start (و یکی روی دسکتاپ) می‌سازد و نیازی به دسترسی مدیر ندارد. با `install.ps1 -Uninstall` هم حذف می‌شود. تنظیمات شما در هر دو حالت دست‌نخورده می‌مانند، چون بیلد پورتابل همان مسیر تنظیمات unpackaged بالا را می‌خواند.

بیلد فونت‌های خودش را همراه دارد. `CascadiaCode.ttf` و `CascadiaMono.ttf` کنار `WindowsTerminal.exe` قرار دارند و ترمینال هر `.ttf` که کنار فایل اجرایی خودش باشد را *قبل از* درخواست فونت از سیستم بارگذاری می‌کند، بنابراین روی سیستمی که اصلاً فونتی نصب نیست هم متن خود را نشان می‌دهد. هیچ فونتی ثبت نمی‌شود و دسترسی مدیر هم لازم نیست. همین چیزی است که بیلد پورتابل و فایل تک‌فایلهٔ بالا را مستقل از سیستمی می‌کند که رویش اجرا می‌شوند.

**پیش‌نیاز سیستم:** ویندوز ۱۰ نسخهٔ ۲۰۰۴ (بیلد ۱۹۰۴۱) یا جدیدتر، همان حدی که توزیع پورتابل بالادست لازم دارد. Windows Server 2019 بیلد ۱۷۷۶۳ است و پایین‌تر از این حد است، پس ترمینال روی آن اجرا نمی‌شود و نصب‌کننده همین را به شما می‌گوید، به جای اینکه نسخه‌ای را نصب کند که باز نمی‌شود. Windows Server 2022 (بیلد ۲۰۳۴۸) و جدیدتر پشتیبانی می‌شوند.

### اصلاح نسخهٔ نصب‌شده و چرا ممکن نیست

یادداشت صادقانه، چون در غیر این صورت پروژه ابزاری منتشر می‌کند که شکستش شبیه تقصیر خود کاربر است: **جایگزینی فایل‌ها داخل یک ترمینال نصب‌شده از Store در ویندوزهای جدید ممکن نیست.**

بسته‌های Store داخل `C:\Program Files\WindowsApps` هستند که توسط درایور فیلتر «سیستم فایل ایزولهٔ ویندوز» (`wcifs`) محافظت می‌شود. ما این را روی یک نصب تازه خودمان تست کردیم: گرفتن مالکیت کل پکیج به‌صورت بازگشتی (`takeown /R /A /D Y`) موفق می‌شود، ۲۶۵ فایل به مالکیت مدیران درمی‌آید. دادن دسترسی کامل به مدیران به‌صورت بازگشتی (`icacls /grant *S-1-5-32-544:F /T`) هم موفق می‌شود، ۲۶۵ فایل پردازش، صفر شکست. با این حال ساختن یک فایل جدید در همان پوشه همچنان رد می‌شود. `wcifs` اصلاً به مالکیت و ACL نگاه نمی‌کند؛ نوشتن از هر پروسه‌ای که داخل کانتینر خود پکیج اجرا نمی‌شود را مسدود می‌کند. همان دستور `takeown` که در ویندوز ۸ و ویندوز ۱۰ٔ اولیه کار می‌کرد، در سیستمی که `wcifs` دارد دیگر کار نمی‌کند.

تنها نوشتنی که `wcifs` به آن اعتماد می‌کند API استقرار AppX است، همان مسیری که خود Store از آن استفاده می‌کند. استفاده از آن برای جایگزینی نصب Store در جای خودش نیازمند پکیجی امضاشده توسط مایکروسافت است که این پروژه نمی‌تواند تولید کند.

**بیلد پورتابل بالا جایگزین کارآمد است.** از هر پوشه‌ای اجرا می‌شود، تنظیمات و فونت‌های فعلی شما را می‌خواند، نیازی به نصب ندارد و خارج از آن پوشه هیچ‌چیز را لمس نمی‌کند.

یک `install.ps1` اختیاری داخل zip قرار دارد که بدون دست زدن به نصب Store، یک ورودی در منوی Start به بیلد می‌دهد؛ بنابراین کار روی AppX بالا تنها راه رسیدن به آن نیست.

### چه چیزی تغییر کرده

پنج فایل، همه در رندرکنندهٔ Atlas:

- `src/renderer/atlas/AtlasEngine.cpp`
- `src/renderer/atlas/AtlasEngine.h`
- `src/renderer/atlas/DWriteTextAnalysis.cpp`
- `src/renderer/atlas/DWriteTextAnalysis.h`
- `src/renderer/atlas/common.h`

قاعدهٔ L2 یونیکد پیاده شده که ترتیب کلمات را درست می‌کند، و جهت پاراگراف هم همین‌جا بر اساس قاعده‌های P2/P3 یونیکد تعیین می‌شود: سطری راست‌به‌چپ است که *اولین* حرف قوی آن راست‌به‌چپ باشد، و در غیر این صورت چپ‌به‌راست است. بنابراین سطری که با یک پرامپت انگلیسی شروع می‌شود یک پاراگراف چپ‌به‌راست می‌ماند، پرامپت همان‌جای خودش سمت چپ حفظ می‌شود، و هر متن فارسی، عربی یا عبری که بعد از آن بیاید یک بازهٔ راست‌به‌چپ درون همان پاراگراف است و مثل کاری که یک مرورگر یا ویرایشگر متن با محتوای ترکیبی می‌کند چیده می‌شود. سطری که با فارسی شروع شود یک پاراگراف راست‌به‌چپ است و از سمت راست چیده می‌شود. محل قرار گرفتن نویسه‌های خنثی مثل فاصله را خود DirectWrite بر اساس همان جهت پاراگراف مشخص می‌کند. پروب `probe-para-dir` در `tools/bidi-probe` سطوح حل‌شده و ترتیب نهایی تصویری را زیر هر دو قاعده چاپ می‌کند، یعنی همین قاعده و قاعدهٔ قدیمی‌تر که هر حرف راست‌به‌چپی کل سطر را راست‌به‌چپ می‌کرد، تا تفاوت دیده شود به‌جای اینکه درباره‌اش بحث شود.

سطرهایی که هیچ متن راست‌به‌چپ ندارند دست‌نخورده می‌مانند، پس متن انگلیسی و عدد دقیقاً مثل قبل رسم می‌شود.

آینه‌کردن پرانتز و شکل‌دهی اعداد (L3/L4) هنوز پیاده نشده‌اند، پس محتوای ترکیبی مثل `ABC سلام 123` ممکن است جای عددها جابه‌جا نشان داده شود.

پچ کامل در `patch/windowsterminal-rtl.patch` و نسخهٔ خوانای فایل‌های تغییرکرده در `patch/` است.

### آزمون‌ها

شش برنامهٔ کوچک در `tools/bidi-probe` هست که به سؤال‌هایی جواب می‌دهند که با نگاه کردن به صفحه نمی‌شود پرسید، از جمله اینکه آیا خواندنِ سلول‌های رسم‌شده متن اصلی را برمی‌گرداند یا نه، جهت پاراگراف یک سطر باید چطور تعیین شود، و کدام فونت با شبکهٔ سلولی ترمینال جور درمی‌آید. خروجی‌شان در هر بیلد اجرا می‌شود و به‌عنوان artifact به همان اجرای CI پیوست می‌شود، ولی دیگر داخل صفحهٔ ریلیز قرار نمی‌گیرد.

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