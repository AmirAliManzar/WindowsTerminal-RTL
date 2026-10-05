// WindowsTerminal-RTL installer.
//
// A real executable rather than a PowerShell script. It works out this
// machine's architecture, downloads the matching portable build from the
// Releases page, checks its sha256 against the sidecar, and installs it to
// %LOCALAPPDATA%\Programs\WindowsTerminal-RTL with Start menu and desktop
// shortcuts.
//
// Nothing needs admin rights, the Store terminal is untouched, and the
// portable build reads %LOCALAPPDATA%\Microsoft\Windows Terminal\settings.json
// so existing profiles, themes and fonts carry over with no import step.
//
// Built with a bare csc.exe (see scripts/build-installer.ps1) so it needs no
// .NET SDK, no MSBuild and no targeting pack: just the .NET Framework that is
// already on every Windows 10/11 box.

using System;
using System.Collections;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Net;
using System.Net.Http;
using System.Net.Http.Headers;
using System.Reflection;
using System.Security.Cryptography;
using System.Text;
using System.Threading.Tasks;
using System.Web.Script.Serialization;

namespace WindowsTerminalRtlInstaller
{
    internal static class Program
    {
        private const string Owner = "AmirAliManzar";
        private const string Repo = "WindowsTerminal-RTL";
        private const string AppName = "Windows Terminal RTL";
        private const string ExeName = "WindowsTerminal.exe";

        private static readonly HttpClient Http = new HttpClient(
            new HttpClientHandler { AllowAutoRedirect = true });

        // An expression-bodied property is C# 6, and the csc.exe that ships in
        // the Framework directory only knows C# 5, so this is a method.
        private static string DefaultInstallDir()
        {
            return Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                "Programs", "WindowsTerminal-RTL");
        }

        private static int Main(string[] args)
        {
            Console.OutputEncoding = Encoding.UTF8;

            // Without this the Framework's ServicePointManager negotiates with
            // a legacy protocol list, api.github.com refuses the handshake and
            // the install dies with "Could not create SSL/TLS secure channel"
            // before it has downloaded a single byte. Tls13 is not in the 4.7.2
            // enum, hence the cast; the OS simply ignores it when it is absent.
            ServicePointManager.SecurityProtocol =
                SecurityProtocolType.Tls12 | SecurityProtocolType.Tls11 | (SecurityProtocolType)0x3000;

            Http.DefaultRequestHeaders.UserAgent.ParseAdd(AppName);
            Http.DefaultRequestHeaders.Accept.ParseAdd("application/vnd.github+json");

            var opts = ParseArgs(args);

            if (opts.Help)
            {
                PrintHelp();
                return 0;
            }
            if (opts.ShowVersion)
            {
                Console.WriteLine(AppName + " installer " + AssemblyVersion);
                return 0;
            }

            WriteHeader();

            try
            {
                if (opts.Uninstall)
                {
                    Uninstall(opts);
                    return 0;
                }

                Install(opts);
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
                Console.Error.WriteLine("  https://github.com/" + Owner + "/" + Repo + "/issues and paste the text above.");
                return 1;
            }
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
                    case "--install-dir":
                        if (hasNext) o.InstallDir = args[++i];
                        break;
                    default:
                        // /install-dir=... and -install-dir ... style
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
            Console.WriteLine(AppName + " installer " + AssemblyVersion);
            Console.WriteLine();
            Console.WriteLine("Downloads the latest release and installs it for the current user.");
            Console.WriteLine();
            Console.WriteLine("  WindowsTerminal-RTL-Installer.exe              install the latest build");
            Console.WriteLine("  WindowsTerminal-RTL-Installer.exe --force      re-download and reinstall");
            Console.WriteLine("  WindowsTerminal-RTL-Installer.exe --uninstall  remove this install");
            Console.WriteLine();
            Console.WriteLine("  --install-dir <path>     install somewhere other than");
            Console.WriteLine("                           %LOCALAPPDATA%\\Programs\\WindowsTerminal-RTL");
            Console.WriteLine("  --no-desktop-shortcut    Start menu shortcut only");
            Console.WriteLine();
            Console.WriteLine("No admin rights are needed and the Store copy of Windows Terminal is");
            Console.WriteLine("left alone. Your settings at");
            Console.WriteLine("  %LOCALAPPDATA%\\Microsoft\\Windows Terminal\\settings.json");
            Console.WriteLine("are picked up automatically.");
        }

