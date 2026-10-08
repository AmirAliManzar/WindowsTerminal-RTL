// The installer wizard: a dark WinForms UI built entirely in code (no XAML,
// no designer) so it compiles with a bare csc.exe.
//
// Pages: welcome -> architecture -> destination -> options -> installing ->
// done. The heavy lifting lives in InstallJob; this only renders its Log and
// Progress events and keeps the navigation honest.
//
// Two languages, English and Persian, selected by the Windows UI language and
// switchable from the header button. All control positions are authored once
// in left-to-right coordinates and mirrored through X() when Persian is
// active, so the same layout code serves both directions. The form itself
// keeps RightToLeft = No; per-control RightToLeft plus that mirror is enough
// and avoids the framework silently moving things behind our back.
//
// C# 5 syntax on purpose: the csc.exe that ships with the .NET Framework does
// not know anything newer.

using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.IO;
using System.Reflection;
using System.Threading;
using System.Windows.Forms;

namespace WindowsTerminalRtlInstaller
{
    internal sealed class WizardForm : Form
    {
        // Palette. Dark, like the terminal it installs.
        private static readonly Color Bg = Color.FromArgb(20, 20, 20);
        private static readonly Color HeaderBg = Color.FromArgb(32, 32, 32);
        private static readonly Color Fg = Color.FromArgb(240, 240, 240);
        private static readonly Color Muted = Color.FromArgb(152, 158, 164);
        private static readonly Color Button = Color.FromArgb(48, 48, 48);
        private static readonly Color ButtonBorder = Color.FromArgb(74, 74, 74);
        private static readonly Color Accent = Color.FromArgb(76, 194, 255);
        private static readonly Color Tile = Color.FromArgb(236, 238, 241);

        private static readonly Font Ui = new Font("Segoe UI", 9.75f);
        private static readonly Font UiBold = new Font("Segoe UI", 9.75f, FontStyle.Bold);
        private static readonly Font Title = new Font("Segoe UI", 13f, FontStyle.Bold);
        private static readonly Font HeaderTitle = new Font("Segoe UI", 15.75f, FontStyle.Bold);
        private static readonly Font Mono = new Font("Consolas", 8.75f);

        private const int W = 620;

        private readonly InstallJob _job = new InstallJob();

        private readonly Panel _content;
        private readonly Button _btnBack, _btnNext, _btnCancel, _btnLang;
        private readonly PictureBox _pic;
        private readonly Label _lblTitle, _lblSub;
        private Label _lblStatus, _lblPercent;
        private ProgressBar _progress;
        private TextBox _txtLog;
        private Label _lblRelease, _lblDirNote;
        private RadioButton _rbArchAuto, _rbArchX64, _rbArchArm64;
        private TextBox _txtDir;
        private CheckBox _chkStart, _chkDesktop;
        private Label _lblDoneTitle, _lblDoneBody;
        private Button _btnLaunch, _btnRetry, _btnUninstall, _btnRepair, _btnUpdate;
        private Label _lblWelcomeTitle, _lblWelcomeBody, _lblWelcomeStatus;
        private Label _lblArchTitle, _lblArchNote, _lblDestTitle, _lblOptionsTitle, _lblOptionsNote;
        private Button _btnBrowse;

        private readonly Control[] _pages;
        private int _index;
        private bool _working;
        private bool _fetchedRelease;
        private long _lastProgressTick;

        private string _installDir;
        private bool _uninstalling;
        private bool _repairing;

