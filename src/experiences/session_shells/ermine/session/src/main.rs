// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This declaration is required to support the `select!`.
#![recursion_limit = "256"]

#[macro_use]
mod element_repository;

use {
    crate::element_repository::{ElementEventHandler, ElementManagerServer, ElementRepository},
    anyhow::{Context as _, Error},
    fidl::endpoints::{ClientEnd, DiscoverableService, Proxy},
    fidl_fuchsia_session::{
        ElementManagerMarker, ElementManagerRequestStream, GraphicalPresenterMarker,
    },
    fidl_fuchsia_session_scene,
    fidl_fuchsia_session_scene::{ManagerMarker, ManagerProxy},
    fidl_fuchsia_sys::LauncherMarker,
    fidl_fuchsia_sys2 as fsys,
    fidl_fuchsia_ui_app::ViewProviderMarker,
    fidl_fuchsia_ui_policy::{PresentationMarker, PresentationRequest, PresentationRequestStream},
    fidl_fuchsia_ui_views as ui_views,
    fidl_fuchsia_ui_views::ViewRefInstalledMarker,
    fuchsia_async as fasync,
    fuchsia_component::{
        client::{connect_to_protocol, launch_with_options, App, LaunchOptions},
        server::ServiceFs,
    },
    fuchsia_zircon as zx,
    futures::try_join,
    futures::StreamExt,
    futures::TryStreamExt,
    legacy_element_management::SimpleElementManager,
    std::fs,
    std::rc::Rc,
    std::sync::{Arc, Weak},
};

enum ExposedServices {
    ElementManager(ElementManagerRequestStream),
    Presentation(PresentationRequestStream),
}

/// The maximum number of open requests to this component.
///
/// Currently we have this value set low because the only service we are serving
/// is the ElementManager service and we don't expect many connections to it at
/// any given time.
const NUM_CONCURRENT_REQUESTS: usize = 5;

async fn launch_ermine() -> Result<(App, zx::Channel), Error> {
    let launcher = connect_to_protocol::<LauncherMarker>()?;

    let (client_chan, server_chan) = zx::Channel::create().unwrap();

    let mut launch_options = LaunchOptions::new();
    launch_options.set_additional_services(
        vec![
            PresentationMarker::SERVICE_NAME.to_string(),
            ElementManagerMarker::SERVICE_NAME.to_string(),
        ],
        client_chan,
    );

    // Check if shell is overridden. Otherwise use the default ermine shell.
    let shell_url = match fs::read_to_string("/config/data/shell") {
        Ok(url) => url,
        Err(_) => "fuchsia-pkg://fuchsia.com/ermine#meta/ermine.cmx".to_string()
    };

    let app = launch_with_options(
        &launcher,
        shell_url,
        None,
        launch_options,
    )?;

    Ok((app, server_chan))
}

async fn expose_services(
    element_server: ElementManagerServer<SimpleElementManager>,
    scene_manager: Arc<ManagerProxy>,
    server_chan: zx::Channel,
) -> Result<(), Error> {
    let mut fs = ServiceFs::new();

    // Add services for component outgoing directory.
    fs.dir("svc")
        .add_fidl_service(ExposedServices::ElementManager)
        .add_fidl_service(ExposedServices::Presentation);
    fs.take_and_serve_directory_handle()?;

    // Add services served over `server_chan`.
    fs.add_fidl_service_at(ElementManagerMarker::SERVICE_NAME, ExposedServices::ElementManager);
    fs.add_fidl_service_at(PresentationMarker::SERVICE_NAME, ExposedServices::Presentation);
    fs.serve_connection(server_chan).unwrap();

    // create a reference so that we can use this within the `for_each_concurrent` generator.
    // If we do not create a ref we will run into issues with the borrow checker.
    let element_server_ref = &element_server;
    fs.for_each_concurrent(NUM_CONCURRENT_REQUESTS, |service_request: ExposedServices| async {
        match service_request {
            ExposedServices::ElementManager(request_stream) => {
                // TODO(47079): handle error
                let _ = element_server_ref.handle_request(request_stream).await;
            }
            ExposedServices::Presentation(request_stream) => {
                // TODO(47079): handle error
                let _ =
                    handle_presentation_request_stream(request_stream, scene_manager.clone()).await;
            }
        }
    })
    .await;
    Ok(())
}

