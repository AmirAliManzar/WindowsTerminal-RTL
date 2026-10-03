//! WindowsTerminal-RTL patcher.
//!
//! A thin wrapper around `scripts/patch-wt.ps1` that asks Windows for
//! administrator rights before running it, so the user can just double-click the
//! exe. No dependencies, links shell32 directly - the same shape as the zed-rtl
//! patcher, so both projects behave the same way for the user.
//!
//! Why a wrapper at all: the actual work has to read and write
//! `C:\Program Files\WindowsApps\...`, which needs elevation, and a bare .ps1
//! double-clicked in Explorer does not get it. ShellExecuteEx with "runas" does.
//!
//! Usage, all optional:
//!     WindowsTerminal-RTL-patcher.exe                    patch the installed terminal
//!     WindowsTerminal-RTL-patcher.exe -Revert            put the original files back
//!     WindowsTerminal-RTL-patcher.exe -LocalDir <path>   use DLLs from a folder instead
//!                                                        of downloading them
//!     WindowsTerminal-RTL-patcher.exe -PackageDir <path> patch an install elsewhere
//!     WindowsTerminal-RTL-patcher.exe -NoRestart         do not relaunch the terminal
//!
//! The exe passes -RequiredVersion automatically. It will not patch a Windows
//! Terminal of any other version: the renderer is C++ with no ABI between
//! releases, so mismatched DLLs corrupt the install rather than patch it.

use std::ffi::OsStr;
use std::io::Write;
use std::os::windows::ffi::OsStrExt;
use std::process::Command;

#[cfg(windows)]
#[link(name = "shell32")]
extern "system" {
    fn IsUserAnAdmin() -> i32;
    fn ShellExecuteW(
        hwnd: *mut std::ffi::c_void,
        op: *const u16,
        file: *const u16,
        params: *const u16,
        dir: *const u16,
        show: i32,
    ) -> isize;
}

#[cfg(not(windows))]
compile_error!("the patcher only exists for Windows");

/// The patch script, baked into the exe at build time. Keeping one copy means the
/// exe and the repository can never disagree about what it does.
const PS1: &str = include_str!("../../scripts/patch-wt.ps1");

/// The Windows Terminal version the bundled DLLs were built from, baked in the
/// same way. The script refuses to patch any other version unless -Force is
/// given, because the renderer has no stable ABI across versions.
const REQUIRED_VERSION: &str = include_str!("../../REQUIRED_VERSION");

fn wide(s: &str) -> Vec<u16> {
    OsStr::new(s).encode_wide().chain(std::iter::once(0)).collect()
}

fn is_admin() -> bool {
    // IsUserAnAdmin is deprecated but still exported by shell32, and it is the
    // cheapest way to ask "did the process come back elevated?".
    unsafe { IsUserAnAdmin() != 0 }
}

fn pause() {
    print!("\nPress Enter to close... ");
    std::io::stdout().flush().ok();
    let mut s = String::new();
    std::io::stdin().read_line(&mut s).ok();
}

fn main() {
    let mut args: Vec<String> = std::env::args().skip(1).collect();

    // Supply the version the bundled DLLs were built for unless the caller
    // already named one. Trimmed: the file on disk ends with a newline, and
    // PowerShell would then see "1.24.11911.0\n" and not match anything.
    let required = REQUIRED_VERSION.trim();
    if !required.is_empty()
        && !args
            .iter()
            .any(|a| a.eq_ignore_ascii_case("-RequiredVersion"))
    {
        args.push("-RequiredVersion".to_string());
        args.push(required.to_string());
    }

    if !is_admin() {
        println!("This patcher needs administrator rights.");
        println!("Asking Windows for permission (click Yes on the UAC prompt)...");

        let exe = std::env::current_exe().unwrap_or_default();
        let exe_str = exe.to_str().unwrap_or("WindowsTerminal-RTL-patcher.exe");
        let params = args.join(" ");

        let rc = unsafe {
            ShellExecuteW(
                std::ptr::null_mut(),
                wide("runas").as_ptr(),
                wide(exe_str).as_ptr(),
                wide(&params).as_ptr(),
                std::ptr::null(),
                1, // SW_SHOWNORMAL
            )
        };

        if (rc as usize) <= 32 {
            eprintln!("Could not start elevated (ShellExecute error {rc}).");
            eprintln!("Right-click the patcher and choose \"Run as administrator\",");
            eprintln!("or use the script directly:");
            eprintln!("  powershell -ExecutionPolicy Bypass -File .\\scripts\\patch-wt.ps1");
            pause();
            std::process::exit(1);
        }

        // The elevated copy runs in its own console window; this one is done.
        return;
    }

    println!("Running as administrator.");

    let dir = std::env::temp_dir().join("WindowsTerminal-RTL-patcher");
    if let Err(e) = std::fs::create_dir_all(&dir) {
        eprintln!("cannot create {}: {e}", dir.display());
        pause();
        std::process::exit(1);
    }
    let script = dir.join("patch-wt.ps1");
    if let Err(e) = std::fs::write(&script, PS1) {
        eprintln!("cannot write {}: {e}", script.display());
        pause();
        std::process::exit(1);
    }

    println!("---");
    let status = Command::new("powershell.exe")
        .args(["-NoProfile", "-ExecutionPolicy", "Bypass", "-File"])
        .arg(&script)
        .args(&args)
        .status();

    match status {
        Ok(s) if s.success() => {
            println!("---");
            println!("Done. Run it again with -Revert if you want the original files back.");
        }
        Ok(s) => {
            println!("---");
            eprintln!("The patch script reported a failure (exit {:?}).", s.code());
        }
        Err(e) => {
            eprintln!("could not start powershell.exe: {e}");
        }
    }

    pause();
}