        public WizardForm()
        {
            Strings.Set(Strings.Detect());

            Text = Strings.Get(Strings.FormTitle);
            ClientSize = new Size(W, 460);
            StartPosition = FormStartPosition.CenterScreen;
            FormBorderStyle = FormBorderStyle.FixedSingle;
            MaximizeBox = false;
            MinimizeBox = false;
            BackColor = Bg;
            ForeColor = Fg;
            // Deliberately No: mirroring is done by hand in Localize().
            RightToLeft = RightToLeft.No;
            Font = Ui;

            Icon = LoadEmbeddedIcon();

            // ---- header -------------------------------------------------
            var header = new Panel { Dock = DockStyle.Top, Height = 66, BackColor = HeaderBg };
            _pic = new PictureBox
            {
                Size = new Size(46, 46),
                Location = At(24, 10, 46),
                SizeMode = PictureBoxSizeMode.CenterImage,
                BackColor = HeaderBg,
            };
            using (var ic = LoadEmbeddedIcon())
            {
                if (ic != null) _pic.Image = IconTile(ic, 46);
            }
            _lblTitle = new Label
            {
                Font = HeaderTitle,
                ForeColor = Fg,
                Location = At(80, 9, 430),
                Size = new Size(430, 28),
                BackColor = HeaderBg,
            };
            _lblSub = new Label
            {
                Font = Ui,
                ForeColor = Muted,
                Location = At(80, 38, 430),
                Size = new Size(430, 22),
                BackColor = HeaderBg,
            };
            _btnLang = MakeButton(Strings.OtherName, 72);
            _btnLang.Location = At(524, 19, 72);
            _btnLang.Click += (s, e) =>
            {
                Strings.Set(Strings.Rtl ? AppLanguage.English : AppLanguage.Persian);
                Localize();
            };
            header.Controls.AddRange(new Control[] { _lblTitle, _lblSub, _pic, _btnLang });

            // ---- button bar ---------------------------------------------
            var bar = new Panel { Dock = DockStyle.Bottom, Height = 58, BackColor = Bg };
            _btnBack = MakeButton(Strings.Get(Strings.Back), 110);
            _btnBack.Location = At(24, 12, 110);
            _btnNext = MakePrimary(Strings.Get(Strings.Next), 132);
            _btnNext.Location = At(144, 12, 132);
            _btnCancel = MakeButton(Strings.Get(Strings.Cancel), 110);
            _btnCancel.Location = At(486, 12, 110);
            bar.Controls.AddRange(new Control[] { _btnCancel, _btnNext, _btnBack });

            _btnBack.Click += (s, e) => ShowPage(_index - 1);
            _btnNext.Click += (s, e) => OnNext();
            _btnCancel.Click += (s, e) => Close();

            // ---- content ------------------------------------------------
            _content = new Panel { Dock = DockStyle.Fill, BackColor = Bg };

            Controls.AddRange(new Control[] { _content, bar, header });

            _pages = new Control[]
            {
                BuildWelcome(),
                BuildArch(),
                BuildDestination(),
                BuildOptions(),
                BuildInstalling(),
                BuildDone(),
            };
            foreach (var p in _pages)
            {
                p.Dock = DockStyle.Fill;
                p.BackColor = Bg;
                _content.Controls.Add(p);
            }

            // The install job reports on whatever thread it likes; everything
            // here must run on the UI thread.
            _job.Log += msg => BeginInvoke((Action)(() => AppendLog(msg)));
            _job.Progress += (got, total) => BeginInvoke((Action)(() => UpdateProgress(got, total)));

            Localize();
            ShowPage(0);
        }

        // Mirrors a left-to-right x coordinate when Persian is active, so the
        // same authored layout serves both directions. Named At rather than
        // Point so it cannot be confused with System.Drawing.Point.
        private static Point At(int x, int y, int width)
        {
            return new Point(Strings.Rtl ? (W - x - width) : x, y);
        }

        // ------------------------------------------------------- localize

