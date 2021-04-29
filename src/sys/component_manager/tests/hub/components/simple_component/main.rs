// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {fuchsia_async as fasync, fuchsia_syslog as syslog, futures::future::pending, log::info};

#[fasync::run_singlethreaded]
/// Simple program that never terminates
async fn main() {
    syslog::init_with_tags(&["simple_component"]).unwrap();
    info!("Child created!");
    pending::<()>().await;
}
