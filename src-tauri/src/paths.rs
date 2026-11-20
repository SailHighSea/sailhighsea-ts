//! Where the app keeps its data, including portable mode.
//!
//! Portable mode (desktop only) is on when a `portable.txt` sits next to the program (or the `--portable` flag is given):
//! settings, favorites and history then live in a `data/` folder next to the program. Next to the program means
//!  - Linux AppImage: the folder that holds the .AppImage file (read from `$APPIMAGE`, the executable itself is in a temp mount)
//!  - macOS: the folder that holds the .app bundle
//!  - otherwise: the folder of the executable
//! If that folder cannot be written to (Program Files, /Applications), the normal app-data folder is used instead.

use std::fs;
use std::path::{Path, PathBuf};

#[derive(Clone, Debug)]
pub struct DataLocation {
    pub dir: PathBuf,
    pub portable: bool,
    /// Portable mode was asked for but the folder is read-only, so the app-data folder is used.
    pub portable_fallback: bool,
    /// Where the old C app kept its files, for the one-time import.
    pub legacy_dir: Option<PathBuf>,
}

/// The folder "next to the program" in the sense described above.
pub fn base_dir() -> Option<PathBuf> {
    if let Some(img) = std::env::var_os("APPIMAGE") {
        if let Some(p) = Path::new(&img).parent() {
            return Some(p.to_path_buf());
        }
    }
    let exe = std::env::current_exe().ok()?;
    if cfg!(target_os = "macos") {
        for a in exe.ancestors() {
            if a.extension().map_or(false, |e| e.eq_ignore_ascii_case("app")) {
                return a.parent().map(Path::to_path_buf);
            }
        }
    }
    exe.parent().map(Path::to_path_buf)
}

pub fn dir_writable(dir: &Path) -> bool {
    if fs::create_dir_all(dir).is_err() {
        return false;
    }
    let probe = dir.join(".write-test");
    let ok = fs::write(&probe, b"x").is_ok();
    let _ = fs::remove_file(probe);
    ok
}

/// Pure decision, so it can be tested: `base` is the folder next to the program, `app_data` the normal data folder.
pub fn decide(args: &[String], base: Option<&Path>, app_data: PathBuf, mobile: bool) -> DataLocation {
    if mobile {
        return DataLocation { dir: app_data, portable: false, portable_fallback: false, legacy_dir: None };
    }
    let wants = args.iter().any(|a| a == "--portable") || base.map_or(false, |b| b.join("portable.txt").exists());
    let legacy_dir = base.map(Path::to_path_buf);
    if wants {
        if let Some(b) = base {
            let d = b.join("data");
            if dir_writable(&d) {
                return DataLocation { dir: d, portable: true, portable_fallback: false, legacy_dir };
            }
        }
        return DataLocation { dir: app_data, portable: false, portable_fallback: true, legacy_dir };
    }
    DataLocation { dir: app_data, portable: false, portable_fallback: false, legacy_dir }
}

pub fn resolve(args: &[String], app_data: PathBuf) -> DataLocation {
    decide(args, base_dir().as_deref(), app_data, cfg!(any(target_os = "android", target_os = "ios")))
}

#[cfg(test)]
mod tests {
    use super::*;

    fn temp(name: &str) -> PathBuf {
        let d = std::env::temp_dir().join(format!("shs-paths-{}-{}", name, std::process::id()));
        let _ = fs::remove_dir_all(&d);
        fs::create_dir_all(&d).unwrap();
        d
    }

    #[test]
    fn portable_marker_and_flag() {
        let base = temp("base");
        let app = temp("appdata");
        let none: Vec<String> = vec![];
        let l = decide(&none, Some(&base), app.clone(), false);
        assert!(!l.portable && l.dir == app);

        fs::write(base.join("portable.txt"), "").unwrap();
        let l = decide(&none, Some(&base), app.clone(), false);
        assert!(l.portable && l.dir == base.join("data") && l.dir.is_dir());

        fs::remove_file(base.join("portable.txt")).unwrap();
        let l = decide(&["--portable".to_string()], Some(&base), app.clone(), false);
        assert!(l.portable);

        let l = decide(&["--portable".to_string()], Some(&base), app.clone(), true);
        assert!(!l.portable && l.dir == app); // Android has no portable mode
        let _ = fs::remove_dir_all(base);
        let _ = fs::remove_dir_all(app);
    }

    #[test]
    fn unwritable_base_falls_back() {
        let app = temp("appdata2");
        // a path below a file cannot be created
        let base = temp("base2");
        let blocker = base.join("file");
        fs::write(&blocker, "x").unwrap();
        let l = decide(&["--portable".to_string()], Some(&blocker), app.clone(), false);
        assert!(!l.portable && l.portable_fallback && l.dir == app);
        let _ = fs::remove_dir_all(base);
        let _ = fs::remove_dir_all(app);
    }
}