        // ------------------------------------------------------------- flow

        private static void Install(Options opts)
        {
            string installDir = string.IsNullOrEmpty(opts.InstallDir)
                ? DefaultInstallDir()
                : Path.GetFullPath(opts.InstallDir);

            string arch = DetectArchitecture();
            string assetName = "WindowsTerminal-RTL-" + arch + ".zip";
            Step("architecture: " + arch);

            // Already installed? Report and step aside, exactly like the old
            // script did, so a double-click on a second download is a no-op
            // rather than a surprise reinstall.
            if (!opts.Force && string.IsNullOrEmpty(opts.InstallDir))
            {
                string existing = Path.Combine(DefaultInstallDir(), ExeName);
                if (File.Exists(existing))
                {
                    Step(AppName + " is already installed at " + DefaultInstallDir());
                    Step("re-run with --force to reinstall, or --install-dir to choose elsewhere");
                    Console.WriteLine();
                    Console.WriteLine("  done. Search the Start menu for \"" + AppName + "\".");
                    return;
                }
            }

            Step("finding the latest release");
            var release = FetchLatestRelease();
            Step("latest release: " + release.Tag);

            var asset = release.Assets.FirstOrDefault(x => string.Equals(x.Name, assetName, StringComparison.OrdinalIgnoreCase));
            if (asset == null)
                throw new InvalidOperationException("release " + release.Tag + " has no " + assetName);

            string temp = Path.Combine(Path.GetTempPath(), "wt-rtl-install-" + Guid.NewGuid().ToString("N"));
            Directory.CreateDirectory(temp);
            try
            {
                string zip = Path.Combine(temp, assetName);
                Step("downloading " + assetName + " (" + FormatBytes(asset.Size) + ")");
                Download(asset.Url, zip, asset.Size);

                string expected = null;
                var sumAsset = release.Assets.FirstOrDefault(x => string.Equals(x.Name, assetName + ".sha256", StringComparison.OrdinalIgnoreCase));
                if (sumAsset != null)
                    expected = DownloadText(sumAsset.Url).Trim();
                if (!string.IsNullOrEmpty(expected))
                {
                    string[] parts = expected.Split(new[] { ' ', '\t', '\r', '\n' }, StringSplitOptions.RemoveEmptyEntries);
                    string want = parts[0].ToLowerInvariant();
                    string got = Sha256(zip);
                    if (want != got)
                        throw new InvalidOperationException("sha256 mismatch: the download is corrupt or was tampered with");
                    Step("sha256 verified");
                }

                Step("extracting");
                string extractRoot = Path.Combine(temp, "extract");
                ZipFile.ExtractToDirectory(zip, extractRoot);

                // The zip has the build at its root. If it extracted into a
                // single subfolder instead, step into that folder.
                string source = extractRoot;
                string[] candidates = Directory.GetDirectories(extractRoot);
                if (!File.Exists(Path.Combine(source, ExeName)) && candidates.Length == 1)
                    source = candidates[0];
                if (!File.Exists(Path.Combine(source, ExeName)))
                    throw new InvalidOperationException("the zip has no " + ExeName + " at its root");

                Step("installing to " + installDir);
                CloseRunningInstances(installDir);
                PrepareInstallDir(installDir);
                CopyTree(source, installDir);

                string destExe = Path.Combine(installDir, ExeName);
                if (!File.Exists(destExe))
                    throw new InvalidOperationException("the copy did not land " + ExeName + " in " + installDir);

                int count = Directory.EnumerateFiles(installDir, "*", SearchOption.AllDirectories).Count();
                Step("copied " + count + " files");

                CreateShortcuts(destExe, installDir, opts.NoDesktopShortcut);

                Console.WriteLine();
                Console.WriteLine("  done. Search the Start menu for \"" + AppName + "\".");
                Console.WriteLine("  Your settings carry over automatically:");
                Console.WriteLine("    %LOCALAPPDATA%\\Microsoft\\Windows Terminal\\settings.json");
                Console.WriteLine("  To remove: WindowsTerminal-RTL-Installer.exe --uninstall");
            }
            finally
            {
                TryDelete(temp);
            }
        }

