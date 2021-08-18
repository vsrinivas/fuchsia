// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

use diagnostics_log::{self, Interest, PublishOptions, Severity};

#[fuchsia::component(logging = false)]
async fn main() {
    let _ = diagnostics_log::init_publishing(PublishOptions {
        interest: Interest { min_severity: Some(Severity::Debug), ..Interest::EMPTY },
        ..Default::default()
    })
    .unwrap();

    tracing::debug!("my debug message.");
    tracing::info!("my info message.");
    tracing::warn!("my warn message.");
}
