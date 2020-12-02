// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error, fidl_fuchsia_sys2 as fsys, fidl_fuchsia_ui_lifecycle as fui_lifecycle,
    fuchsia_async as fasync, fuchsia_component::client::connect_to_service,
    session_manager_lib::session_manager::SessionManager,
};

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["session_manager"]).expect("Failed to initialize logger.");

    let realm = connect_to_service::<fsys::RealmMarker>()?;
    let scenic_lifecycle = match connect_to_service::<fui_lifecycle::LifecycleControllerMarker>() {
        Ok(scenic_lifecycle) => Some(scenic_lifecycle),
        Err(_) => None,
    };

    // Start the startup session, if any, and serve services exposed by session manager.
    let mut session_manager = SessionManager::new(realm, scenic_lifecycle);
    session_manager.launch_startup_session().await?;
    session_manager.expose_services().await?;

    Ok(())
}
