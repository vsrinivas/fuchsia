// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::env;
use std::io;

#[fuchsia_async::run_singlethreaded]
async fn main() {
    let args: Vec<String> = env::args().collect();
    if args.len() != 2 {
        eprintln!("Usage: run_test_suite <test_url>\n");
        eprintln!(
            "<test_url> is the URL of the test component to run. It must be a component \
             publishing the fuchsia.test.TestSuite protocol."
        );
        std::process::exit(1);
    }

    println!("\nRunning test '{}'", args[1]);

    let mut stdout = io::stdout();
    let result = run_test_suite_lib::run_test(args[1].clone(), &mut stdout).await;
    if result.is_err() {
        let err = result.unwrap_err();
        println!("Test suite encountered error trying to run tests: {:?}", err);
        std::process::exit(1);
    }

    let (outcome, executed, passed) = result.unwrap();

    println!("\n{} out of {} tests passed...", passed.len(), executed.len());
    println!("{} completed with outcome: {}", &args[1], outcome);

    if outcome == run_test_suite_lib::TestOutcome::Passed {
        std::process::exit(0);
    }
    std::process::exit(1);
}
