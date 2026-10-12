# Windows Terminal RTL

A Windows patch that fixes right-to-left text rendering in [Windows Terminal](https://github.com/microsoft/terminal).

On Windows, Windows Terminal has problems rendering Persian, Arabic and Hebrew text. Words may appear in the wrong order, letters may come apart, and the caret may land in the wrong cell.

This patch fixes the problem at the text-shaping layer, in the Atlas renderer, so it applies everywhere Windows Terminal draws text: the tab bar, the settings UI, and anything running inside the terminal, including shells, `claude code`, `opencode` and editors over SSH.

## Downloads

Prebuilt files are available on the [Releases](../../releases) page:

- `WindowsTerminal-RTL-Installer.exe` The installer: one file that downloads and installs the right build for you
- `WindowsTerminal-RTL-Portable-x64.exe` The whole x64 build as a single file, with no installer and no unzip
- `WindowsTerminal-RTL-Portable-arm64.exe` The same for Windows on ARM
- `WindowsTerminal-RTL-x64.zip` Portable build for 64-bit Intel/AMD systems
- `WindowsTerminal-RTL-arm64.zip` Portable build for Windows on ARM
- `windowsterminal-rtl.patch` The RTL patch, for building Windows Terminal yourself
- `build-unpackaged.patch` The build changes needed to produce an unpackaged build
- `utf8-console-codepage.patch` The console codepage fix, so Persian survives the byte path on any locale

The portable builds do not require installation or administrator access. They read your existing Windows Terminal configuration, so your profiles, themes, colour schemes and fonts carry over:

```text
%LOCALAPPDATA%\Microsoft\Windows Terminal
```

Download the build that matches your system and unzip it anywhere.

- **Installer:** grab `WindowsTerminal-RTL-Installer.exe` from the Releases page and double-click it. It opens a short wizard: pick the architecture, pick the install folder, choose whether to add Start menu and desktop shortcuts, and it downloads the matching build, verifies it against its sha256 and installs it. The wizard is English by default and switches to Persian when your Windows display language is Persian. The language button in the header corner switches between the two at any point. No admin rights are needed, and the Store terminal is untouched. The install registers itself in *Add or remove programs*, so it can be removed from Settings. Running the installer again over an existing install offers three options: **Update**, when the installed build is older than the installer, which downloads the latest release and replaces the build in place; **Repair**, which recreates the shortcuts and the *Add or remove programs* entry without re-downloading; and **Uninstall**. From the command line, `--uninstall` removes and `--repair` rebuilds the shortcuts.
- **No install:** run `WindowsTerminal.exe` from the zip. Nothing is installed and nothing outside that folder is touched.
- **One file, not even a zip:** run `WindowsTerminal-RTL-Portable-x64.exe` (or the `arm64` one) from wherever you put it. The whole build is inside that one file. The first run unpacks it into `%LOCALAPPDATA%\Programs\WindowsTerminal-RTL-Portable` and starts the terminal. Every run after that is instant, because the payload is already on disk and the marker still matches. Nothing is registered and no admin rights are asked for. A new release replaces the previous folder, so two versions never mix.
- **With a Start menu entry:** the zip also carries `install.ps1`. Right-click it and choose *Run with PowerShell*. It copies the build to `%LOCALAPPDATA%\Programs\WindowsTerminal-RTL`, creates a Start menu shortcut and a desktop one, and needs no admin rights. Run `install.ps1 -Uninstall` to remove it. Your settings are never touched, because a portable build reads the unpackaged settings path above.

## Fonts and the console codepage

The build carries its own fonts. `CascadiaCode.ttf` and `CascadiaMono.ttf` sit next to `WindowsTerminal.exe`, and the terminal loads any `.ttf` beside its own executable before it asks the system for a typeface. It therefore renders its text on a machine that has no fonts installed at all. Nothing is registered for that and no admin rights are needed.

Both bundled fonts cover every one of the 45 core Persian and Arabic letters through their cmap tables, and both carry a real GSUB table with the Arabic shaping features (`ccmp`, `init`, `medi`, `fina`, `rlig`) under the `arab` script. DirectWrite applies those features because the renderer runs the text through `AnalyzeScript` first, and the script analysis that call returns for Arabic is what turns positional shaping on. Shaping does not depend on anything the machine happens to have installed.

Drawing the glyphs is only half of the problem. The text also has to reach the console intact. A program writing through the byte-oriented console API encodes its text to the console's codepage before it writes, and upstream seeds that codepage from the machine's OEM codepage. On a machine whose locale is not Arabic, Persian is not representable in that codepage, so every letter becomes a literal `?`. That is the `????` you see on a Windows install with no Persian language support, and it is a conversion failure, not a missing glyph.

This build defaults the console to UTF-8 (codepage 65001) instead, so the round-trip holds on any machine regardless of its locale. One tradeoff is worth stating: a program that deliberately writes bytes in the legacy OEM codepage, a batch file saved in the ANSI codepage for instance, will have those bytes read as UTF-8. Anything that writes UTF-8 works, and UTF-8 is the modern default. A program that wants its old codepage back can still call `SetConsoleOutputCP`.

**System requirements:** Windows 10 version 2004 (build 19041) or later, the same floor as the upstream portable distribution. Windows Server 2019 is build 17763 and is below it, so the terminal cannot run there and the installer says so rather than installing a copy that cannot open. Windows Server 2022 (build 20348) and later are supported.

## Patching an installed copy, and why it is not possible

An honest note, because the alternative is shipping a tool that fails in a way that looks like the user's fault: replacing files inside a Store-installed terminal is not possible on modern Windows.

Store packages live in `C:\Program Files\WindowsApps`, which is protected by the Windows Container Isolation File System (`wcifs`) filter driver. This was verified directly on a fresh install. Taking ownership of the entire package recursively (`takeown /R /A /D Y`) succeeds: 265 files become owned by Administrators. Granting Administrators full control recursively (`icacls /grant *S-1-5-32-544:F /T`) also succeeds: 265 files processed, zero failures. Then creating a single new file in that same folder is still denied. `wcifs` ignores ownership and ACLs entirely; it blocks writes from any process that is not running inside the package's own container. The `takeown` recipe that works on Windows 8 and early Windows 10 does not work on anything that ships `wcifs`.

The only writer `wcifs` trusts is the AppX deployment API, the same path the Store itself uses. Replacing the Store install in place through it requires a package signed by Microsoft, which this project cannot produce.

The portable build above is the working alternative. It runs from any folder, reads your existing settings and fonts, needs no installation, and touches nothing outside that folder. An optional `install.ps1` in the zip gives the build a Start menu entry without touching the Store install.

## What the patch changes

Two patches are applied. They touch no file in common, because the renderer and the console host are separate.

The RTL patch changes five files, all in the Atlas renderer:

- `src/renderer/atlas/AtlasEngine.cpp`  
  Records a bidi level per cluster while shaping, applies Unicode bidi rule L2 to put the clusters into visual order, emits an odd-level cluster's glyphs back to front, permutes the colour planes into visual space, and maps the caret through the same permutation.
- `src/renderer/atlas/AtlasEngine.h` and `common.h`  
  The `ShapedRow` cluster record, the level range type, and the logical-to-visual column map.
- `src/renderer/atlas/DWriteTextAnalysis.cpp` and `.h`  
  `AnalyzeBidi` is actually called. `AnalyzeScript` on its own never runs the bidi algorithm, so without it every cluster would come back level 0. The sink now collects the resolved levels, and a row's paragraph direction follows the Unicode rules P2/P3: it is right-to-left when the row's *first* strong character is right-to-left.

The complete RTL patch is at:

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

The codepage patch changes one line in `src/host/settings.cpp`. Upstream defaults the console's codepage to the machine's OEM codepage; this build defaults it to UTF-8, so a program encoding Persian to the console codepage does not turn every letter into a `?` on a machine whose locale is not Arabic.

```text
patch/utf8-console-codepage.patch
```

Rows containing no right-to-left cluster at all are left untouched, so lines of only English text or numbers render bit-for-bit as before.

### Only part of the bidi algorithm

Rule L2 is implemented, which is what reorders whole words, and paragraph direction is decided by the Unicode rules P2/P3: a row is right-to-left when its *first* strong character is right-to-left, and left-to-right otherwise. A line that starts with a Latin prompt is therefore a left-to-right paragraph: the prompt keeps its place on the left, and any Persian, Arabic or Hebrew after it is a right-to-left run nested inside it, laid out the way a browser or a text editor lays out mixed content. A line that starts with Persian is a right-to-left paragraph and runs from the right. Where neutral characters such as spaces settle is resolved by DirectWrite from that paragraph direction.

Not implemented here: bracket mirroring and number shaping (L3/L4), so mixed content such as `ABC سلام 123` may put the digits in the wrong place. This is a deliberate stopping point rather than an oversight: each rule has to be correct on its own before it is layered on.

### How it was verified

`tools/bidi-probe` holds small programs that answer questions about the rendering that cannot be answered by looking at a screen. They run in CI and their output is attached to the workflow run as build artifacts.

| probe | question |
|---|---|
| `probe-rtl-layout` | Runs the renderer's own pipeline and checks that reading the emitted cells in the corresponding direction reproduces the input. This is what distinguishes "the words moved" from "the words are wrong". |
| `probe-para-dir` | How a row's paragraph direction is decided. Prints the resolved levels and the final visual order under both P2/P3, which looks at the first strong character, and the older rule that any strong RTL letter is decisive. |
| `probe-glyph-order` | Whether DirectWrite returns the glyphs of an RTL run in logical or visual order. This decides whether rule L2 has to reverse the clusters. |
| `probe-glyph-direction` | Whether DirectWrite reverses them anyway when asked for left to right. |
| `probe-lamalef-real` | Which installed fonts fuse lam-alef, the ligature Arabic script requires. The bundled Cascadia fonts shape positionally but do not fuse lam-alef, which is a property of that font, not of the pipeline. |
| `probe-font-fit` | Which font fits a terminal's cell grid. A terminal forces every shaped cluster to exactly one cell, so a font whose Arabic glyphs are narrower than the cell gets stretched, and that is what reads as a gap between letters. |

They need no Windows Terminal to build:

```text
cl /nologo /EHsc /std:c++20 probe-rtl-layout.cpp /link dwrite.lib
probe-rtl-layout.exe
```

## Building from source

Requirements:

- Visual Studio 2022 or newer with the C++ workload
- The Windows 10 SDK
- Python 3

The Windows Terminal version being patched is specified by `UPSTREAM_REF`. The build script clones it, applies the patches and produces the portable build:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build-portable.ps1 -Arch x64
```

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build-portable.ps1 -Arch arm64
```

Builds are done in CI on `windows-latest`.

### A note on the build system

Two things about building Windows Terminal unpackaged are not obvious and cost a long time to find, so they are recorded in `patch/build-unpackaged.patch`:

- Do **not** pass `-p:WindowsStoreApp=false` or `-p:ApplicationType=`. A `-p:` value is a global property that overrides the project, and those two silently disable the XAML compiler, the generated-files directory, the UAP platform winmds and the include paths for generated XAML sources.
- The build must run on a Visual Studio whose version matches the Windows SDK. An older one pairs its `Microsoft.Build.Tasks.Xaml` task with a newer SDK's compiler, and the settings editor's XAML then fails with `WMC9999: Value cannot be null. Parameter name: name`, which is an internal exception rather than a diagnosable error. This is why CI builds on `windows-latest`.

## Status

This repository is a workaround until right-to-left support is merged into Windows Terminal itself.

## License

Windows Terminal is MIT licensed; the license text is in `LICENSE`.

The binaries in the releases are modified versions of Windows Terminal and remain subject to its license terms.

## Contributors

- AmirAliManzar
- Claude

The RTL patch under `patch/` was written with AI assistance from Claude.

---

## فارسی

این پروژه مشکل نمایش متن‌های راست‌به‌چپ، به‌ویژه فارسی، عربی و عبری، رو در نسخهٔ ویندوز Windows Terminal برطرف می‌کنه.

برای استفاده، نسخهٔ مناسب سیستم خودتون رو از بخش [Releases](../../releases) دانلود کنید. فایل‌های پورتابل نیازی به نصب ندارند، تنظیمات فعلی Windows Terminal شما رو می‌خونن و حتی فونت‌های خودشون رو همراه دارن، پس روی سیستمی که هیچ فونتی نصب نیست هم کار می‌کنن. اگه نصب‌کننده رو ترجیح می‌دید، `WindowsTerminal-RTL-Installer.exe` یه ویزارد کوتاه باز می‌کنه که بیلد مناسب رو دانلود و نصب می‌کنه.

جایگزینی فایل‌های داخل ترمینال نصب‌شده از Store ممکن نیست، چون `C:\Program Files\WindowsApps` با درایور `wcifs` محافظت می‌شه و نوشتن از بیرون کانتینر پکیج رو کلاً مسدود می‌کنه. بیلد پورتابل بالا جایگزین کارآمده.

از ویندوز ۱۰ نسخهٔ ۲۰۰۴ به بعد پشتیبانی می‌شه. این پروژه راه‌حلی موقته تا پشتیبانی کامل از راست‌به‌چپ مستقیماً به Windows Terminal اضافه بشه.

### مجوز

Windows Terminal تحت مجوز MIT منتشر می‌شه و فایل‌های باینری ریلیز، نسخهٔ تغییرکردهٔ اون هستن. متن مجوز تو `LICENSE` موجوده.
