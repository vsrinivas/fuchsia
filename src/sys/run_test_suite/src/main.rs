// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, run_test_suite_lib::DisabledTestHandling, std::io};

#[derive(FromArgs)]
/// Arguments
struct Args {
    /// test timeout. Exits with -`ZX_ERR_TIMED_OUT` if the test times out.
    #[argh(option, short = 't')]
    timeout: Option<u32>,

    /// test url. Test should implement `fuchsia.test.Suite` protocol.
    #[argh(positional)]
    test_url: String,

    /// test filter. A glob pattern for matching tests.
    #[argh(option)]
    test_filter: Option<String>,

    /// whether to also run tests that have been marked disabled/ignored by the test author.
    #[argh(switch)]
    also_run_disabled_tests: bool,
}

#[fuchsia_async::run_singlethreaded]
async fn main() {
    let Args { timeout, test_url, test_filter, also_run_disabled_tests } = argh::from_env();

    println!("\nRunning test '{}'", &test_url);

    let disabled_tests = if also_run_disabled_tests {
        DisabledTestHandling::Include
    } else {
        DisabledTestHandling::Exclude
    };

    let mut stdout = io::stdout();
    let run_test_suite_lib::RunResult { outcome, executed, passed, successful_completion } =
        match run_test_suite_lib::run_test(
            test_url.clone(),
            &mut stdout,
            timeout.and_then(std::num::NonZeroU32::new),
            test_filter.as_ref().map(String::as_str),
            disabled_tests,
        )
        .await
        {
            Ok(run_result) => run_result,
            Err(err) => {
                println!("Test suite encountered error trying to run tests: {:?}", err);
                std::process::exit(1);
            }
        };

    println!("{} out of {} tests passed...", passed.len(), executed.len());
    println!("{} completed with result: {}", &test_url, outcome);

    if !successful_completion {
        println!("{} did not complete successfully.", &test_url);
    }

    match outcome {
        run_test_suite_lib::Outcome::Passed => {}
        run_test_suite_lib::Outcome::Timedout => {
            std::process::exit(-fuchsia_zircon::Status::TIMED_OUT.into_raw());
        }
        run_test_suite_lib::Outcome::Failed
        | run_test_suite_lib::Outcome::Inconclusive
        | run_test_suite_lib::Outcome::Error => {
            std::process::exit(1);
        }
    }
}