        private void Localize()
        {
            RightToLeft rtl = Strings.Rtl ? RightToLeft.Yes : RightToLeft.No;
            ContentAlignment near = Strings.Rtl ? ContentAlignment.MiddleRight : ContentAlignment.MiddleLeft;
            ContentAlignment topNear = Strings.Rtl ? ContentAlignment.TopRight : ContentAlignment.TopLeft;

            Text = Strings.Get(Strings.FormTitle);

            _lblTitle.Text = Strings.Get(Strings.HeaderTitle);
            _lblTitle.TextAlign = near;
            _lblTitle.RightToLeft = rtl;
            _lblSub.Text = Strings.Get(Strings.HeaderSub) + AppVersion();
            _lblSub.TextAlign = near;
            _lblSub.RightToLeft = rtl;
            _btnLang.Text = Strings.OtherName;

            _btnBack.Text = Strings.Get(Strings.Back);
            _btnCancel.Text = Strings.Get(Strings.Cancel);

            _lblWelcomeTitle.Text = Strings.Get(Strings.WelcomeTitle);
            _lblWelcomeBody.Text = Strings.Get(Strings.WelcomeBody);
            _btnUninstall.Text = Strings.Get(Strings.Uninstall);
            _btnRepair.Text = Strings.Get(Strings.Repair);
            _btnUpdate.Text = Strings.Get(Strings.Update);
            RefreshWelcomeStatus();

            _lblArchTitle.Text = Strings.Get(Strings.ArchTitle);
            _rbArchAuto.Text = Strings.Get(Strings.ArchAuto);
            _rbArchX64.Text = Strings.Get(Strings.ArchX64);
            _rbArchArm64.Text = Strings.Get(Strings.ArchArm64);
            _lblArchNote.Text = Strings.Get(Strings.ArchNote);

            _lblDestTitle.Text = Strings.Get(Strings.DestTitle);
            _btnBrowse.Text = Strings.Get(Strings.Browse);
            RefreshDirNote();

            _lblOptionsTitle.Text = Strings.Get(Strings.OptionsTitle);
            _chkStart.Text = Strings.Get(Strings.ChkStart);
            _chkDesktop.Text = Strings.Get(Strings.ChkDesktop);
            _lblOptionsNote.Text = Strings.Get(Strings.OptionsNote);

            _btnLaunch.Text = Strings.Get(Strings.Launch);
            _btnRetry.Text = Strings.Get(Strings.Retry);

            // Text direction for everything that shows words.
            foreach (Control c in _content.Controls)
                ApplyRtl(c, rtl, near, topNear);
            _btnBack.RightToLeft = rtl;
            _btnNext.RightToLeft = rtl;
            _btnCancel.RightToLeft = rtl;
            _btnLang.RightToLeft = rtl;

            // A path and a log are left-to-right even in Persian.
            _txtDir.RightToLeft = RightToLeft.No;
            _txtDir.TextAlign = HorizontalAlignment.Left;
            _txtLog.RightToLeft = RightToLeft.No;

            // Re-mirror the asymmetric pieces.
            _pic.Location = At(24, 10, 46);
            _lblTitle.Location = At(80, 9, 430);
            _lblSub.Location = At(80, 38, 430);
            _btnLang.Location = At(524, 19, 72);
            _btnBack.Location = At(24, 12, 110);
            _btnNext.Location = At(144, 12, 132);
            _btnCancel.Location = At(486, 12, 110);
            _btnUninstall.Location = At(24, 252, 140);
            _btnRepair.Location = At(174, 252, 140);
            _btnUpdate.Location = At(324, 252, 140);
            _txtDir.Location = At(24, 78, 442);
            _btnBrowse.Location = At(476, 78, 120);
            _btnLaunch.Location = At(24, 240, 170);
            _btnRetry.Location = At(204, 240, 140);

            ShowPage(_index);
            Refresh();
        }

        // Walks a page and points every label, radio and check at the right
        // edge for Persian or the left edge for English.
        //
        // Only ever called on a page panel, and only sets RightToLeft on
        // controls that draw text. Setting it on a container Panel would flip
        // the whole panel into WS_EX_LAYOUTRTL and mirror its children a second
        // time, undoing the manual mirror Localize() just applied.
        private static void ApplyRtl(Control parent, RightToLeft rtl,
                                     ContentAlignment near, ContentAlignment topNear)
        {
            foreach (Control c in parent.Controls)
            {
                var lbl = c as Label;
                if (lbl != null)
                {
                    lbl.RightToLeft = rtl;
                    lbl.TextAlign = (lbl.Height >= 60) ? topNear : near;
                    continue;
                }
                var radio = c as RadioButton;
                if (radio != null)
                {
                    radio.RightToLeft = rtl;
                    radio.TextAlign = near;
                    continue;
                }
                var check = c as CheckBox;
                if (check != null)
                {
                    check.RightToLeft = rtl;
                    check.TextAlign = near;
                    continue;
                }
                var btn = c as Button;
                if (btn != null)
                {
                    btn.RightToLeft = rtl;
                    continue;
                }
                // ProgressBar fills from the right in Persian, TextBoxes keep
                // whatever their page set.
                if (!(c is Panel)) c.RightToLeft = rtl;
            }
        }

        // ---------------------------------------------------------- pages

