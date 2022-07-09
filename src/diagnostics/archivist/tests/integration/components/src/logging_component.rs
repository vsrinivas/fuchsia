// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#![warn(clippy::all)]

use fuchsia_async as fasync;
use fuchsia_zircon as zx;

#[fuchsia::main(logging_tags = ["logging component"], logging_minimum_severity = "debug")]
async fn main() {
    tracing::debug!("my debug message.");
    tracing::info!("my info message.");
    tracing::warn!("my warn message.");

    // TODO(fxbug.dev/79121): Component manager may send the Stop event to Archivist before it
    // sends CapabilityRequested. In this case the logs may be lost. This sleep delays
    // terminating the test to give component manager time to send the CapabilityRequested
    // event. This sleep should be removed once component manager orders the events.
    fasync::Timer::new(fasync::Time::after(zx::Duration::from_seconds(2))).await
}
