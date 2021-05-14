// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod environment;
mod hub;
mod tree_actor;

use {
    environment::TreeStressorEnvironment,
    fuchsia_async as fasync,
    log::LevelFilter,
    stress_test::{run_test, StdoutLogger},
};

#[fasync::run_singlethreaded(test)]
pub async fn test() {
    // Initialize logging
    StdoutLogger::init(LevelFilter::Info);

    // TODO(529520): The time limit for this test is reduced due to handle count issues.
    // Setup the environment to run for 2 hours with no limit on operation count
    let env = TreeStressorEnvironment::new(Some(7200), None, 2000).await;

    // Run the test
    run_test(env).await;
}
