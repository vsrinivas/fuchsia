// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::io;

#[fuchsia_async::run_singlethreaded]
async fn main() {
    // TODO(anmittal): Introduce fidl protocol to run any tests.
    // This is just for demoing the initial prototype.
    let test_url = "fuchsia-pkg://fuchsia.com/example-tests#meta/echo_test_realm.cm";

    println!("\nRunning test '{}'", test_url);

    let mut stdout = io::stdout();
    let result = test_manager_lib::run_test(test_url.to_string(), &mut stdout).await;
    if result.is_err() {
        let err = result.unwrap_err();
        println!("Test suite encountered error trying to run tests: {:?}", err);
        std::process::exit(1);
    }

    let (outcome, executed, passed) = result.unwrap();

    println!("\n{} out of {} tests passed...", passed.len(), executed.len());
    println!("{} completed with outcome: {}", test_url, outcome);

    if outcome == test_manager_lib::TestOutcome::Passed {
        std::process::exit(0);
    }
    std::process::exit(1);
}
