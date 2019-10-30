// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::Error,
    fuchsia_async as fasync,
    session_manager_lib::{service_management, startup},
};

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    // Launch the session which was provided to the session manager at startup.
    fuchsia_syslog::init_with_tags(&["session_manager"]).expect("Failed to initialize logger.");
    let session_url = startup::get_session_url();
    startup::launch_session(&session_url).await?;

    // Start serving the services exposed by session manager.
    service_management::expose_services().await?;
    Ok(())
}
