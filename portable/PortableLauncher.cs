// Self-extracting launcher for the portable Windows Terminal build.
//
// The entire portable folder, the terminal, every DLL it loads and the Cascadia
// fonts it renders with, is embedded inside this EXE as payload.zip. One file
// is the whole product: copy it anywhere and run it. The first run extracts the
// payload into a per-user folder and starts the terminal. Every run after that
// is instant, because the payload is already on disk and the marker still
// matches.
//
// The marker carries the build version, the architecture and a short hash of the
// payload. A new release therefore lands in its own folder and the previous one
// is deleted, and the two never mix; a re-shipped build of the same version
// re-extracts, because the hash no longer matches. Extraction goes into a
// staging folder that is moved into place with a single call, so a crash halfway
// through cannot leave a half-populated install behind.
//
// Nothing is installed, nothing is registered, no font is added to the system
// and no administrator rights are asked for. That is what makes this independent
// of the machine it lands on: the fonts the terminal needs ride along inside the
// payload, and the terminal loads them from its own folder before it ever asks
// the system for a typeface.
//
// Errors reach the user through user32 MessageBox directly rather than
// System.Windows.Forms, so this launcher never depends on the desktop parts of
// the framework. The only reference beyond the base class library is
// System.IO.Compression, which has shipped inside the framework since 4.5.
//
// Compiled with a bare csc.exe (see scripts/build-portable-sfx.ps1), which means
// the C# 5 compiler, so this source deliberately stays inside C# 5 syntax.

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.IO.Compression;
using System.Reflection;
using System.Runtime.InteropServices;

namespace WindowsTerminalRtlPortable
{
    internal static class PortableLauncher
    {
        private const string AppFolderName = "WindowsTerminal-RTL-Portable";
        private const string ExeName = "WindowsTerminal.exe";
        private const string MarkerFileName = ".portable-marker";

        private const uint MbOk = 0x00000000;
        private const uint MbIconError = 0x00000010;

        [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern int MessageBox(IntPtr hWnd, string text, string caption, uint type);

        private static int Main(string[] args)
        {
            try
            {
                return Run(args);
            }
            catch (Exception ex)
            {
                // A winexe has no console to write to, so an unhandled exception
                // here would vanish without a trace. Surfacing it in a message
                // box is the difference between "nothing happened" and a problem
                // the user can actually describe.
                MessageBox(IntPtr.Zero, ex.Message, "Windows Terminal RTL", MbOk | MbIconError);
                return 1;
            }
        }

        private static int Run(string[] args)
        {
            Assembly assembly = Assembly.GetExecutingAssembly();

            string marker = ReadStringResource(assembly, "marker.txt");
            if (string.IsNullOrEmpty(marker))
            {
                throw new InvalidOperationException(
                    "This launcher has no version marker embedded in it, so it cannot tell a fresh install from an existing one.");
            }
            marker = marker.Trim();

            string[] parts = marker.Split(new char[] { ' ', '\t', '\r', '\n' }, StringSplitOptions.RemoveEmptyEntries);
            string version = parts.Length > 0 ? parts[0] : string.Empty;
            string architecture = parts.Length > 1 ? parts[1] : "x64";
            if (version.Length == 0)
            {
                throw new InvalidOperationException("The version marker inside this launcher is malformed: " + marker);
            }

            string baseDirectory = Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                "Programs",
                AppFolderName);
            string targetDirectory = Path.Combine(baseDirectory, version + "-" + architecture);
            string markerFile = Path.Combine(targetDirectory, MarkerFileName);

            // The payload for this exact marker is already on disk, so skip the
            // extraction and start the terminal. This is what makes the second
            // run and every run after it instant.
            if (Directory.Exists(targetDirectory)
                && File.Exists(markerFile)
                && ReadAllText(markerFile).Trim() == marker)
            {
                return Launch(targetDirectory, args);
            }

            byte[] payload = ReadAllBytesResource(assembly, "payload.zip");
            if (payload == null || payload.Length == 0)
            {
                throw new InvalidOperationException(
                    "This launcher has no payload embedded in it, so it has nothing to install.");
            }

            Directory.CreateDirectory(baseDirectory);

            // Extract into a staging folder first and move it into place with one
            // call. A crash while extracting leaves staging garbage behind, but
            // never a target directory that is only half written.
            string stagingDirectory = Path.Combine(
                baseDirectory,
                version + "-" + architecture + ".staging-" + Path.GetRandomFileName());
            DeleteDirectoryIfExists(stagingDirectory);
            Directory.CreateDirectory(stagingDirectory);

            ExtractPayload(payload, stagingDirectory);

            File.WriteAllText(Path.Combine(stagingDirectory, MarkerFileName), marker);

            // An older build can already be sitting in the target directory with a
            // marker that no longer matches. Clear it before the move, so the new
            // payload is the only thing the terminal ever loads from there.
            DeleteDirectoryIfExists(targetDirectory);
            Directory.Move(stagingDirectory, targetDirectory);

            // Keep only the folder for the payload this launcher carries. This is
            // also what sweeps up any staging folder a previous crash left behind.
            DeleteOtherVersions(baseDirectory, targetDirectory);

            return Launch(targetDirectory, args);
        }

