// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await)]

use {
    failure::{self, Error, ResultExt},
    fuchsia_async as fasync,
    session_manager_lib::startup,
};

const NUM_THREADS: usize = 1;

fn main() -> Result<(), Error> {
    let mut executor = fasync::Executor::new().context("Error creating executor.")?;
    executor
        .run(startup::launch_root_session(), NUM_THREADS)
        .context("Error launching root session.")
        .expect("Error launching root session.");

    Ok(())
}
