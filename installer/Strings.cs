// Bilingual UI strings for the installer.
//
// Two tables, English and Persian. The active language is chosen once at
// startup from the Windows UI language and can then be flipped at runtime by
// the language button in the header, which is why every control reads its
// text through here instead of holding a literal.
//
// C# 5 on purpose: the Framework csc.exe is the C# 5 compiler.

using System;
using System.Collections.Generic;
using System.Globalization;

namespace WindowsTerminalRtlInstaller
{
    internal enum AppLanguage { English, Persian }

    internal static class Strings
    {
        // -------------------------------------------------------------- keys
        public const string
            FormTitle          = "FormTitle",
            HeaderTitle        = "HeaderTitle",
            HeaderSub          = "HeaderSub",
            WelcomeTitle       = "WelcomeTitle",
            WelcomeBody        = "WelcomeBody",
            InstalledAt        = "InstalledAt",
            NotInstalled       = "NotInstalled",
            Uninstall          = "Uninstall",
            ArchTitle          = "ArchTitle",
            ArchAuto           = "ArchAuto",
            ArchX64            = "ArchX64",
            ArchArm64          = "ArchArm64",
            ArchNote           = "ArchNote",
            CheckingRelease    = "CheckingRelease",
            LatestRelease      = "LatestRelease",
            ReleaseUnavailable = "ReleaseUnavailable",
            DestTitle          = "DestTitle",
            Browse             = "Browse",
            DestNote           = "DestNote",
            DestNoteExisting   = "DestNoteExisting",
            OptionsTitle       = "OptionsTitle",
            ChkStart           = "ChkStart",
            ChkDesktop         = "ChkDesktop",
            OptionsNote        = "OptionsNote",
            StatusPreparing    = "StatusPreparing",
            StatusDownloading  = "StatusDownloading",
            StatusUninstalling = "StatusUninstalling",
            Back               = "Back",
            Next               = "Next",
            Cancel             = "Cancel",
            Install            = "Install",
            Finish             = "Finish",
            Retry              = "Retry",
            Launch             = "Launch",
            DoneTitleOk        = "DoneTitleOk",
            DoneBodyOk         = "DoneBodyOk",
            DoneTitleOkRemove  = "DoneTitleOkRemove",
            DoneBodyOkRemove   = "DoneBodyOkRemove",
            DoneTitleFail      = "DoneTitleFail",
            DoneBodyFailPrefix = "DoneBodyFailPrefix",
            Cause              = "Cause",
            UnknownError       = "UnknownError",
            RetryHint          = "RetryHint",
            IssuesSuffix       = "IssuesSuffix",
            FolderDialog       = "FolderDialog",
            BytesReceived      = "BytesReceived",
            ProgressSeparator  = "ProgressSeparator",
            CrashTitle         = "CrashTitle",
            CrashBody          = "CrashBody";

