// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod display_provider;
mod environment;
mod tap_actor;

use {
    anyhow::Error,
    argh::FromArgs,
    environment::ScenicEnvironment,
    fuchsia_async as fasync,
    log::LevelFilter,
    stress_test::{run_test, StdoutLogger},
};

#[derive(Clone, Debug, FromArgs)]
/// Creates an instance of scenic and performs stressful operations on it
pub struct Args {
    /// number of operations to complete before exiting.
    #[argh(option, short = 'o')]
    num_operations: Option<u64>,

    /// filter logging by level (off, error, warn, info, debug, trace)
    #[argh(option, short = 'l')]
    log_filter: Option<LevelFilter>,

    /// controls delay between each touch operation (down, up)
    #[argh(option, short = 'd', default = "1")]
    touch_delay_secs: u64,

    /// if set, the test runs for this time limit before exiting successfully.
    #[argh(option, short = 't')]
    time_limit_secs: Option<u64>,
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    // Get arguments from command line
    let args: Args = argh::from_env();

    // Initialize logging
    StdoutLogger::init(args.log_filter.unwrap_or(LevelFilter::Info));

    // Setup the scenic environment
    let env = ScenicEnvironment::new(args).await;

    // Run the test
    run_test(env).await;

    Ok(())
}
