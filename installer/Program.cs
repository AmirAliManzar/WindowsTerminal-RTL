// WindowsTerminal-RTL installer entry point.
//
// Double-clicked, or run with no arguments, it opens a graphical wizard: the
// user picks the architecture and the install folder, watches the download,
// and gets a Start menu shortcut. Run with a flag it stays the original
// console installer, so `--uninstall` and `--install-dir` keep working from a
// shell.
//
// Built with a bare csc.exe (see scripts/build-installer.ps1) so it needs no
// .NET SDK, no MSBuild and no targeting pack: just the .NET Framework that is
// already on every Windows 10/11 box.

using System;
using System.IO;
using System.Reflection;
using System.Text;
using System.Windows.Forms;

namespace WindowsTerminalRtlInstaller
{
    internal static class Program
    {
        private static int Main(string[] args)
        {
            // A /target:winexe process has no console of its own. Inherited from
            // a shell it can write to it; started some other way it cannot, so
            // this is best effort and never fatal.
            try { Console.OutputEncoding = Encoding.UTF8; }
            catch { }

            // A graphical front end needs a single-threaded apartment for the
            // folder dialog and the WScript.Shell COM calls it makes.
            if (args.Length == 0)
            {
                Application.EnableVisualStyles();
                Application.SetCompatibleTextRenderingDefault(false);

                // An unhandled error on the UI thread would otherwise vanish and
                // the user would only see a frozen window. Write the detail to a
                // log file they can attach to a report, and tell them so.
                Application.ThreadException += (s, e) =>
                {
                    string log = null;
                    try
                    {
                        log = Path.Combine(Path.GetTempPath(),
                            "WindowsTerminal-RTL-installer-error.log");
                        File.WriteAllText(log, e.Exception.ToString());
                    }
                    catch { }
                    MessageBox.Show(
                        string.Format(Strings.Get(Strings.CrashBody), log ?? string.Empty),
                        Strings.Get(Strings.CrashTitle),
                        MessageBoxButtons.OK, MessageBoxIcon.Error);
                };
                Application.Run(new WizardForm());
                return 0;
            }

            return ConsoleMain(args);
        }

        // ------------------------------------------------------- console UI

        private static int ConsoleMain(string[] args)
        {
            // Without this the Framework's ServicePointManager negotiates with
            // a legacy protocol list, api.github.com refuses the handshake and
            // the install dies with "Could not create SSL/TLS secure channel"
            // before it has downloaded a single byte. InstallJob sets it too;
            // this keeps the console path working even if that changes.
            System.Net.ServicePointManager.SecurityProtocol =
                System.Net.SecurityProtocolType.Tls12 | System.Net.SecurityProtocolType.Tls11 |
                (System.Net.SecurityProtocolType)0x3000;

            var opts = ParseArgs(args);

            if (opts.Help) { PrintHelp(); return 0; }
            if (opts.ShowVersion)
            {
                Console.WriteLine(InstallJob.AppName + " installer " + AssemblyVersion);
                return 0;
            }

            WriteHeader();

            var job = new InstallJob();
            job.Log += msg => Console.WriteLine("  " + msg);
            job.Progress += (got, total) => DrawProgress(got, total);

            try
            {
                if (opts.Uninstall)
                {
                    job.Uninstall(opts.InstallDir);
                    return 0;
                }

                // Already installed? Report and step aside, so a second run is
                // a no-op rather than a surprise reinstall.
                if (!opts.Force && string.IsNullOrEmpty(opts.InstallDir))
                {
                    string existing = InstallJob.FindInstallDir();
                    if (existing != null)
                    {
                        Console.WriteLine("  " + InstallJob.AppName + " is already installed at " + existing);
                        Console.WriteLine("  re-run with --force to reinstall, or --install-dir to choose elsewhere");
                        Console.WriteLine();
                        Console.WriteLine("  done. Search the Start menu for \"" + InstallJob.AppName + "\".");
                        return 0;
                    }
                }

                var settings = new InstallSettings();
                settings.InstallDir = opts.InstallDir;
                settings.Architecture = opts.Arch;
                settings.StartMenuShortcut = true;
                settings.DesktopShortcut = !opts.NoDesktopShortcut;
                settings.Force = opts.Force;
                job.Install(settings);

                Console.WriteLine();
                Console.WriteLine("  To remove: WindowsTerminal-RTL-Installer.exe --uninstall");
                return 0;
            }
            catch (Exception ex)
            {
                // .Result calls wrap everything in AggregateException; the outer
                // message ("One or more errors occurred") says nothing, so walk
                // down to the one that actually explains the failure.
                Exception real = ex;
                for (var a = ex as AggregateException; a != null && a.InnerException != null; a = real as AggregateException)
                    real = a.InnerException;

                Console.Error.WriteLine();
                Console.Error.WriteLine("  install failed: " + real.Message);
                if (real.InnerException != null && !ReferenceEquals(real, real.InnerException))
                    Console.Error.WriteLine("  because: " + real.InnerException.Message);
                Console.Error.WriteLine();
                Console.Error.WriteLine("  If this is a network problem, re-run. If it keeps failing, open an issue at");
                Console.Error.WriteLine("  https://github.com/" + InstallJob.Owner + "/" + InstallJob.Repo + "/issues and paste the text above.");
                return 1;
            }
        }

