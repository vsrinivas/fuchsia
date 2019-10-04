// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::Error,
    fuchsia_async as fasync, fuchsia_zircon as zx,
    session_manager::{cobalt, startup},
};

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let start_time = zx::Time::get(zx::ClockId::Monotonic);
    startup::launch_session().await?;
    let end_time = zx::Time::get(zx::ClockId::Monotonic);

    let cobalt_logger = cobalt::get_logger()?;
    let session_url = startup::get_session_url();
    cobalt::log_session_launch_time(cobalt_logger, &session_url, start_time, end_time).await?;
    Ok(())
}
