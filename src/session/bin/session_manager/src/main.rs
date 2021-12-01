// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error, fidl_fuchsia_component as fcomponent,
    fuchsia_component::client::connect_to_protocol,
    session_manager_lib::session_manager::SessionManager,
};

#[fuchsia::component]
async fn main() -> Result<(), Error> {
    let realm = connect_to_protocol::<fcomponent::RealmMarker>()?;
    // Start the startup session, if any, and serve services exposed by session manager.
    let mut session_manager = SessionManager::new(realm);
    // TODO(fxbug.dev/67789): Using ? here causes errors to not be logged.
    session_manager.launch_startup_session().await.expect("failed to launch session");
    session_manager.serve().await.expect("failed to serve protocols");

    Ok(())
}
