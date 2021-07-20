// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Helpers for writing test cases.

use {
    anyhow::{Context, Result},
    std::{
        ffi::OsString,
        fs::{create_dir_all, metadata, set_permissions, OpenOptions},
        io::Write,
        os::unix::fs::PermissionsExt,
        path::PathBuf,
        process,
    },
    tempfile::TempDir,
};

const USER_READ_EXECUTE: u32 = 0o500;

/// Provide temporary HOME, PATH env settings, removing the directory and
/// restoring the env on drop.
///
/// It's strongly advised to use with some form of test serialization such as
/// the serialize_test crate. The env is global to the process, so running tests
/// with env changes in parallel will be confusing.
#[cfg(not(target_os = "fuchsia"))]
pub struct TempTestEnv {
    /// Path to the original bash (outside of this temp env).
    pub bash_shell: PathBuf,

    /// Path to the user home directory (outside of this temp env).
    pub old_home: Option<OsString>,

    /// Original $PATH env var (outside of this temp env).
    pub old_path: Option<OsString>,

    /// Path to the original bin directory (outside of this temp env).
    pub bin: PathBuf,

    /// Path to the original etc directory (outside of this temp env).
    pub etc: PathBuf,

    /// Path fake home directory (inside of this temp env).
    pub home: PathBuf,

    /// Path to the root directory of the test env (this temp env).
    pub root: TempDir,

    /// Whether to keep the temp directory contents after execution.
    pub keep: bool,
}

impl TempTestEnv {
    /// Create a temp directory and override the HOME and PATH env vars.
    ///
    /// Env settings restored on drop, but the temp directory (and contents)
    /// will be kept (not removed on drop).
    /// Intended for debugging.
    pub fn keep() -> Result<Self> {
        TempTestEnv::new_with_options(true)
    }

    /// Create a temp directory and override the HOME and PATH env vars.
    ///
    /// Env settings restored on drop.
    pub fn new() -> Result<Self> {
        TempTestEnv::new_with_options(false)
    }

    /// Create a temp directory and override the HOME and PATH env vars.
    ///
    /// Env settings restored on drop.
    ///
    /// `keep` - if true temp dir will not be removed on drop.
    pub fn new_with_options(keep: bool) -> Result<Self> {
        let bash_shell = which("bash")?;
        let old_home = std::env::var_os("HOME");
        let old_path = std::env::var_os("PATH");

        let root = TempDir::new()?;

        let bin = root.path().join("bin");
        create_dir_all(&bin)?;

        let etc = root.path().join("etc");
        create_dir_all(&etc)?;

        let home = root.path().join("home").join("test_user");
        create_dir_all(&home)?;

        std::env::set_var("HOME", &home.as_path());
        std::env::set_var("PATH", &bin.as_path());
        Ok(Self { bash_shell, old_home, old_path, bin, etc, home, root, keep })
    }

    /// Create a test exe in the temp bin directory with `contents`.
    ///
    /// The shebang line (e.g. `#!/usr/bin/env bash\n`) is added automatically.
    pub fn create_bash_exe(&self, name: &str, contents: &str) -> Result<()> {
        let path = self.bin.join(name);
        OpenOptions::new()
            .create(true)
            .write(true)
            .open(&path)
            .context("create_no_op_exe open")?
            .write(format!("#!{}\n{}", &self.bash_shell.display(), &contents).as_bytes())
            .context("create_no_op_exe file")?;
        let mut permissions = metadata(&path).context("create_no_op_exe metadata")?.permissions();
        permissions.set_mode(USER_READ_EXECUTE);
        set_permissions(&path, permissions).context("create_no_op_exe set permissions")?;
        Ok(())
    }

    /// Create a test exe (that does nothing) in the temp bin directory.
    pub fn create_no_op_exe(&self, name: &str) -> Result<()> {
        self.create_bash_exe(name, &"")
    }

    /// executable, based on the original (non-test) PATH.
    pub fn original(&self, cmd: &str) -> Result<String> {
        let old_path: OsString = self.old_path.as_ref().unwrap().into();
        let output = process::Command::new("/usr/bin/env")
            .arg("which")
            .arg(cmd)
            .env("PATH", old_path)
            .output()?;
        Ok(String::from_utf8_lossy(&output.stdout).trim().to_string())
    }
}

