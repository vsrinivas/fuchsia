// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    ffx_core::ffx_bail,
    fuchsia_async::TimeoutExt,
    std::process::Stdio,
    std::{env, path::PathBuf, process::Command, time::Duration},
    std::{future::Future, pin::Pin},
    tempfile::TempDir,
    termion::is_tty,
};

pub mod asserts;

pub struct Isolate {
    _tmpdir: TempDir,

    home_dir: PathBuf,
    xdg_config_home: PathBuf,
    ascendd_path: PathBuf,
}

impl Isolate {
    /// new creates a new isolated environment for ffx to run in, including a
    /// user level configuration that isolates the ascendd socket into a temporary
    /// directory. If $FUCHSIA_TEST_OUTDIR is set, then it is used as the log
    /// directory. The isolated environment is torn down when the Isolate is
    /// dropped, which will attempt to terminate any running daemon and then
    /// remove all isolate files.
    pub fn new(name: &str) -> Result<Isolate> {
        let tmpdir = tempfile::Builder::new().prefix(name).tempdir()?;
        let home_dir = tmpdir.path().join("user-home");
        let tmp_dir = tmpdir.path().join("tmp");

        let log_dir = if let Ok(d) = std::env::var("FUCHSIA_TEST_OUTDIR") {
            PathBuf::from(d)
        } else {
            tmpdir.path().join("log")
        };

        for dir in [&home_dir, &tmp_dir, &log_dir].iter() {
            std::fs::create_dir_all(dir)?;
        }

        let ascendd_path = tmp_dir.join("ascendd");

        let xdg_config_home = home_dir.join(".local/share");

        let user_config_dir = xdg_config_home.join("Fuchsia/ffx/config");
        std::fs::create_dir_all(&user_config_dir)?;

        // TODO(raggi): use structured programming to write the JSON
        std::fs::write(
            user_config_dir.join("config.json"),
            format!(
                r##"{{
            "log": {{
                "enabled": true,
                "dir": "{}"
            }},
            "overnet": {{
                "socket": "{}"
            }},
            "test": {{
                "is-isolated": true
            }}
            }}"##,
                log_dir.to_string_lossy(),
                ascendd_path.to_string_lossy()
            ),
        )?;

        std::fs::write(
            user_config_dir.join(".ffx_env"),
            format!(
                r##"{{
            "user": "{}",
            "build": null,
            "global": null
            }}"##,
                user_config_dir.join("config.json").to_string_lossy()
            ),
        )?;

        Ok(Isolate {
            _tmpdir: tmpdir,
            home_dir: home_dir,
            xdg_config_home: xdg_config_home,
            ascendd_path: ascendd_path,
        })
    }

    /// ffx constructs a std::process::Command with the given arguments set to be passed to the ffx binary.
    pub fn ffx(&self, args: &[&str]) -> std::process::Command {
        let mut ffx_path = env::current_exe().expect("could not determine own path");
        // when we daemonize, our path will change to /, so get the canonical path before that occurs.
        ffx_path = std::fs::canonicalize(ffx_path).expect("could not canonicalize own path");
        let mut cmd = Command::new(ffx_path);
        cmd.args(args);
        cmd.env_clear();
        cmd.env("HOME", &*self.home_dir);
        cmd.env("XDG_CONFIG_HOME", &*self.xdg_config_home);
        cmd
    }
}

impl Drop for Isolate {
    fn drop(&mut self) {
        // TODO(raggi): implement a unix socket peer probe here, and avoid
        // invoking ffx daemon stop if there's nothing on the other end.
        let _ = self.ascendd_path;

        let mut cmd = self.ffx(&["daemon", "stop"]);
        cmd.stdin(Stdio::null());
        cmd.stdout(Stdio::null());
        cmd.stderr(Stdio::null());
        let _ = cmd.spawn().map(|mut child| child.wait());
    }
}

/// run runs the given set of tests printing results to stdout and exiting
/// with 0 or 1 if the tests passed or failed, respectively.
pub async fn run(tests: Vec<TestCase>, timeout: Duration, case_timeout: Duration) -> Result<()> {
    let color = is_tty(&std::io::stdout());

    async {
        let num_tests = tests.len();

        println!("1..{}", num_tests);

        let mut num_errs: usize = 0;
        for (i, tc) in tests.iter().enumerate().map(|(i, tc)| (i + 1, tc)) {
            match (tc.f)()
                .on_timeout(case_timeout, || ffx_bail!("timed out after {:?}", case_timeout))
                .await
            {
                Ok(()) => {
                    println!("{}ok {}{} - {}", green(color), i, nocol(color), tc.name);
                }
                Err(err) => {
                    println!("{}not ok {}{} - {} {:?}", red(color), i, nocol(color), tc.name, err);
                    num_errs = num_errs + 1;
                }
            }
        }

        if num_errs != 0 {
            ffx_bail!("{}{}/{} failed{}", red(color), num_errs, num_tests, nocol(color));
        } else {
            println!("{}{}/{} passed{}", green(color), num_tests, num_tests, nocol(color));
        }

        Ok(())
    }
    .on_timeout(timeout, || ffx_bail!("timed out after {:?}", timeout))
    .await
}

fn green(color: bool) -> &'static str {
    if color {
        termion::color::Green.fg_str()
    } else {
        ""
    }
}
fn red(color: bool) -> &'static str {
    if color {
        termion::color::Red.fg_str()
    } else {
        ""
    }
}
fn nocol(color: bool) -> &'static str {
    if color {
        termion::color::Reset.fg_str()
    } else {
        ""
    }
}

#[macro_export]
macro_rules! tests {
    ( $( $x:expr ),* $(,)* ) => {
        {
            let mut temp_vec = Vec::new();
            $(
                temp_vec.push($crate::test::TestCase::new(stringify!($x), move || Box::pin($x())));
            )*
            temp_vec
        }
    };
}

pub type TestFn = fn() -> Pin<Box<dyn Future<Output = Result<()>>>>;

pub struct TestCase {
    name: &'static str,
    f: TestFn,
}

impl TestCase {
    pub fn new(name: &'static str, f: TestFn) -> Self {
        Self { name: name, f: f }
    }
}
