// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod account_manager;

use anyhow::Error;
use fuchsia_async as fasync;
use fuchsia_component::server::ServiceFs;
use futures::StreamExt;
use log::info;

use crate::account_manager::AccountManager;

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["pw_auth"]).expect("Can't init logger");
    info!("Starting password authenticator");

    let mut fs = ServiceFs::new();

    // Add FIDL services here once we have protocols for them.
    fs.dir("svc").add_fidl_service(|stream| {
        // TODO(zarvox): don't detach futures; tear them down deterministically
        fasync::Task::spawn(AccountManager::handle_requests_for_stream(stream)).detach();
    });

    fs.take_and_serve_directory_handle()?;

    fs.collect::<()>().await;

    Ok(())
}

#[cfg(test)]
mod test {
    // Add tests once we have behavior to test.
}
