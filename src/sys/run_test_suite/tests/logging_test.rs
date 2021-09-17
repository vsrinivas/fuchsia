// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    diagnostics_log::{self, Interest, Severity},
    tracing,
};

// TODO(fxbug.dev/82789): use fuchsia crate to init logging when it supports setting a minimum
// log severity.
#[fuchsia::test(logging = false)]
async fn log_and_exit() {
    let _ = diagnostics_log::init_publishing(diagnostics_log::PublishOptions {
        tags: Some(&["log_and_exit_test", "logging_test"]),
        interest: Interest { min_severity: Some(Severity::Debug), ..Interest::EMPTY },
        ..Default::default()
    })
    .unwrap();
    tracing::debug!("my debug message");
    tracing::info!("my info message");
    tracing::warn!("my warn message");

    // TODO(fxbug.dev/79121): Component manager may send the Stop event to Archivist before it
    // sends CapabilityRequested. In this case the logs may be lost. This sleep delays
    // terminating the test to give component manager time to send the CapabilityRequested
    // event. This sleep should be removed once component manager orders the events.
    std::thread::sleep(std::time::Duration::from_secs(2));
}
