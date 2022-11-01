// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod flatland_actor;
mod flatland_environment;
mod flatland_instance;
mod input_actor;
mod pointer_state;

use {
    argh::FromArgs, flatland_environment::FlatlandEnvironment, fuchsia_async as fasync,
    stress_test::run_test, tracing::Level,
};

#[derive(Clone, Debug, FromArgs)]
/// Creates an instance of scenic and performs stressful operations on it
pub struct Args {
    /// number of operations to complete before exiting.
    #[argh(option, short = 'o')]
    num_operations: Option<u64>,

    /// filter logging by level (off, error, warn, info, debug, trace)
    #[argh(option, short = 'l')]
    log_filter: Option<Level>,

    /// if set, the test runs for this time limit before exiting successfully.
    #[argh(option, short = 't')]
    time_limit_secs: Option<u64>,

    /// flag passed in by rust test runner
    #[argh(switch)]
    #[allow(unused)]
    nocapture: bool,
}

#[fasync::run_singlethreaded(test)]
async fn test() {
    // Get arguments from command line
    let args: Args = argh::from_env();

    match args.log_filter {
        Some(filter) => diagnostics_log::init!(&[], diagnostics_log::interest(filter)),
        None => diagnostics_log::init!(),
    }

    // Setup the scenic environment
    let env = FlatlandEnvironment::new(args).await;

    // Run the test
    run_test(env).await;
}
