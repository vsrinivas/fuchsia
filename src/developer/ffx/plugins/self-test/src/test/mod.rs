// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, bail, ensure, Context, Result},
    errors::ffx_bail,
    fuchsia_async::TimeoutExt,
    serde_json::Value,
    std::process::Stdio,
    std::{future::Future, pin::Pin},
    std::{io::Write, process::Command, time::Duration},
    termion::is_tty,
};

pub mod asserts;

/// Create a new ffx isolate. This method relies on the environment provided by
/// the ffx binary and should only be called within ffx.
pub async fn new_isolate(name: &str) -> Result<ffx_isolate::Isolate> {
    // This method is always called from within ffx.
    let ffx_path = std::env::current_exe().expect("could not determine own path");
    let ffx_path = std::fs::canonicalize(ffx_path).expect("could not canonicalize own path");
    let ssh_key = ffx_config::get::<String, _>("ssh.priv").await?.into();

    ffx_isolate::Isolate::new(name, ffx_path, ssh_key).await
}

/// Get the target nodename we're expected to interact with in this test, or
/// pick the first discovered target. If nodename is set via $FUCHSIA_NODENAME
/// that is returned, if the nodename is not given, and zero targets are found,
/// this is also an error.
pub async fn get_target_nodename() -> Result<String> {
    if let Ok(nodename) = std::env::var("FUCHSIA_NODENAME") {
        return Ok(nodename);
    }

    let isolate = new_isolate("initial-target-discovery").await?;

    // ensure a daemon is spun up first, so we have a moment to discover targets.
    let out = isolate.ffx(&["target", "wait", "-t", "5"]).await?;
    if !out.status.success() {
        bail!("No targets found after 5s")
    }

    let out = isolate.ffx(&["target", "list", "-f", "j"]).await.context("getting target list")?;

    ensure!(out.status.success(), "Looking up a target name failed: {:?}", out);

    let targets: Value =
        serde_json::from_str(&out.stdout).context("parsing output from target list")?;

    let targets = targets.as_array().ok_or(anyhow!("expected target list ot return an array"))?;

    let target = targets
        .iter()
        .find(|target| {
            target["nodename"] != ""
                && target["target_state"]
                    .as_str()
                    .map(|s| s.to_lowercase().contains("product"))
                    .unwrap_or(false)
        })
        .ok_or(anyhow!("did not find any named targets in a product state"))?;
    target["nodename"]
        .as_str()
        .map(|s| s.to_string())
        .ok_or(anyhow!("expected product state target to have a nodename"))
}

/// cleanup tries to give the daemon a chance to exit but eventually runs pkill to ensure nothing
/// is left running.
async fn cleanup() -> Result<()> {
    let max_attempts = 4;
    let daemon_start_arg = "(^|/)ffx (-.* )?daemon start$";

    for attempt in 0..max_attempts {
        let did_find_daemon = fuchsia_async::unblock(move || {
            let mut cmd = Command::new("pgrep");
            cmd.arg("-f")
                .arg(daemon_start_arg)
                .stdin(Stdio::null())
                .stdout(Stdio::null())
                .stderr(Stdio::null());
            let status = cmd.status()?;
            Ok::<_, anyhow::Error>(status.success())
        })
        .await?;

        if did_find_daemon {
            // Daemon stop waits 20ms before exiting so we try to avoid a race by waiting here.
            // Based on max_attempts of 4 this will wait a maximum of 500ms.
            fuchsia_async::Timer::new(Duration::from_millis(50 * (1 + attempt))).await;
            continue;
        } else {
            return Ok(());
        }
    }

    // Success here means that pkill was able to find something to kill so that means a daemon
    // was still running that we did not expect to be running. We return an error here to make
    // this a failure of the test suite as a whole to find out when this is happening.
    let mut cmd = Command::new("pkill");
    cmd.arg("-f")
        .arg(daemon_start_arg)
        .stdin(Stdio::null())
        .stdout(Stdio::null())
        .stderr(Stdio::null());
    let did_kill_daemon = cmd.status()?.success();
    if did_kill_daemon {
        // We do not return an error here because that was causing flakes.
        // I think we should still clean up here but we need a less racey shutdown path before we
        // can return an error here to keep the flake rate down.
        eprintln!("A daemon was killed that was not supposed to be running");
    }
    Ok(())
}

/// run runs the given set of tests printing results to stdout and exiting
/// with 0 or 1 if the tests passed or failed, respectively.
pub async fn run(tests: Vec<TestCase>, timeout: Duration, case_timeout: Duration) -> Result<()> {
    let mut writer = std::io::stdout();
    let color = is_tty(&writer);
    let green = green(color);
    let red = red(color);
    let nocol = nocol(color);

    let test_result = async {
        let num_tests = tests.len();

        writeln!(&mut writer, "1..{}", num_tests)?;

        let mut num_errs: usize = 0;
        for (i, tc) in tests.iter().enumerate().map(|(i, tc)| (i + 1, tc)) {
            write!(&mut writer, "{nocol}{i}. {name} - ", name = tc.name)?;
            writer.flush()?;
            match (tc.f)()
                .on_timeout(case_timeout, || ffx_bail!("timed out after {:?}", case_timeout))
                .await
            {
                Ok(()) => {
                    writeln!(&mut writer, "{green}ok{nocol}",)?;
                }
                Err(err) => {
                    writeln!(&mut writer, "{red}not ok{nocol}:\n{err:?}\n",)?;
                    num_errs = num_errs + 1;
                }
            }
        }

        if num_errs != 0 {
            ffx_bail!("{red}{num_errs}/{num_tests} failed{nocol}");
        } else {
            writeln!(&mut writer, "{green}{num_tests}/{num_tests} passed{nocol}",)?;
        }

        Ok(())
    }
    .on_timeout(timeout, || ffx_bail!("timed out after {:?}", timeout))
    .await;

    let cleanup_result = cleanup().await;

    test_result.and(cleanup_result)
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
    pub name: &'static str,
    f: TestFn,
}

impl TestCase {
    pub fn new(name: &'static str, f: TestFn) -> Self {
        Self { name, f }
    }
}
