// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error, fidl_fuchsia_sys2 as fsys, fidl_fuchsia_ui_lifecycle as fui_lifecycle,
    fuchsia_async as fasync, fuchsia_component::client::connect_to_service, fuchsia_syslog,
    session_manager_lib::session_manager::SessionManager, std::sync::Arc,
};

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["session_manager"]).expect("Failed to initialize logger.");

    let realm = connect_to_service::<fsys::RealmMarker>()?;
    let ui_lifecycle_factory =
        Arc::new(|| connect_to_service::<fui_lifecycle::LifecycleControllerMarker>().ok());

    // Start the startup session, if any, and serve services exposed by session manager.
    let mut session_manager = SessionManager::new(realm, ui_lifecycle_factory);
    // TODO(fxbug.dev/67789): Using ? here causes errors to not be logged.
    session_manager.launch_startup_session().await.expect("failed to launch session");
    session_manager.expose_services().await.expect("failed to expose services");

    Ok(())
}