impl Drop for TempTestEnv {
    fn drop(&mut self) {
        if let Some(s) = &self.old_home {
            std::env::set_var("HOME", s);
        } else {
            std::env::remove_var("HOME");
        }
        if let Some(s) = &self.old_path {
            std::env::set_var("PATH", s);
        } else {
            std::env::remove_var("PATH");
        }
        if self.keep {
            let owned = std::mem::replace(
                &mut self.root,
                TempDir::new().expect("Need to replace temp directory to keep it (keep() failed)."),
            );
            let path = owned.into_path();
            let notes = format!(
                "Keeping temp dir {:?} for debugging: \
                Consider using `rm -rf {:?}` when you're done.",
                path, path
            );
            if std::thread::panicking() {
                println!("{}", notes);
                return;
            }

            // This panic serves two purposes:
            // - Causes the test to show console output (successful tests don't
            //   normally display the stderr/stdout output).
            // - Deters checking in tests that use `keep()` to avoid creating
            //   debris on CI/CQ test machines. (Tip: do not check in tests that
            //   use `keep()`.
            panic!("Using TempTestEnv::keep() intentionally panics when dropped. {}", notes);
            // Note: the 'path' (temp directory) is intentionally not removed.
        }
    }
}

/// Determine the path to an executable based on the current PATH.
pub fn which(cmd: &str) -> Result<PathBuf> {
    let output = process::Command::new("/usr/bin/env").arg("which").arg(cmd).output()?;
    Ok(PathBuf::from(String::from_utf8_lossy(&output.stdout).trim()))
}

#[cfg(test)]
mod test {
    use {super::*, serial_test::serial, std::process::Command};

    #[test]
    #[serial]
    fn test_test_env() {
        let test_env = TempTestEnv::new().expect("create test env");
        assert!(test_env.bin.exists());
        assert!(test_env.etc.exists());
        assert!(test_env.home.exists());
        assert!(test_env.root.path().exists());
        assert_eq!(std::env::var_os("HOME"), Some(OsString::from(&test_env.home)));
        assert_eq!(std::env::var_os("PATH"), Some(OsString::from(&test_env.bin)));

        let fake_exe_path = test_env.bin.join("fake-exe");
        assert!(!fake_exe_path.exists());
        test_env.create_no_op_exe("fake-exe").expect("create exe");
        assert!(fake_exe_path.exists());
        let mut child = Command::new("fake-exe").spawn().expect("run fake-exe");
        assert_eq!(child.wait().expect("wait").code(), Some(0));
    }

    #[test]
    #[serial]
    fn test_no_residue() {
        let test_path: PathBuf;
        let original_home = std::env::var_os("HOME");
        let original_path = std::env::var_os("PATH");
        {
            let test_env = TempTestEnv::new().expect("create test env");
            test_path = test_env.root.path().to_path_buf();
            assert!(test_path.exists());
            assert_ne!(original_home, std::env::var_os("HOME"));
            assert_ne!(original_path, std::env::var_os("PATH"));
        }
        assert!(!test_path.display().to_string().is_empty());
        assert!(!test_path.exists());
        assert_eq!(original_home, std::env::var_os("HOME"));
        assert_eq!(original_path, std::env::var_os("PATH"));
    }

    #[test]
    #[serial]
    fn test_original() {
        let test_env = TempTestEnv::new().expect("create test env");
        let path = test_env.original("bash").expect("path");
        assert!(!path.is_empty());
    }

    #[test]
    #[serial]
    fn test_which() {
        let path = which("bash").expect("which");
        assert!(path.is_absolute());
        assert!(path.has_root());
    }

    #[test]
    #[serial]
    #[should_panic(expected = "panics when dropped")]
    fn test_keep_panic() {
        TempTestEnv::keep().expect("create test env");
    }

    #[test]
    #[serial]
    #[should_panic(expected = "The test panic.")]
    fn test_panic() {
        // Create named var to extend life to end of block.
        let _test_env = TempTestEnv::keep().expect("create test env");
        panic!("The test panic.");
    }
}
