// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, std::io};

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
}

#[fuchsia_async::run_singlethreaded]
async fn main() {
    let Args { timeout, test_url, test_filter } = argh::from_env();

    println!("\nRunning test '{}'", &test_url);

    let mut stdout = io::stdout();
    let run_test_suite_lib::RunResult { outcome, executed, passed, successful_completion } =
        match run_test_suite_lib::run_test(
            test_url.clone(),
            &mut stdout,
            timeout.and_then(std::num::NonZeroU32::new),
            test_filter.as_ref().map(String::as_str),
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
