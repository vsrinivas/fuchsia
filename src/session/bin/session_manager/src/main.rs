// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error, fidl_fuchsia_component as fcomponent,
    fuchsia_component::client::connect_to_protocol, session_manager_config::Config,
    session_manager_lib::session_manager::SessionManager, tracing::info,
};

#[fuchsia::component]
async fn main() -> Result<(), Error> {
    let realm = connect_to_protocol::<fcomponent::RealmMarker>()?;
    // Start the startup session, if any, and serve services exposed by session manager.
    let mut session_manager = SessionManager::new(realm);
    let Config { session_url } = Config::from_args();
    // TODO(fxbug.dev/67789): Using ? here causes errors to not be logged.
    if !session_url.is_empty() {
        session_manager
            .launch_startup_session(session_url)
            .await
            .expect("failed to launch session");
    } else {
        info!("Received an empty startup session URL, waiting for a request.");
    }
    session_manager.serve().await.expect("failed to serve protocols");

    Ok(())
}