        private static void ExtractPayload(byte[] payload, string destination)
        {
            using (MemoryStream stream = new MemoryStream(payload))
            using (ZipArchive archive = new ZipArchive(stream, ZipArchiveMode.Read))
            {
                foreach (ZipArchiveEntry entry in archive.Entries)
                {
                    string relativeName = entry.FullName;

                    // A zip entry can carry a leading slash. Left alone it escapes
                    // the destination folder and the file lands at the root of the
                    // drive, so drop it.
                    while (relativeName.StartsWith("/", StringComparison.Ordinal))
                    {
                        relativeName = relativeName.Substring(1);
                    }

                    if (relativeName.Length == 0)
                    {
                        continue;
                    }

                    string absolutePath = Path.Combine(destination, relativeName);

                    // An entry that ends in a separator is a directory rather than a
                    // file. Creating it is enough; the file entries that follow also
                    // create their own parents.
                    if (relativeName.EndsWith("/", StringComparison.Ordinal))
                    {
                        Directory.CreateDirectory(absolutePath);
                        continue;
                    }

                    string parentDirectory = Path.GetDirectoryName(absolutePath);
                    if (!string.IsNullOrEmpty(parentDirectory))
                    {
                        Directory.CreateDirectory(parentDirectory);
                    }

                    // Copied by hand instead of ZipFileExtensions.ExtractToFile so
                    // this launcher does not need the Compression.FileSystem
                    // assembly at all.
                    using (Stream source = entry.Open())
                    using (Stream target = File.Create(absolutePath))
                    {
                        source.CopyTo(target);
                    }
                }
            }
        }

        private static int Launch(string directory, string[] args)
        {
            string executable = Path.Combine(directory, ExeName);
            if (!File.Exists(executable))
            {
                throw new InvalidOperationException(
                    "The extracted payload does not contain " + ExeName + ", so the terminal cannot start.");
            }

            ProcessStartInfo startInfo = new ProcessStartInfo();
            startInfo.FileName = executable;
            startInfo.WorkingDirectory = directory;
            startInfo.UseShellExecute = false;
            startInfo.Arguments = BuildCommandLine(args);

            Process process = Process.Start(startInfo);
            if (process == null)
            {
                throw new InvalidOperationException("Starting " + ExeName + " failed.");
            }

            // Fire and forget. The terminal is its own process and outlives this
            // launcher, so there is nothing left to wait for here.
            return 0;
        }

        private static string BuildCommandLine(string[] args)
        {
            // Hand the command line the user gave this launcher straight through to
            // the terminal, so the launcher can be used exactly like wt.exe.
            if (args == null || args.Length == 0)
            {
                return string.Empty;
            }

            List<string> quoted = new List<string>();
            for (int i = 0; i < args.Length; i++)
            {
                string arg = args[i];
                if (arg.Length == 0 || arg.IndexOfAny(new char[] { ' ', '"' }) >= 0)
                {
                    quoted.Add("\"" + arg.Replace("\"", "\"\"") + "\"");
                }
                else
                {
                    quoted.Add(arg);
                }
            }

            return string.Join(" ", quoted.ToArray());
        }

        private static void DeleteOtherVersions(string baseDirectory, string keepDirectory)
        {
            DirectoryInfo info = new DirectoryInfo(baseDirectory);
            if (!info.Exists)
            {
                return;
            }

            char[] separators = new char[] { Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar };
            string keepFullName = keepDirectory.TrimEnd(separators);

            foreach (DirectoryInfo child in info.GetDirectories())
            {
                if (string.Equals(child.FullName.TrimEnd(separators), keepFullName, StringComparison.OrdinalIgnoreCase))
                {
                    continue;
                }

                try
                {
                    child.Delete(true);
                }
                catch
                {
                    // A folder that cannot be removed right now is not worth failing
                    // an otherwise successful launch over. It costs disk space and
                    // nothing else, and the next run takes another pass at it.
                }
            }
        }

        private static void DeleteDirectoryIfExists(string path)
        {
            if (Directory.Exists(path))
            {
                Directory.Delete(path, true);
            }
        }

        private static string FindResourceName(Assembly assembly, string nameSuffix)
        {
            // The resources are embedded by name at build time, but the compiler
            // prefixes a resource with the default namespace when one is present,
            // so match on the suffix instead of assuming the exact name.
            foreach (string name in assembly.GetManifestResourceNames())
            {
                if (name.EndsWith(nameSuffix, StringComparison.OrdinalIgnoreCase))
                {
                    return name;
                }
            }
            return null;
        }

        private static string ReadStringResource(Assembly assembly, string nameSuffix)
        {
            string name = FindResourceName(assembly, nameSuffix);
            if (name == null)
            {
                return null;
            }

            using (Stream stream = assembly.GetManifestResourceStream(name))
            {
                if (stream == null)
                {
                    return null;
                }
                using (StreamReader reader = new StreamReader(stream, true))
                {
                    return reader.ReadToEnd();
                }
            }
        }

        private static byte[] ReadAllBytesResource(Assembly assembly, string nameSuffix)
        {
            string name = FindResourceName(assembly, nameSuffix);
            if (name == null)
            {
                return null;
            }

            using (Stream stream = assembly.GetManifestResourceStream(name))
            {
                if (stream == null)
                {
                    return null;
                }

                // The payload is tens of megabytes, so read it from a stream whose
                // length is already known rather than growing a buffer chunk by
                // chunk.
                byte[] buffer = new byte[stream.Length];
                int totalRead = 0;
                while (totalRead < buffer.Length)
                {
                    int read = stream.Read(buffer, totalRead, buffer.Length - totalRead);
                    if (read == 0)
                    {
                        break;
                    }
                    totalRead += read;
                }

                if (totalRead < buffer.Length)
                {
                    Array.Resize(ref buffer, totalRead);
                }
                return buffer;
            }
        }

        private static string ReadAllText(string path)
        {
            using (Stream stream = File.OpenRead(path))
            using (StreamReader reader = new StreamReader(stream, true))
            {
                return reader.ReadToEnd();
            }
        }
    }
}
