// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod mouse_pointer_hack;
mod pointer_hack_server;
mod touch_pointer_hack;
mod workstation_input_pipeline;

use {
    crate::pointer_hack_server::PointerHackServer,
    anyhow::Error,
    fidl::endpoints::DiscoverableService,
    fidl_fuchsia_sys::LauncherMarker,
    fidl_fuchsia_ui_app::ViewProviderMarker,
    fidl_fuchsia_ui_policy::PresentationMarker,
    fidl_fuchsia_ui_scenic::ScenicMarker,
    fuchsia_async as fasync,
    fuchsia_component::client::{connect_to_service, launch_with_options, App, LaunchOptions},
    fuchsia_zircon as zx,
    scene_management::{self, SceneManager},
};

async fn launch_ermine() -> Result<(App, PointerHackServer), Error> {
    let launcher = connect_to_service::<LauncherMarker>()?;

    let (client_chan, server_chan) = zx::Channel::create().unwrap();

    let pointer_hack_server = PointerHackServer::new(server_chan);
    let mut launch_options = LaunchOptions::new();
    launch_options
        .set_additional_services(vec![PresentationMarker::SERVICE_NAME.to_string()], client_chan);

    let app = launch_with_options(
        &launcher,
        "fuchsia-pkg://fuchsia.com/ermine#meta/ermine.cmx".to_string(),
        None,
        launch_options,
    )?;

    Ok((app, pointer_hack_server))
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["workstation_session"]).expect("Failed to initialize logger.");

    let (app, pointer_hack_server) = launch_ermine().await?;
    let view_provider = app.connect_to_service::<ViewProviderMarker>()?;

    let scenic = connect_to_service::<ScenicMarker>()?;
    let mut scene_manager = scene_management::FlatSceneManager::new(scenic, None, None).await?;

    // This node can be used to move the associated view around.
    let _node = scene_manager.add_view_to_scene(view_provider, Some("Ermine".to_string())).await?;

    workstation_input_pipeline::handle_input(scene_manager, &pointer_hack_server).await;

    loop {}
}
