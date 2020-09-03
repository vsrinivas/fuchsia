// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, fidl_fuchsia_test_manager::HarnessMarker};

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

    /// run test cases in parallel, up to the number provided.
    #[argh(option)]
    parallel: Option<u16>,
}

#[fuchsia_async::run_singlethreaded]
async fn main() {
    let Args { timeout, test_url, test_filter, also_run_disabled_tests, parallel } =
        argh::from_env();
    let harness = fuchsia_component::client::connect_to_service::<HarnessMarker>()
        .expect("connecting to HarnessProxy");

    match run_test_suite_lib::run_tests_and_get_outcome(
        test_url,
        timeout.and_then(std::num::NonZeroU32::new),
        test_filter,
        also_run_disabled_tests,
        parallel,
        harness,
    )
    .await
    {
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
