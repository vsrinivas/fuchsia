// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fuchsia_runtime::{self as fruntime, HandleInfo, HandleType},
    fuchsia_syslog::{self as fxlog, fx_log_err, fx_log_info},
    fuchsia_zircon::{self as zx},
    std::process,
};

/// Example which takes the Lifecycle handle passed by the Runner. We simply
/// close the channel and exit after taking the handle.
fn main() {
    fxlog::init().expect("logger failed to start");
    match fruntime::take_startup_handle(HandleInfo::new(HandleType::Lifecycle, 0)) {
        Some(lifecycle_handle) => {
            fx_log_info!("Lifecycle channel received.");
            // We could start waiting for a message on this channel which
            // would tell us to stop. Instead we close it, indicating to our
            // Runner that we are done.
            let x: zx::Channel = lifecycle_handle.into();
            drop(x);

            // Technically we could just fall off the end of main, but for
            // example purposes we exit explicitly.
            process::exit(0);
        }
        None => {
            // We did not receive a lifecycle channel, exit abnormally.
            fx_log_err!("No lifecycle channel received, exiting.");
            process::abort();
        }
    }
}