        // ------------------------------------------------------------ tables
        private static readonly Dictionary<string, string> English =
            new Dictionary<string, string>
        {
            { FormTitle,          "Windows Terminal RTL installer" },
            { HeaderTitle,        "Windows Terminal RTL" },
            { HeaderSub,          "Portable build with right-to-left rendering   " },
            { WelcomeTitle,       "Welcome to the Windows Terminal RTL installer" },
            { WelcomeBody,
                "This downloads a portable build of Windows Terminal that renders " +
                "Persian and other right-to-left languages correctly.\n\n" +
                "Your existing settings, profiles and fonts are picked up " +
                "automatically from:\n" +
                "%LOCALAPPDATA%\\Microsoft\\Windows Terminal\\settings.json\n\n" +
                "No administrator rights are required, and the Store version of " +
                "Windows Terminal is left untouched." },
            { InstalledAt,        "Installed at:  " },
            { NotInstalled,       "Not installed yet." },
            { Uninstall,          "Uninstall" },
            { ArchTitle,          "Download architecture" },
            { ArchAuto,           "Automatic (match this computer, recommended)" },
            { ArchX64,            "x64, Intel / AMD" },
            { ArchArm64,          "arm64, Windows on ARM" },
            { ArchNote,           "Leave it on Automatic if you are not sure which one to pick." },
            { CheckingRelease,    "Checking the latest release" },
            { LatestRelease,      "Latest release: " },
            { ReleaseUnavailable, "Could not reach the release feed, the wizard will try again during install." },
            { DestTitle,          "Install location" },
            { Browse,             "Browse" },
            { DestNote,           "Installs for the current user, no administrator rights needed." },
            { DestNoteExisting,   "An existing install was found in this folder and will be replaced.\nYour settings are left untouched." },
            { OptionsTitle,       "Shortcuts" },
            { ChkStart,           "Create a Start menu shortcut" },
            { ChkDesktop,         "Create a desktop shortcut" },
            { OptionsNote,        "These shortcuts can be removed later from the app, or with Uninstall." },
            { StatusPreparing,    "Preparing" },
            { StatusDownloading,  "Downloading" },
            { StatusUninstalling, "Uninstalling" },
            { Back,               "Back" },
            { Next,               "Next" },
            { Cancel,             "Cancel" },
            { Install,            "Install" },
            { Finish,             "Finish" },
            { Retry,              "Retry" },
            { Launch,             "Launch Terminal" },
            { DoneTitleOk,        "Installation finished" },
            { DoneBodyOk,
                "Windows Terminal RTL is installed.\n\n" +
                "Search the Start menu, or use the desktop shortcut, to open it.\n" +
                "Your settings were picked up automatically.\n\n" +
                "To remove it later, open this installer again and choose Uninstall." },
            { DoneTitleOkRemove,  "Uninstall finished" },
            { DoneBodyOkRemove,   "Windows Terminal RTL was removed.\nYour settings were left untouched." },
            { DoneTitleFail,      "Installation failed" },
            { DoneBodyFailPrefix, "The install could not finish:\n" },
            { Cause,              "\n\nCause: " },
            { UnknownError,       "Unknown error" },
            { RetryHint,          "\n\nIf this was a network problem, choose Retry.\nIf it keeps failing, please report it at\n" },
            { IssuesSuffix,       "" },
            { FolderDialog,       "Choose the folder to install Windows Terminal RTL into" },
            { BytesReceived,      " received" },
            { ProgressSeparator,  " / " },
            { CrashTitle,         "Something went wrong" },
            { CrashBody,
                "The installer hit an unexpected error and had to stop.\n\n" +
                "The details were written to:\n" +
                "{0}\n\n" +
                "Please report this at\n" +
                "https://github.com/" + InstallJob.Owner + "/" + InstallJob.Repo + "/issues" },
        };

