// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fuchsia_async as fasync, fuchsia_zircon as zx,
    session_manager_lib::{service_management, startup},
};

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["session_manager"]).expect("Failed to initialize logger.");

    let (mut session_url, input_injection_svc_channel) = match startup::get_session_url() {
        Some(session_url) => (session_url.clone(), startup::launch_session(&session_url).await?),
        None => (String::new(), zx::Channel::from(zx::Handle::invalid())),
    };

    // Start serving the services exposed by session manager.
    service_management::expose_services(&mut session_url, input_injection_svc_channel).await?;
    Ok(())
}
