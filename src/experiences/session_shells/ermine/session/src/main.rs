// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[macro_use]
mod input_testing_utilities;
mod element_repository;
mod mouse_pointer_hack;
mod pointer_hack_server;
mod touch_pointer_hack;
mod workstation_input_pipeline;

use {
    crate::{
        element_repository::{ElementEventHandler, ElementManagerServer, ElementRepository},
        pointer_hack_server::PointerHackServer,
    },
    anyhow::{Context as _, Error},
    element_management::SimpleElementManager,
    fidl::endpoints::DiscoverableService,
    fidl_fuchsia_session::{
        ElementManagerMarker, ElementManagerRequestStream, GraphicalPresenterMarker,
    },
    fidl_fuchsia_sys::LauncherMarker,
    fidl_fuchsia_sys2 as fsys,
    fidl_fuchsia_ui_app::ViewProviderMarker,
    fidl_fuchsia_ui_policy::PresentationMarker,
    fidl_fuchsia_ui_scenic::ScenicMarker,
    fuchsia_async as fasync,
    fuchsia_component::{
        client::{connect_to_service, launch_with_options, App, LaunchOptions},
        server::ServiceFs,
    },
    fuchsia_syslog::fx_log_info,
    fuchsia_zircon as zx,
    futures::{try_join, StreamExt},
    scene_management::{self, SceneManager},
    std::rc::Rc,
};

enum ExposedServices {
    ElementManager(ElementManagerRequestStream),
}

/// The maximum number of open requests to this component.
///
/// Currently we have this value set low because the only service we are serving
/// is the ElementManager service and we don't expect many connections to it at
/// any given time.
const NUM_CONCURRENT_REQUESTS: usize = 5;

async fn launch_ermine(
    element_server: ElementManagerServer<SimpleElementManager>,
) -> Result<(App, PointerHackServer), Error> {
    let launcher = connect_to_service::<LauncherMarker>()?;

    let (client_chan, server_chan) = zx::Channel::create().unwrap();

    let pointer_hack_server = PointerHackServer::new(server_chan, element_server);
    let mut launch_options = LaunchOptions::new();
    launch_options.set_additional_services(
        vec![
            PresentationMarker::SERVICE_NAME.to_string(),
            ElementManagerMarker::SERVICE_NAME.to_string(),
        ],
        client_chan,
    );

    let app = launch_with_options(
        &launcher,
        "fuchsia-pkg://fuchsia.com/ermine#meta/ermine.cmx".to_string(),
        None,
        launch_options,
    )?;

    Ok((app, pointer_hack_server))
}

async fn expose_services(
    element_server: ElementManagerServer<SimpleElementManager>,
) -> Result<(), Error> {
    let mut fs = ServiceFs::new_local();
    fs.dir("svc").add_fidl_service(ExposedServices::ElementManager);

    fs.take_and_serve_directory_handle()?;

    // create a reference so that we can use this within the `for_each_concurrent` generator.
    // If we do not create a ref we will run into issues with the borrow checker.
    let element_server_ref = &element_server;
    fs.for_each_concurrent(
        NUM_CONCURRENT_REQUESTS,
        move |service_request: ExposedServices| async move {
            match service_request {
                ExposedServices::ElementManager(request_stream) => {
                    // TODO(47079): handle error
                    fx_log_info!("received incoming element manager request");
                    let _ = element_server_ref.handle_request(request_stream).await;
                }
            }
        },
    )
    .await;
    Ok(())
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["workstation_session"]).expect("Failed to initialize logger.");
    let realm =
        connect_to_service::<fsys::RealmMarker>().context("Could not connect to Realm service.")?;
    let element_manager = SimpleElementManager::new(realm);

    let mut element_repository = ElementRepository::new(Rc::new(element_manager));

    let (app, pointer_hack_server) = launch_ermine(element_repository.make_server()).await?;
    let view_provider = app.connect_to_service::<ViewProviderMarker>()?;

    let presenter = app.connect_to_service::<GraphicalPresenterMarker>()?;
    let mut handler = ElementEventHandler::new(presenter);

    let scenic = connect_to_service::<ScenicMarker>()?;
    let mut scene_manager = scene_management::FlatSceneManager::new(scenic, None, None).await?;

    // This node can be used to move the associated view around.
    let _node = scene_manager.add_view_to_scene(view_provider, Some("Ermine".to_string())).await?;

    let services_fut = expose_services(element_repository.make_server());
    let input_fut = workstation_input_pipeline::handle_input(scene_manager, &pointer_hack_server);
    let element_manager_fut = element_repository.run_with_handler(&mut handler);
    let focus_fut = input::focus_listening::handle_focus_changes();

    //TODO(47080) monitor the futures to see if they complete in an error.
    let _ = try_join!(services_fut, input_fut, element_manager_fut, focus_fut);

    element_repository.shutdown()?;

    Ok(())
}
