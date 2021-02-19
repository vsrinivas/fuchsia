// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {fuchsia_syslog, tracing};

#[test]
fn log_and_exit() {
    fuchsia_syslog::init_with_tags(&["log_and_exit_test"]).expect("initializing logging");
    fuchsia_syslog::set_severity(fuchsia_syslog::levels::DEBUG);
    tracing::info!("my info message");
    tracing::warn!("my warn message");
    tracing::error!("my error message");
}