        private static void Uninstall(Options opts)
        {
            string installDir = string.IsNullOrEmpty(opts.InstallDir)
                ? DefaultInstallDir()
                : Path.GetFullPath(opts.InstallDir);

            Step("uninstalling " + AppName);

            CloseRunningInstances(installDir);

            string startMenu = Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.Programs),
                AppName + ".lnk");
            string desktop = Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.Desktop),
                AppName + ".lnk");

            foreach (string lnk in new[] { startMenu, desktop })
            {
                if (File.Exists(lnk))
                {
                    TryDelete(lnk);
                    Step("removed " + lnk);
                }
            }

            if (Directory.Exists(installDir))
            {
                TryDelete(installDir);
                Step("removed " + installDir);
            }
            else
            {
                Step(installDir + " not present, nothing to remove");
            }

            Console.WriteLine();
            Console.WriteLine("  done. Your settings at");
            Console.WriteLine("    %LOCALAPPDATA%\\Microsoft\\Windows Terminal\\settings.json");
            Console.WriteLine("  are untouched, and the Store terminal is untouched too.");
        }

        // --------------------------------------------------------- network

        private sealed class Release
        {
            public string Tag;
            public readonly List<Asset> Assets = new List<Asset>();
        }

        private sealed class Asset
        {
            public string Name;
            public string Url;
            public long Size;
        }

        private static Release FetchLatestRelease()
        {
            string json = DownloadText("https://api.github.com/repos/" + Owner + "/" + Repo + "/releases/latest");
            var ser = new JavaScriptSerializer();
            var root = ser.Deserialize<Dictionary<string, object>>(json);
            if (root == null)
                throw new InvalidOperationException("the GitHub API returned an unreadable response");

            var release = new Release();
            object tag;
            if (root.TryGetValue("tag_name", out tag) && tag != null)
                release.Tag = Convert.ToString(tag);

            object assetsObj;
            if (!root.TryGetValue("assets", out assetsObj) || assetsObj == null)
                throw new InvalidOperationException("release " + release.Tag + " has no assets");

            foreach (var item in (IEnumerable)assetsObj)
            {
                var dict = item as Dictionary<string, object>;
                if (dict == null) continue;
                var asset = new Asset();
                object name, url, size;
                if (dict.TryGetValue("name", out name)) asset.Name = Convert.ToString(name);
                if (dict.TryGetValue("browser_download_url", out url)) asset.Url = Convert.ToString(url);
                if (dict.TryGetValue("size", out size)) asset.Size = Convert.ToInt64(size);
                if (!string.IsNullOrEmpty(asset.Name) && !string.IsNullOrEmpty(asset.Url))
                    release.Assets.Add(asset);
            }
            return release;
        }

        private static string DownloadText(string url)
        {
            using (var resp = Http.GetAsync(url).Result)
            {
                resp.EnsureSuccessStatusCode();
                return resp.Content.ReadAsStringAsync().Result;
            }
        }

        // The CI archive this downloads is a 36 MB object behind a redirect,
        // and on a flaky connection the stream dies every few MB. Failing the
        // whole install over that is worse than picking up where it left off,
        // so this resumes from the partial file rather than starting over.
        private static void Download(string url, string dest, long expectedSize)
        {
            const int maxAttempts = 6;
            for (int attempt = 1; attempt <= maxAttempts; attempt++)
            {
                if (TryDownload(url, dest, expectedSize, resume: attempt > 1)) return;
                if (attempt < maxAttempts)
                {
                    long have = File.Exists(dest) ? new FileInfo(dest).Length : 0;
                    Step("download interrupted at " + FormatBytes(have) + ", resuming (attempt " + (attempt + 1) + ")");
                }
            }
            throw new InvalidOperationException("could not download " + url + " after " + maxAttempts + " attempts");
        }

        // A Range header asking for everything after the bytes already on disk.
        // GitHub's CDN honours it and returns 206; if the server ignores the
        // range and sends 200 anyway the file is restarted from zero, which is
        // still correct, just slower.
        private static bool TryDownload(string url, string dest, long expectedSize, bool resume)
        {
            long startAt = 0;
            if (resume && File.Exists(dest))
                startAt = new FileInfo(dest).Length;

            var request = new HttpRequestMessage(HttpMethod.Get, url);
            if (startAt > 0)
                request.Headers.Range = new RangeHeaderValue(startAt, null);

            using (var resp = Http.SendAsync(request, HttpCompletionOption.ResponseHeadersRead).Result)
            {
                // 416 Range Not Satisfiable: the file is already complete, or
                // the server cannot serve the requested range. Either way this
                // is not a retryable failure.
                if (resp.StatusCode == System.Net.HttpStatusCode.RequestedRangeNotSatisfiable)
                    return expectedSize > 0 && File.Exists(dest) && new FileInfo(dest).Length == expectedSize;

                // If we asked to resume and the server ignored the Range header
                // it sends 200 with the whole body. Appending that to the
                // partial file would corrupt it, so truncate first and treat
                // this as a restart.
                if (startAt > 0 && resp.StatusCode == System.Net.HttpStatusCode.OK)
                {
                    using (var truncate = new FileStream(dest, FileMode.Truncate, FileAccess.Write)) { }
                    startAt = 0;
                }

                resp.EnsureSuccessStatusCode();
                using (var src = resp.Content.ReadAsStreamAsync().Result)
                using (var dst = new FileStream(dest, FileMode.Append, FileAccess.Write))
                {
                    byte[] buffer = new byte[1024 * 64];
                    long received = startAt;
                    int n;
                    while ((n = src.Read(buffer, 0, buffer.Length)) > 0)
                    {
                        dst.Write(buffer, 0, n);
                        received += n;
                        DrawProgress(received, expectedSize);
                    }
                }
            }
            var info = new FileInfo(dest);
            if (expectedSize > 0 && info.Length != expectedSize)
            {
                Step("got " + info.Length + " bytes, expected " + expectedSize);
                return false;
            }
            return info.Length > 0;
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
                          FormatBytes(received) + " / " + FormatBytes(total) + "  ");
            if (received >= total) Console.WriteLine();
        }

        // ------------------------------------------------------------ files

        private static string DetectArchitecture()
        {
            // Same test the PowerShell installer used. PROCESSOR_ARCHITECTURE is
            // ARM64 on Arm Windows, x64 or AMD64 on x64, and the build only
            // ships those two assets.
            string proc = Environment.GetEnvironmentVariable("PROCESSOR_ARCHITECTURE");
            if (!string.IsNullOrEmpty(proc) && proc.IndexOf("arm", StringComparison.OrdinalIgnoreCase) >= 0)
                return "arm64";
            return "x64";
        }

        private static string Sha256(string path)
        {
            using (var sha = SHA256.Create())
            using (var stream = File.OpenRead(path))
            {
                byte[] hash = sha.ComputeHash(stream);
                var sb = new StringBuilder(hash.Length * 2);
                foreach (byte b in hash) sb.Append(b.ToString("x2"));
                return sb.ToString();
            }
        }

        // Close any instance of this install that is still running, otherwise
        // the exe and DLLs it holds are locked and the copy below fails with a
        // "file in use" error. Anything outside installDir is left alone.
        private static void CloseRunningInstances(string installDir)
        {
            foreach (var proc in Process.GetProcessesByName("WindowsTerminal"))
            {
                try
                {
                    string path = proc.MainModule.FileName;
                    if (path != null && path.StartsWith(installDir, StringComparison.OrdinalIgnoreCase))
                    {
                        Step("closing running " + AppName + " (pid " + proc.Id + ")");
                        proc.Kill();
                        proc.WaitForExit(8000);
                    }
                }
                catch
                {
                    // No permission to read this process's module, or it is not
                    // ours. Either way it is not something to kill.
                }
            }
        }

        private static void PrepareInstallDir(string installDir)
        {
            if (Directory.Exists(installDir))
            {
                // Overwrite in place so a pinned shortcut keeps pointing at the
                // same exe path.
                TryDelete(installDir);
            }
            Directory.CreateDirectory(installDir);
        }

        private static void CopyTree(string source, string dest)
        {
            foreach (string file in Directory.EnumerateFiles(source, "*", SearchOption.AllDirectories))
            {
                string rel = file.Substring(source.Length).TrimStart(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
                string target = Path.Combine(dest, rel);
                Directory.CreateDirectory(Path.GetDirectoryName(target));
                File.Copy(file, target, overwrite: true);
            }
        }

        private static void CreateShortcuts(string destExe, string installDir, bool skipDesktop)
        {
            // WScript.Shell is the one COM object that ships on every Windows
            // box and writes a .lnk; late binding keeps the build free of an
            // interop assembly.
            Type shellType = Type.GetTypeFromProgID("WScript.Shell");
            if (shellType == null)
                throw new InvalidOperationException("cannot create shortcuts on this system (WScript.Shell missing)");

            var shell = Activator.CreateInstance(shellType);

            string startMenu = Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.Programs),
                AppName + ".lnk");
            MakeShortcut(shell, shellType, startMenu, destExe, installDir,
                "Windows Terminal with right-to-left text rendering");
            Step("shortcut: " + startMenu);

            if (!skipDesktop)
            {
                string desktop = Path.Combine(
                    Environment.GetFolderPath(Environment.SpecialFolder.Desktop),
                    AppName + ".lnk");
                MakeShortcut(shell, shellType, desktop, destExe, installDir, AppName);
                Step("shortcut: " + desktop);
            }
        }

        private static void MakeShortcut(object shell, Type shellType, string path, string targetPath,
                                         string workingDir, string description)
        {
            object lnk = shellType.InvokeMember("CreateShortcut",
                BindingFlags.InvokeMethod | BindingFlags.GetProperty, null, shell, new object[] { path });
            Type lnkType = lnk.GetType();
            lnkType.InvokeMember("TargetPath", BindingFlags.SetProperty, null, lnk, new object[] { targetPath });
            lnkType.InvokeMember("WorkingDirectory", BindingFlags.SetProperty, null, lnk, new object[] { workingDir });
            lnkType.InvokeMember("IconLocation", BindingFlags.SetProperty, null, lnk, new object[] { targetPath + ",0" });
            lnkType.InvokeMember("Description", BindingFlags.SetProperty, null, lnk, new object[] { description });
            lnkType.InvokeMember("Save", BindingFlags.InvokeMethod, null, lnk, null);
        }

        // ------------------------------------------------------------ utils

        private static void TryDelete(string path)
        {
            try
            {
                if (File.Exists(path)) File.Delete(path);
                if (Directory.Exists(path)) Directory.Delete(path, recursive: true);
            }
            catch
            {
                // Best effort: a locked file left behind is not worth failing
                // an otherwise complete install over.
            }
        }

        private static string FormatBytes(long bytes)
        {
            if (bytes < 1024) return bytes + " B";
            double mb = bytes / 1024.0 / 1024.0;
            if (mb < 1024) return mb.ToString("F1") + " MB";
            return (mb / 1024.0).ToString("F1") + " GB";
        }

        private static void WriteHeader()
        {
            Console.ForegroundColor = ConsoleColor.Cyan;
            Console.WriteLine("  " + AppName + " installer " + AssemblyVersion);
            Console.ResetColor();
        }

        private static void Step(string msg)
        {
            Console.WriteLine("  " + msg);
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