        private Control BuildWelcome()
        {
            var p = new Panel();
            int w = W - 48;

            _lblWelcomeTitle = new Label
            {
                Font = Title, ForeColor = Fg,
                Location = new Point(24, 26), Size = new Size(w, 30),
            };
            _lblWelcomeBody = new Label
            {
                Font = Ui, ForeColor = Color.FromArgb(200, 200, 200),
                Location = new Point(24, 66), Size = new Size(w, 140),
            };
            _lblWelcomeStatus = new Label
            {
                Font = Ui, ForeColor = Muted,
                Location = new Point(24, 218), Size = new Size(w, 20),
            };
            _btnUninstall = MakeButton(Strings.Get(Strings.Uninstall), 140);
            _btnUninstall.Location = At(24, 252, 140);
            _btnUninstall.Click += (s, e) => StartUninstall();

            // Repair sits beside Uninstall and is only offered when an install is
            // already present. It rebuilds the shortcuts and the Add or remove
            // programs entry without downloading anything.
            _btnRepair = MakeButton(Strings.Get(Strings.Repair), 140);
            _btnRepair.Location = At(174, 252, 140);
            _btnRepair.Click += (s, e) => StartRepair();

            // Update sits beside Repair and appears only when the installed payload
            // is older than this installer. Unlike Repair it downloads the latest
            // release and replaces the payload, which is what "I ran the new
            // installer" is expected to do.
            _btnUpdate = MakeButton(Strings.Get(Strings.Update), 140);
            _btnUpdate.Location = At(324, 252, 140);
            _btnUpdate.Click += (s, e) => StartUpdate();

            p.Controls.AddRange(new Control[] { _lblWelcomeTitle, _lblWelcomeBody, _lblWelcomeStatus, _btnUninstall, _btnRepair, _btnUpdate });
            return p;
        }

        // True when an install is present on disk and its recorded version is older
        // than this installer. That is the only situation in which Update, rather
        // than Repair, is the action the user came for.
        private static bool UpdateAvailable()
        {
            string dir = InstallJob.FindInstallDir();
            if (string.IsNullOrEmpty(dir))
                return false;
            string installed = InstallJob.ReadInstalledVersion(dir);
            if (string.IsNullOrEmpty(installed))
                return false;
            Version have, want;
            if (!Version.TryParse(installed, out have))
                return false;
            if (!Version.TryParse(Assembly.GetExecutingAssembly().GetName().Version.ToString(), out want))
                return false;
            return have < want;
        }

        private void RefreshWelcomeStatus()
        {
            string existing = InstallJob.FindInstallDir();
            _lblWelcomeStatus.Text = existing != null
                ? Strings.Get(Strings.InstalledAt) + existing
                : Strings.Get(Strings.NotInstalled);
            if (_btnUninstall != null)
                _btnUninstall.Visible = (existing != null);
            if (_btnRepair != null)
                _btnRepair.Visible = (existing != null);
            if (_btnUpdate != null)
                _btnUpdate.Visible = (existing != null && UpdateAvailable());
        }

        private Control BuildArch()
        {
            var p = new Panel();
            int w = W - 48;

            _lblArchTitle = MakeTitle();
            _lblArchTitle.Location = new Point(24, 26);

            _rbArchAuto = MakeRadio();
            _rbArchX64 = MakeRadio();
            _rbArchArm64 = MakeRadio();
            _rbArchAuto.Checked = true;

            var radios = new[] { _rbArchAuto, _rbArchX64, _rbArchArm64 };
            for (int i = 0; i < radios.Length; i++)
            {
                radios[i].Location = new Point(24, 74 + i * 36);
                radios[i].Size = new Size(w, 30);
            }

            _lblArchNote = new Label
            {
                Font = Ui, ForeColor = Muted,
                Location = new Point(24, 192), Size = new Size(w, 20),
            };
            _lblRelease = new Label
            {
                Font = UiBold, ForeColor = Accent,
                Location = new Point(24, 226), Size = new Size(w, 20),
            };

            p.Controls.AddRange(new Control[] { _lblArchTitle, _rbArchAuto, _rbArchX64, _rbArchArm64, _lblArchNote, _lblRelease });
            return p;
        }

        private Control BuildDestination()
        {
            var p = new Panel();
            int w = W - 48;

            _lblDestTitle = MakeTitle();
            _lblDestTitle.Location = new Point(24, 26);

            _txtDir = new TextBox
            {
                Text = InstallJob.DefaultInstallDir(),
                Font = Ui,
                Location = new Point(24, 78),
                Size = new Size(w - 120 - 10, 30),
                BorderStyle = BorderStyle.FixedSingle,
                BackColor = Color.FromArgb(12, 12, 12),
                ForeColor = Fg,
            };
            _btnBrowse = MakeButton(Strings.Get(Strings.Browse), 120);
            _btnBrowse.Location = new Point(476, 78);
            _btnBrowse.Click += (s, e) =>
            {
                using (var dlg = new FolderBrowserDialog())
                {
                    dlg.Description = Strings.Get(Strings.FolderDialog);
                    dlg.SelectedPath = Directory.Exists(_txtDir.Text) ? _txtDir.Text : "";
                    if (dlg.ShowDialog(this) == DialogResult.OK)
                        _txtDir.Text = dlg.SelectedPath;
                }
            };

            _lblDirNote = new Label
            {
                Font = Ui, ForeColor = Muted,
                Location = new Point(24, 122), Size = new Size(w, 60),
            };

            _txtDir.TextChanged += (s, e) => RefreshDirNote();

            p.Controls.AddRange(new Control[] { _lblDestTitle, _txtDir, _btnBrowse, _lblDirNote });
            return p;
        }