        private static readonly Dictionary<string, string> Persian =
            new Dictionary<string, string>
        {
            { FormTitle,          "نصاب Windows Terminal RTL" },
            { HeaderTitle,        "Windows Terminal RTL" },
            { HeaderSub,          "نسخه قابل حمل با رندر راست به چپ   " },
            { WelcomeTitle,       "به نصاب Windows Terminal RTL خوش آمدید" },
            { WelcomeBody,
                "این برنامه یک نسخه قابل حمل از Windows Terminal دریافت میکند که " +
                "متن فارسی و سایر زبانهای راست به چپ را درست رندر میکند.\n\n" +
                "تنظیمات، پروفایلها و فونتهای فعلی شما به طور خودکار از این مسیر " +
                "به کار گرفته میشوند:\n" +
                "%LOCALAPPDATA%\\Microsoft\\Windows Terminal\\settings.json\n\n" +
                "نیازی به دسترسی مدیر نیست و نسخه Microsoft Store دست نخورده میماند." },
            { InstalledAt,        "نصب شده در:  " },
            { NotInstalled,       "هنوز نصب نشده است." },
            { Uninstall,          "حذف نصب" },
            { ArchTitle,          "معماری دانلود" },
            { ArchAuto,           "خودکار (بر اساس این کامپیوتر، پیشنهادی)" },
            { ArchX64,            "x64، Intel / AMD" },
            { ArchArm64,          "arm64، Windows روی ARM" },
            { ArchNote,           "اگر نمیدانید کدام است، حالت خودکار را رها کنید." },
            { CheckingRelease,    "در حال بررسی آخرین نسخه" },
            { LatestRelease,      "آخرین نسخه منتشر شده: " },
            { ReleaseUnavailable, "دسترسی به نسخه منتشر شده مقدور نیست، هنگام نصب دوباره تلاش میشود." },
            { DestTitle,          "محل نصب" },
            { Browse,             "انتخاب" },
            { DestNote,           "برای حساب کاربری فعلی نصب میشود و نیازی به دسترسی مدیر ندارد." },
            { DestNoteExisting,   "در این مسیر نصب موجودی پیدا شد و با نصب دوباره جایگزین میشود.\nتنظیمات شما دست نخورده میماند." },
            { OptionsTitle,       "میانبرها" },
            { ChkStart,           "ایجاد میانبر در منوی Start" },
            { ChkDesktop,         "ایجاد میانبر روی دسکتاپ" },
            { OptionsNote,        "حذف این میانبرها بعدا از داخل خود برنامه یا با حذف نصب ممکن است." },
            { StatusPreparing,    "آماده سازی" },
            { StatusDownloading,  "در حال دانلود" },
            { StatusUninstalling, "در حال حذف نصب" },
            { Back,               "برگشت" },
            { Next,               "بعدی" },
            { Cancel,             "انصراف" },
            { Install,            "نصب" },
            { Finish,             "پایان" },
            { Retry,              "تلاش دوباره" },
            { Launch,             "اجرای ترمینال" },
            { DoneTitleOk,        "نصب با موفقیت انجام شد" },
            { DoneBodyOk,
                "Windows Terminal RTL نصب شد.\n\n" +
                "برای اجرا در منوی Start جستجو کنید یا از میانبر دسکتاپ استفاده کنید.\n" +
                "تنظیمات شما به طور خودکار به کار گرفته شد.\n\n" +
                "برای حذف نصب بعدا این برنامه را دوباره باز کنید و حذف نصب را بزنید." },
            { DoneTitleOkRemove,  "حذف نصب انجام شد" },
            { DoneBodyOkRemove,   "Windows Terminal RTL حذف شد.\nتنظیمات شما دست نخورده ماند." },
            { DoneTitleFail,      "نصب ناموفق بود" },
            { DoneBodyFailPrefix, "نصب ناتمام ماند:\n" },
            { Cause,              "\n\nعلت: " },
            { UnknownError,       "خطای ناشناخته" },
            { RetryHint,          "\n\nاگر مشکل شبکه بود تلاش دوباره را بزنید.\nاگر ادامه داشت در\n" },
            { IssuesSuffix,       " مطرح کنید" },
            { FolderDialog,       "پوشه نصب Windows Terminal RTL را انتخاب کنید" },
            { BytesReceived,      " دریافت شد" },
            { ProgressSeparator,  " / " },
            { CrashTitle,         "خطایی رخ داد" },
            { CrashBody,
                "نصاب با خطای پیشبینی نشدهای مواجه شد و متوقف شد.\n\n" +
                "جزئیات در این مسیر نوشته شد:\n" +
                "{0}\n\n" +
                "لطفا این مشکل را در\n" +
                "https://github.com/" + InstallJob.Owner + "/" + InstallJob.Repo + "/issues" +
                "\n مطرح کنید" },
        };

        // ------------------------------------------------------------ state
        public static AppLanguage Current { get; private set; }

        // Right-to-left everything lays out the Persian way.
        public static bool Rtl { get { return Current == AppLanguage.Persian; } }

        // The label on the toggle button is the name of the language you would
        // switch to, not the one you are in.
        public static string OtherName
        {
            get { return Current == AppLanguage.Persian ? "English" : "فارسی"; }
        }

        public static void Set(AppLanguage lang)
        {
            Current = lang;
        }

        // Persian only when Windows itself is Persian; every other language
        // gets English.
        public static AppLanguage Detect()
        {
            try
            {
                var c = CultureInfo.CurrentUICulture;
                if (c != null && c.TwoLetterISOLanguageName == "fa")
                    return AppLanguage.Persian;
            }
            catch { }
            return AppLanguage.English;
        }

        public static string Get(string key)
        {
            var table = Current == AppLanguage.Persian ? Persian : English;
            string value;
            return table.TryGetValue(key, out value) ? value : key;
        }
    }
}
