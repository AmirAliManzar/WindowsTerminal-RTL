// The install and uninstall machinery, shared by the console entry point and
// the wizard UI.
//
// Everything this class wants to report goes through the Log and Progress
// events so each front end can render it its own way: the console prints lines
// and draws a progress bar, the wizard fills a list and a progress bar.
//
// Built with a bare csc.exe, so this stays inside C# 5 syntax on purpose: no
// expression-bodied members, no string interpolation, no nameof.

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
using System.Web.Script.Serialization;

namespace WindowsTerminalRtlInstaller
{
    internal sealed class InstallSettings
    {
        public string InstallDir;
        public string Architecture;              // "x64" or "arm64"
        public bool StartMenuShortcut = true;
        public bool DesktopShortcut = true;
        public bool Force;                       // reinstall over an existing copy
    }

    internal sealed class InstallJob
    {
        public const string Owner = "AmirAliManzar";
        public const string Repo = "WindowsTerminal-RTL";
        public const string AppName = "Windows Terminal RTL";
        public const string ExeName = "WindowsTerminal.exe";

        private static readonly HttpClient Http = new HttpClient(
            new HttpClientHandler { AllowAutoRedirect = true });

        public event Action<string> Log;
        public event Action<long, long> Progress;   // bytes received, bytes total

        public string LatestTag { get; private set; }

        public InstallJob()
        {
            // Without this the Framework's ServicePointManager negotiates with
            // a legacy protocol list, api.github.com refuses the handshake and
            // the install dies with "Could not create SSL/TLS secure channel"
            // before it has downloaded a single byte. Tls13 is not in the 4.7.2
            // enum, hence the cast; the OS simply ignores it when it is absent.
            ServicePointManager.SecurityProtocol =
                SecurityProtocolType.Tls12 | SecurityProtocolType.Tls11 | (SecurityProtocolType)0x3000;

            Http.DefaultRequestHeaders.UserAgent.ParseAdd(AppName);
            Http.DefaultRequestHeaders.Accept.ParseAdd("application/vnd.github+json");
        }

        public static string DefaultInstallDir()
        {
            return Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                "Programs", "WindowsTerminal-RTL");
        }

        // Where an existing install lives, or null if there is none.
        public static string FindInstallDir()
        {
            string dir = DefaultInstallDir();
            return File.Exists(Path.Combine(dir, ExeName)) ? dir : null;
        }

        public static string DetectArchitecture()
        {
            // PROCESSOR_ARCHITECTURE is ARM64 on Arm Windows, x64 or AMD64 on
            // x64. The build only ships those two assets.
            string proc = Environment.GetEnvironmentVariable("PROCESSOR_ARCHITECTURE");
            if (!string.IsNullOrEmpty(proc) && proc.IndexOf("arm", StringComparison.OrdinalIgnoreCase) >= 0)
                return "arm64";
            return "x64";
        }

        // ------------------------------------------------------------ flow

        public void Install(InstallSettings opts)
        {
            string installDir = string.IsNullOrEmpty(opts.InstallDir)
                ? DefaultInstallDir()
                : Path.GetFullPath(opts.InstallDir);

            string arch = string.IsNullOrEmpty(opts.Architecture)
                ? DetectArchitecture()
                : opts.Architecture;
            string assetName = "WindowsTerminal-RTL-" + arch + ".zip";
            Step("architecture: " + arch);

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

                CreateShortcuts(destExe, installDir, opts.StartMenuShortcut, opts.DesktopShortcut);

                Step("done. Search the Start menu for \"" + AppName + "\".");
                Step("Your settings carry over automatically:");
                Step("  %LOCALAPPDATA%\\Microsoft\\Windows Terminal\\settings.json");
            }
            finally
            {
                TryDelete(temp);
            }
        }

        public void Uninstall(string installDir)
        {
            if (string.IsNullOrEmpty(installDir))
                installDir = DefaultInstallDir();
            else
                installDir = Path.GetFullPath(installDir);

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

            Step("done. Your settings at");
            Step("  %LOCALAPPDATA%\\Microsoft\\Windows Terminal\\settings.json");
            Step("are untouched, and the Store terminal is untouched too.");
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

        public ReleaseInfo FetchLatestReleaseInfo()
        {
            var release = FetchLatestRelease();
            return new ReleaseInfo { Tag = release.Tag };
        }

        private Release FetchLatestRelease()
        {
            string json = DownloadText("https://api.github.com/repos/" + Owner + "/" + Repo + "/releases/latest");
            var ser = new JavaScriptSerializer();
            var root = ser.Deserialize<Dictionary<string, object>>(json);
            if (root == null)
                throw new InvalidOperationException("the GitHub API returned an unreadable response");

            var release = new Release();
            object tag;
            if (root.TryGetValue("tag_name", out tag) && tag != null)
            {
                release.Tag = Convert.ToString(tag);
                LatestTag = release.Tag;
            }

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

        private string DownloadText(string url)
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
        private void Download(string url, string dest, long expectedSize)
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
        private bool TryDownload(string url, string dest, long expectedSize, bool resume)
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
                if (resp.StatusCode == HttpStatusCode.RequestedRangeNotSatisfiable)
                    return expectedSize > 0 && File.Exists(dest) && new FileInfo(dest).Length == expectedSize;

                // If we asked to resume and the server ignored the Range header
                // it sends 200 with the whole body. Appending that to the
                // partial file would corrupt it, so truncate first and treat
                // this as a restart.
                if (startAt > 0 && resp.StatusCode == HttpStatusCode.OK)
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
                        OnProgress(received, expectedSize);
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

        // ------------------------------------------------------------ files

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
        private void CloseRunningInstances(string installDir)
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

        private void CreateShortcuts(string destExe, string installDir, bool startMenu, bool desktop)
        {
            if (!startMenu && !desktop) return;

            // WScript.Shell is the one COM object that ships on every Windows
            // box and writes a .lnk; late binding keeps the build free of an
            // interop assembly.
            Type shellType = Type.GetTypeFromProgID("WScript.Shell");
            if (shellType == null)
                throw new InvalidOperationException("cannot create shortcuts on this system (WScript.Shell missing)");

            var shell = Activator.CreateInstance(shellType);

            if (startMenu)
            {
                string path = Path.Combine(
                    Environment.GetFolderPath(Environment.SpecialFolder.Programs),
                    AppName + ".lnk");
                MakeShortcut(shell, shellType, path, destExe, installDir,
                    "Windows Terminal with right-to-left text rendering");
                Step("shortcut: " + path);
            }

            if (desktop)
            {
                string path = Path.Combine(
                    Environment.GetFolderPath(Environment.SpecialFolder.Desktop),
                    AppName + ".lnk");
                MakeShortcut(shell, shellType, path, destExe, installDir, AppName);
                Step("shortcut: " + path);
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

        public static string FormatBytes(long bytes)
        {
            if (bytes < 1024) return bytes + " B";
            double mb = bytes / 1024.0 / 1024.0;
            if (mb < 1024) return mb.ToString("F1") + " MB";
            return (mb / 1024.0).ToString("F1") + " GB";
        }

        private void Step(string msg)
        {
            if (Log != null) Log(msg);
        }

        private void OnProgress(long received, long total)
        {
            if (Progress != null) Progress(received, total);
        }
    }

    // A tiny view of the release the UI shows on the first page.
    internal sealed class ReleaseInfo
    {
        public string Tag;
    }
}