        private void RefreshDirNote()
        {
            string dir = _txtDir != null ? _txtDir.Text : InstallJob.DefaultInstallDir();
            string note = Strings.Get(Strings.DestNote);
            if (!string.IsNullOrEmpty(dir) && File.Exists(Path.Combine(dir, InstallJob.ExeName)))
                note = Strings.Get(Strings.DestNoteExisting);
            if (_lblDirNote != null) _lblDirNote.Text = note;
        }

        private Control BuildOptions()
        {
            var p = new Panel();
            int w = W - 48;

            _lblOptionsTitle = MakeTitle();
            _lblOptionsTitle.Location = new Point(24, 26);

            _chkStart = MakeCheck();
            _chkDesktop = MakeCheck();
            _chkStart.Checked = true;
            _chkDesktop.Checked = true;
            _chkStart.Location = new Point(24, 78);
            _chkDesktop.Location = new Point(24, 114);
            _chkStart.Size = new Size(w, 30);
            _chkDesktop.Size = new Size(w, 30);

            _lblOptionsNote = new Label
            {
                Font = Ui, ForeColor = Muted,
                Location = new Point(24, 160), Size = new Size(w, 20),
            };

            p.Controls.AddRange(new Control[] { _lblOptionsTitle, _chkStart, _chkDesktop, _lblOptionsNote });
            return p;
        }

        private Control BuildInstalling()
        {
            var p = new Panel();
            int w = W - 48;

            _lblStatus = new Label
            {
                Font = UiBold, ForeColor = Fg,
                Location = new Point(24, 22), Size = new Size(w, 22),
            };
            _progress = new ProgressBar
            {
                Style = ProgressBarStyle.Continuous,
                Location = new Point(24, 54),
                Size = new Size(w, 18),
                Minimum = 0, Maximum = 1000, Value = 0,
            };
            _lblPercent = new Label
            {
                Font = Ui, ForeColor = Muted,
                Location = new Point(24, 78), Size = new Size(w, 20),
            };
            _txtLog = new TextBox
            {
                Multiline = true,
                ReadOnly = true,
                BorderStyle = BorderStyle.FixedSingle,
                BackColor = Color.FromArgb(12, 12, 12),
                ForeColor = Color.FromArgb(207, 207, 207),
                Font = Mono,
                Location = new Point(24, 110),
                Size = new Size(w, 226),
                ScrollBars = ScrollBars.Vertical,
            };

            p.Controls.AddRange(new Control[] { _lblStatus, _progress, _lblPercent, _txtLog });
            return p;
        }

        private Control BuildDone()
        {
            var p = new Panel();
            int w = W - 48;

            _lblDoneTitle = new Label
            {
                Font = Title, ForeColor = Fg,
                Location = new Point(24, 44), Size = new Size(w, 30),
            };
            _lblDoneBody = new Label
            {
                Font = Ui, ForeColor = Color.FromArgb(200, 200, 200),
                Location = new Point(24, 86), Size = new Size(w, 130),
            };
            _btnLaunch = MakePrimary(Strings.Get(Strings.Launch), 170);
            _btnLaunch.Location = new Point(24, 240);
            _btnLaunch.Click += (s, e) =>
            {
                try
                {
                    if (!string.IsNullOrEmpty(_installDir))
                        System.Diagnostics.Process.Start(Path.Combine(_installDir, InstallJob.ExeName));
                }
                catch { }
                Close();
            };
            _btnRetry = MakeButton(Strings.Get(Strings.Retry), 140);
            _btnRetry.Location = new Point(204, 240);
            _btnRetry.Click += (s, e) =>
            {
                if (_uninstalling) StartUninstall(); else StartInstall();
            };

            p.Controls.AddRange(new Control[] { _lblDoneTitle, _lblDoneBody, _btnLaunch, _btnRetry });
            return p;
        }

        // ------------------------------------------------------- navigation

