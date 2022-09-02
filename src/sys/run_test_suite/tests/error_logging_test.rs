// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[fuchsia::test(
    logging_tags = ["error_logging_test"],
    logging_minimum_severity = "debug"
)]
async fn log_and_exit() {
    tracing::info!("my info message");
    tracing::warn!("my warn message");
    tracing::error!("my error message");

    // TODO(fxbug.dev/79121): Component manager may send the Stop event to Archivist before it
    // sends CapabilityRequested. In this case the logs may be lost. This sleep delays
    // terminating the test to give component manager time to send the CapabilityRequested
    // event. This sleep should be removed once component manager orders the events.
    std::thread::sleep(std::time::Duration::from_secs(8));
}
