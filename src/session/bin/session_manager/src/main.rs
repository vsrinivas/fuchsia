// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error, fidl_fuchsia_sys2 as fsys, fuchsia_async as fasync,
    fuchsia_component::client::connect_to_protocol, fuchsia_syslog,
    session_manager_lib::session_manager::SessionManager,
};

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["session_manager"]).expect("Failed to initialize logger.");

    let realm = connect_to_protocol::<fsys::RealmMarker>()?;
    // Start the startup session, if any, and serve services exposed by session manager.
    let mut session_manager = SessionManager::new(realm);
    // TODO(fxbug.dev/67789): Using ? here causes errors to not be logged.
    session_manager.launch_startup_session().await.expect("failed to launch session");
    session_manager.serve().await.expect("failed to serve protocols");

    Ok(())
}