        private void ShowPage(int index)
        {
            if (index < 0 || index >= _pages.Length) return;
            _index = index;
            for (int i = 0; i < _pages.Length; i++)
                _pages[i].Visible = (i == index);

            // The uninstall button only belongs on the welcome page.
            RefreshWelcomeStatus();
            _btnUninstall.Visible = (index == 0 && InstallJob.FindInstallDir() != null);
            _btnUpdate.Visible = (index == 0 && UpdateAvailable());

            _btnBack.Visible = (index >= 1 && index <= 3);
            _btnCancel.Visible = (index != 4 && index != 5);
            _btnNext.Visible = (index != 4);

            switch (index)
            {
                case 0: _btnNext.Text = Strings.Get(Strings.Next); break;
                case 1:
                    _btnNext.Text = Strings.Get(Strings.Next);
                    FetchRelease();
                    break;
                case 2: _btnNext.Text = Strings.Get(Strings.Next); break;
                case 3: _btnNext.Text = Strings.Get(Strings.Install); break;
                case 5: _btnNext.Text = Strings.Get(Strings.Finish); break;
            }
            _btnNext.Enabled = !_working;
            _btnBack.Enabled = !_working;
        }

        private void OnNext()
        {
            if (_working) return;
            if (_index == 3) { StartInstall(); return; }
            if (_index == 5) { Close(); return; }
            ShowPage(_index + 1);
        }

        // The release tag is shown as soon as the architecture page opens, on a
        // thread, so the wizard never blocks on the GitHub API.
        private void FetchRelease()
        {
            if (_fetchedRelease) return;
            _fetchedRelease = true;
            _lblRelease.Text = Strings.Get(Strings.CheckingRelease);
            var th = new Thread(() =>
            {
                string tag = null;
                try { tag = _job.FetchLatestReleaseInfo().Tag; }
                catch { }
                BeginInvoke((Action)(() =>
                {
                    _lblRelease.Text = string.IsNullOrEmpty(tag)
                        ? Strings.Get(Strings.ReleaseUnavailable)
                        : Strings.Get(Strings.LatestRelease) + tag;
                }));
            });
            th.IsBackground = true;
            th.Start();
        }

        // ----------------------------------------------------------- work

        private void StartInstall()
        {
            var s = new InstallSettings();
            s.InstallDir = _txtDir.Text;
            s.Architecture = _rbArchArm64.Checked ? "arm64"
                           : _rbArchX64.Checked ? "x64"
                           : InstallJob.DetectArchitecture();
            s.StartMenuShortcut = _chkStart.Checked;
            s.DesktopShortcut = _chkDesktop.Checked;
            s.Force = true;   // the user walked through a wizard; a reinstall is intentional
            _installDir = Path.GetFullPath(s.InstallDir);
            _uninstalling = false;

            _txtLog.Clear();
            _progress.Value = 0;
            _lblPercent.Text = "";
            _lblStatus.Text = Strings.Get(Strings.StatusDownloading);
            SetWorking(true);
            ShowPage(4);

            var th = new Thread(() => RunJob(() => _job.Install(s)));
            th.IsBackground = true;
            th.Start();
        }

        private void StartUninstall()
        {
            string dir = InstallJob.FindInstallDir() ?? InstallJob.DefaultInstallDir();
            _installDir = null;
            _uninstalling = true;
            _repairing = false;
            _txtLog.Clear();
            _progress.Value = 0;
            _lblPercent.Text = "";
            _lblStatus.Text = Strings.Get(Strings.StatusUninstalling);
            _btnUninstall.Visible = false;
            _btnRepair.Visible = false;
            _btnUpdate.Visible = false;
            SetWorking(true);
            ShowPage(4);

            var th = new Thread(() => RunJob(() => _job.Uninstall(dir)));
            th.IsBackground = true;
            th.Start();
        }

        private void StartRepair()
        {
            string dir = InstallJob.FindInstallDir() ?? InstallJob.DefaultInstallDir();
            _installDir = null;
            _uninstalling = false;
            _repairing = true;
            _txtLog.Clear();
            _progress.Value = 0;
            _progress.Style = ProgressBarStyle.Marquee;
            _lblPercent.Text = "";
            _lblStatus.Text = Strings.Get(Strings.StatusRepairing);
            _btnUninstall.Visible = false;
            _btnRepair.Visible = false;
            _btnUpdate.Visible = false;
            SetWorking(true);
            ShowPage(4);

            var th = new Thread(() => RunJob(() => _job.Repair(dir)));
            th.IsBackground = true;
            th.Start();
        }

