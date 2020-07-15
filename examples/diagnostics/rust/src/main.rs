// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[fuchsia_async::run_singlethreaded]
async fn main() -> Result<(), anyhow::Error> {
    fuchsia_syslog::init()?;
    fuchsia_syslog::set_severity(fuchsia_syslog::levels::DEBUG);
    tracing::trace!("should not print");
    tracing::debug!("should print");
    tracing::info!({ foo = 1, bar = "baz" }, "hello, world!");
    log::warn!("warning: using old api");
    Ok(())
}
