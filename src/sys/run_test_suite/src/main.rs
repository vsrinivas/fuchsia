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
}

#[fuchsia_async::run_singlethreaded]
async fn main() {
    let args: Args = argh::from_env();

    println!("\nRunning test '{}'", args.test_url);

    let mut stdout = io::stdout();
    let result =
        run_test_suite_lib::run_test(args.test_url.clone(), &mut stdout, args.timeout).await;
    if result.is_err() {
        let err = result.unwrap_err();
        println!("Test suite encountered error trying to run tests: {:?}", err);
        std::process::exit(1);
    }

    let run_result = result.unwrap();

    println!("\n{} out of {} tests passed...", run_result.passed.len(), run_result.executed.len());
    println!("{} completed with result: {}", &args.test_url, run_result.outcome);
    if run_result.outcome == run_test_suite_lib::Outcome::Timedout {
        std::process::exit(-fuchsia_zircon::Status::TIMED_OUT.into_raw());
    }

    if !run_result.successful_completion {
        println!("{} did not complete successfully.", &args.test_url);
        std::process::exit(1);
    }

    if run_result.outcome == run_test_suite_lib::Outcome::Passed {
        std::process::exit(0);
    }
    std::process::exit(1);
}