        // Update is the action that actually replaces the payload: it downloads the
        // latest release over the existing install, keeping the same folder and the
        // same architecture. Repair cannot do this, which is why the two buttons
        // exist side by side.
        private void StartUpdate()
        {
            string dir = InstallJob.FindInstallDir();
            if (string.IsNullOrEmpty(dir))
            {
                StartInstall();
                return;
            }
            string arch = InstallJob.ReadInstalledArch(dir);
            if (string.IsNullOrEmpty(arch))
                arch = InstallJob.DetectArchitecture();

            var s = new InstallSettings();
            s.InstallDir = dir;
            s.Architecture = arch;
            s.StartMenuShortcut = true;
            s.DesktopShortcut = true;
            s.Force = true;
            _installDir = Path.GetFullPath(s.InstallDir);
            _uninstalling = false;
            _repairing = false;

            _txtLog.Clear();
            _progress.Value = 0;
            _progress.Style = ProgressBarStyle.Continuous;
            _lblPercent.Text = "";
            _lblStatus.Text = Strings.Get(Strings.StatusDownloading);
            _btnUninstall.Visible = false;
            _btnRepair.Visible = false;
            _btnUpdate.Visible = false;
            SetWorking(true);
            ShowPage(4);

            var th = new Thread(() => RunJob(() => _job.Install(s)));
            th.IsBackground = true;
            th.Start();
        }

        private void RunJob(Action work)
        {
            Exception error = null;
            try { work(); }
            catch (Exception ex)
            {
                // .Result calls wrap everything in AggregateException; the outer
                // message ("One or more errors occurred") says nothing, so walk
                // down to the one that actually explains the failure.
                Exception real = ex;
                for (var a = ex as AggregateException; a != null && a.InnerException != null; a = real as AggregateException)
                    real = a.InnerException;
                error = real;
            }
            BeginInvoke((Action)(() => Finish(error)));
        }

        private void Finish(Exception error)
        {
            SetWorking(false);
            bool ok = (error == null);
            bool remove = _uninstalling;
            bool repair = _repairing;

            _lblDoneTitle.Text = !ok ? Strings.Get(Strings.DoneTitleFail)
                                 : remove ? Strings.Get(Strings.DoneTitleOkRemove)
                                 : repair ? Strings.Get(Strings.DoneTitleOkRepair)
                                          : Strings.Get(Strings.DoneTitleOk);
            _lblDoneTitle.ForeColor = ok ? Fg : Color.FromArgb(255, 120, 120);

            if (ok)
            {
                _lblDoneBody.Text = remove ? Strings.Get(Strings.DoneBodyOkRemove)
                                    : repair ? Strings.Get(Strings.DoneBodyOkRepair)
                                             : Strings.Get(Strings.DoneBodyOk);
            }
            else
            {
                string msg = error != null ? error.Message : Strings.Get(Strings.UnknownError);
                string inner = (error != null && error.InnerException != null &&
                                !ReferenceEquals(error, error.InnerException))
                               ? error.InnerException.Message : null;
                string url = "https://github.com/" + InstallJob.Owner + "/" + InstallJob.Repo + "/issues";
                _lblDoneBody.Text =
                    Strings.Get(Strings.DoneBodyFailPrefix) + msg +
                    (string.IsNullOrEmpty(inner) ? "" : Strings.Get(Strings.Cause) + inner) +
                    Strings.Get(Strings.RetryHint) + url + Strings.Get(Strings.IssuesSuffix);
            }

            _btnLaunch.Visible = (ok && !remove);
            _btnRetry.Visible = !ok;
            _btnNext.Text = Strings.Get(Strings.Finish);
            ShowPage(5);
        }

        private void SetWorking(bool value)
        {
            _working = value;
            _btnNext.Enabled = !value;
            _btnBack.Enabled = !value;
            _btnCancel.Enabled = !value;
        }

        // ----------------------------------------------------------- output

        private void AppendLog(string msg)
        {
            if (_txtLog == null) return;
            _txtLog.AppendText(msg + Environment.NewLine);
            _txtLog.SelectionStart = _txtLog.TextLength;
            _txtLog.ScrollToCaret();
        }

        private void UpdateProgress(long got, long total)
        {
            int tick = Environment.TickCount;
            if (total <= 0)
            {
                _progress.Style = ProgressBarStyle.Marquee;
                _lblPercent.Text = InstallJob.FormatBytes(got) + Strings.Get(Strings.BytesReceived);
                return;
            }
            if (tick - _lastProgressTick < 60 && got < total) return;   // throttle cross-thread updates
            _lastProgressTick = tick;
            _progress.Style = ProgressBarStyle.Continuous;
            _progress.Value = (int)Math.Min(1000, got * 1000 / total);
            double pct = got * 100.0 / total;
            _lblPercent.Text = pct.ToString("F1") + "%   " +
                               InstallJob.FormatBytes(got) + Strings.Get(Strings.ProgressSeparator) +
                               InstallJob.FormatBytes(total);
        }

