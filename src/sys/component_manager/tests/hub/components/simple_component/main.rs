// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error, fuchsia_async as fasync, fuchsia_syslog as syslog, fuchsia_zircon as zx, log::*,
};

#[fasync::run_singlethreaded]
/// Simple program that emits some logs and exits after 30 days
async fn main() -> Result<(), Error> {
    syslog::init_with_tags(&["simple_component"])?;
    info!("Child created!");
    fasync::Timer::new(fasync::Time::after(zx::Duration::from_hours(24 * 30))).await;
    Ok(())
}