        private static void DrawProgress(long received, long total)
        {
            if (total <= 0) return;
            int width = 28;
            int filled = (int)(received * width / total);
            if (filled > width) filled = width;
            string bar = new string('=', filled) + new string('-', width - filled);
            double pct = received * 100.0 / total;
            Console.Write("\r  [" + bar + "] " + pct.ToString("F1").PadLeft(5) + "%  " +
                          InstallJob.FormatBytes(received) + " / " + InstallJob.FormatBytes(total) + "  ");
            if (received >= total) Console.WriteLine();
        }

        // ------------------------------------------------------------- args

        private sealed class Options
        {
            public bool Help;
            public bool ShowVersion;
            public bool Uninstall;
            public bool Force;
            public bool NoDesktopShortcut;
            public string InstallDir;
            public string Arch;
        }

        private static Options ParseArgs(string[] args)
        {
            var o = new Options();
            for (int i = 0; i < args.Length; i++)
            {
                string a = args[i];
                bool hasNext = i + 1 < args.Length;
                switch (a)
                {
                    case "--help":
                    case "-h":
                    case "/?":
                        o.Help = true;
                        break;
                    case "--version":
                        o.ShowVersion = true;
                        break;
                    case "--uninstall":
                        o.Uninstall = true;
                        break;
                    case "--force":
                    case "-force":
                        o.Force = true;
                        break;
                    case "--no-desktop-shortcut":
                        o.NoDesktopShortcut = true;
                        break;
                    case "--arch":
                        if (hasNext) o.Arch = args[++i];
                        break;
                    case "--install-dir":
                        if (hasNext) o.InstallDir = args[++i];
                        break;
                    default:
                        // /install-dir=PATH and -install-dir PATH style
                        foreach (var sep in new[] { "=", ":" })
                        {
                            int n = a.IndexOf(sep, StringComparison.Ordinal);
                            if (n > 0)
                            {
                                string key = a.Substring(0, n);
                                string val = a.Substring(n + sep.Length);
                                if (key.Equals("--install-dir", StringComparison.OrdinalIgnoreCase) ||
                                    key.Equals("/install-dir", StringComparison.OrdinalIgnoreCase) ||
                                    key.Equals("-install-dir", StringComparison.OrdinalIgnoreCase))
                                {
                                    o.InstallDir = val;
                                }
                                else if (key.Equals("--arch", StringComparison.OrdinalIgnoreCase) ||
                                         key.Equals("/arch", StringComparison.OrdinalIgnoreCase))
                                {
                                    o.Arch = val;
                                }
                                break;
                            }
                        }
                        break;
                }
            }
            return o;
        }

        private static void PrintHelp()
        {
            Console.WriteLine(InstallJob.AppName + " installer " + AssemblyVersion);
            Console.WriteLine();
            Console.WriteLine("Run with no arguments to open the install wizard.");
            Console.WriteLine();
            Console.WriteLine("  WindowsTerminal-RTL-Installer.exe              install the latest build (wizard)");
            Console.WriteLine("  WindowsTerminal-RTL-Installer.exe --force      re-download and reinstall");
            Console.WriteLine("  WindowsTerminal-RTL-Installer.exe --uninstall  remove this install");
            Console.WriteLine();
            Console.WriteLine("  --arch x64|arm64         pick the build to install (default: this machine's)");
            Console.WriteLine("  --install-dir <path>     install somewhere other than");
            Console.WriteLine("                           %LOCALAPPDATA%\\Programs\\WindowsTerminal-RTL");
            Console.WriteLine("  --no-desktop-shortcut    Start menu shortcut only");
            Console.WriteLine();
            Console.WriteLine("No admin rights are needed and the Store copy of Windows Terminal is");
            Console.WriteLine("left alone. Your settings at");
            Console.WriteLine("  %LOCALAPPDATA%\\Microsoft\\Windows Terminal\\settings.json");
            Console.WriteLine("are picked up automatically.");
        }

        private static void WriteHeader()
        {
            Console.ForegroundColor = ConsoleColor.Cyan;
            Console.WriteLine("  " + InstallJob.AppName + " installer " + AssemblyVersion);
            Console.ResetColor();
        }

        private static string AssemblyVersion
        {
            get
            {
                var ver = Assembly.GetExecutingAssembly().GetName().Version;
                return ver != null ? ver.ToString(3) : "0.0.0";
            }
        }
    }
}