/// The presentation's pointer events are forwarded to the scene manager,
/// since the scene manager component contains the input pipeline.
pub async fn handle_presentation_request_stream(
    mut request_stream: PresentationRequestStream,
    scene_manager: Arc<ManagerProxy>,
) {
    while let Ok(Some(request)) = request_stream.try_next().await {
        match request {
            PresentationRequest::CapturePointerEventsHack { listener, .. } => {
                let _ = scene_manager.capture_pointer_events(listener);
            }
        }
    }
}

async fn set_view_focus(
    weak_focuser: Weak<fidl_fuchsia_session_scene::ManagerProxy>,
    mut view_ref: ui_views::ViewRef,
) -> Result<(), Error> {
    // [ViewRef]'s are one-shot use only. Duplicate it for use in request_focus below.
    let mut viewref_dup = fuchsia_scenic::duplicate_view_ref(&view_ref)?;

    // Wait for the view_ref to signal its ready to be focused.
    let view_ref_installed = connect_to_protocol::<ViewRefInstalledMarker>()
        .context("Could not connect to ViewRefInstalledMarker")?;
    let watch_result = view_ref_installed.watch(&mut view_ref).await;
    match watch_result {
        // Handle fidl::Errors.
        Err(e) => Err(anyhow::format_err!("Failed with err: {}", e)),
        // Handle ui_views::ViewRefInstalledError.
        Ok(Err(value)) => Err(anyhow::format_err!("Failed with err: {:?}", value)),
        Ok(_) => {
            // Now set focus on the view_ref.
            if let Some(focuser) = weak_focuser.upgrade() {
                let focus_result = focuser.request_focus(&mut viewref_dup).await?;
                match focus_result {
                    Ok(()) => Ok(()),
                    Err(e) => Err(anyhow::format_err!("Failed with err: {:?}", e)),
                }
            } else {
                Err(anyhow::format_err!("Failed to acquire Focuser"))
            }
        }
    }
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["workstation_session"]).expect("Failed to initialize logger.");
    let realm = connect_to_protocol::<fsys::RealmMarker>()
        .context("Could not connect to Realm service.")?;
    let element_manager = SimpleElementManager::new(realm);

    let mut element_repository = ElementRepository::new(Rc::new(element_manager));

    let (app, element_channel) = launch_ermine().await?;
    let view_provider = app.connect_to_protocol::<ViewProviderMarker>()?;

    let presenter = app.connect_to_protocol::<GraphicalPresenterMarker>()?;
    let mut handler = ElementEventHandler::new(presenter);

    let scene_manager = Arc::new(connect_to_protocol::<ManagerMarker>().unwrap());

    let scene_channel: ClientEnd<ViewProviderMarker> = view_provider
        .into_channel()
        .expect("no other users of the wrapped channel")
        .into_zx_channel()
        .into();
    let view_ref = scene_manager.set_root_view(scene_channel.into()).await.unwrap();

    let set_focus_fut = set_view_focus(Arc::downgrade(&scene_manager), view_ref);

    let services_fut =
        expose_services(element_repository.make_server(), scene_manager, element_channel);
    let element_manager_fut = element_repository.run_with_handler(&mut handler);
    let focus_fut = input_pipeline::focus_listening::handle_focus_changes();

    //TODO(47080) monitor the futures to see if they complete in an error.
    let _ = try_join!(element_manager_fut, focus_fut, services_fut, set_focus_fut);

    element_repository.shutdown()?;

    Ok(())
}
