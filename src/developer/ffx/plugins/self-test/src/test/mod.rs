// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    ffx_core::ffx_bail,
    fuchsia_async::TimeoutExt,
    std::{env, process::Command, time::Duration},
    std::{future::Future, pin::Pin},
    termion::is_tty,
};

pub mod asserts;

/// ffx constructs a std::process::Command with the given arguments set to be passed to the ffx binary.
pub fn ffx(args: &[&str]) -> std::process::Command {
    let mut ffx_path = env::current_exe().expect("could not determine own path");
    // when we daemonize, our path will change to /, so get the canonical path before that occurs.
    ffx_path = std::fs::canonicalize(ffx_path).expect("could not canonicalize own path");
    let mut cmd = Command::new(ffx_path);
    cmd.args(args);
    cmd
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