        // ---------------------------------------------------------- helpers

        private static string AppVersion()
        {
            var ver = Assembly.GetExecutingAssembly().GetName().Version;
            return ver != null ? ver.ToString(3) : "0.0.0";
        }

        private static Label MakeTitle()
        {
            return new Label
            {
                Font = Title,
                ForeColor = Fg,
                Size = new Size(W - 48, 30),
            };
        }

        private Button MakeButton(string text, int width)
        {
            var b = new Button
            {
                Text = text,
                Font = Ui,
                Size = new Size(width, 34),
                FlatStyle = FlatStyle.Flat,
                BackColor = Button,
                ForeColor = Fg,
                TextAlign = ContentAlignment.MiddleCenter,
                UseVisualStyleBackColor = false,
            };
            b.FlatAppearance.BorderSize = 1;
            b.FlatAppearance.BorderColor = ButtonBorder;
            b.FlatAppearance.MouseOverBackColor = Color.FromArgb(64, 64, 64);
            b.FlatAppearance.MouseDownBackColor = Color.FromArgb(40, 40, 40);
            return b;
        }

        private Button MakePrimary(string text, int width)
        {
            var b = MakeButton(text, width);
            b.BackColor = Accent;
            b.ForeColor = Color.FromArgb(12, 12, 12);
            b.Font = UiBold;
            b.FlatAppearance.BorderColor = Accent;
            b.FlatAppearance.MouseOverBackColor = Color.FromArgb(110, 210, 255);
            b.FlatAppearance.MouseDownBackColor = Color.FromArgb(60, 170, 230);
            return b;
        }

        private static RadioButton MakeRadio()
        {
            return new RadioButton
            {
                Font = Ui,
                ForeColor = Fg,
                FlatStyle = FlatStyle.Flat,
                BackColor = Bg,
                UseVisualStyleBackColor = false,
            };
        }

        private static CheckBox MakeCheck()
        {
            return new CheckBox
            {
                Font = Ui,
                ForeColor = Fg,
                FlatStyle = FlatStyle.Flat,
                BackColor = Bg,
                UseVisualStyleBackColor = false,
            };
        }

        // The icon ships as /resource:assets\installer.ico so the wizard can
        // show it at full size instead of the 16px ExtractAssociatedIcon gives.
        private static Icon LoadEmbeddedIcon()
        {
            try
            {
                var asm = Assembly.GetExecutingAssembly();
                foreach (var name in asm.GetManifestResourceNames())
                {
                    if (name.EndsWith("installer.ico", StringComparison.OrdinalIgnoreCase))
                    {
                        using (var s = asm.GetManifestResourceStream(name))
                            return new Icon(s);
                    }
                }
            }
            catch { }
            return null;
        }

        // The icon is mostly dark grey on transparency, so on the dark header
        // it vanishes. Painting it onto a light rounded tile makes both the
        // green accent and the terminal body readable.
        private static Bitmap IconTile(Icon ic, int size)
        {
            var bmp = new Bitmap(size, size, System.Drawing.Imaging.PixelFormat.Format32bppPArgb);
            using (var g = Graphics.FromImage(bmp))
            {
                g.SmoothingMode = SmoothingMode.AntiAlias;
                g.InterpolationMode = InterpolationMode.HighQualityBicubic;
                g.Clear(Color.Transparent);

                int r = size / 5;
                var rect = new Rectangle(0, 0, size - 1, size - 1);
                var path = new GraphicsPath();
                path.AddArc(rect.X, rect.Y, r, r, 180, 90);
                path.AddArc(rect.Right - r, rect.Y, r, r, 270, 90);
                path.AddArc(rect.Right - r, rect.Bottom - r, r, r, 0, 90);
                path.AddArc(rect.X, rect.Bottom - r, r, r, 90, 90);
                path.CloseFigure();
                using (var br = new SolidBrush(Tile))
                    g.FillPath(br, path);

                int pad = (int)(size * 0.16);
                var inner = new Rectangle(pad, pad, size - 2 * pad, size - 2 * pad);
                using (var src = ic.ToBitmap())
                    g.DrawImage(src, inner);
            }
            return bmp;
        }
    }
}
