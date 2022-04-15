// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::io::Write;

#[fuchsia::test(
    logging_tags = ["log_then_hang"]
)]
async fn log_then_hang() {
    println!("stdout from hanging test");
    writeln!(std::io::stderr(), "stderr from hanging test").unwrap();
    tracing::info!("syslog from hanging test");

    loop {
        // deliberate hang
    }
}
