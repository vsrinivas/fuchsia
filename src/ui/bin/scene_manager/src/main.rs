// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fidl::prelude::*,
    fidl_fuchsia_accessibility::{MagnificationHandlerMarker, MagnifierMarker},
    fidl_fuchsia_accessibility_scene as a11y_view,
    fidl_fuchsia_input_injection::InputDeviceRegistryRequestStream,
    fidl_fuchsia_session_scene::{
        ManagerRequest as SceneManagerRequest, ManagerRequestStream as SceneManagerRequestStream,
    },
    fidl_fuchsia_ui_accessibility_view::{
        RegistryRequest as A11yViewRegistryRequest,
        RegistryRequestStream as A11yViewRegistryRequestStream,
    },
    fidl_fuchsia_ui_app as ui_app, fidl_fuchsia_ui_composition as fland,
    fidl_fuchsia_ui_input_config::FeaturesRequestStream as InputConfigFeaturesRequestStream,
    fidl_fuchsia_ui_scenic::ScenicMarker,
    fidl_fuchsia_ui_views as ui_views, fuchsia_async as fasync,
    fuchsia_component::{client::connect_to_protocol, server::ServiceFs},
    fuchsia_inspect as inspect,
    fuchsia_syslog::{fx_log_err, fx_log_info, fx_log_warn},
    fuchsia_zircon as zx,
    futures::lock::Mutex,
    futures::{StreamExt, TryStreamExt},
    scene_management::{self, SceneManager},
    std::rc::Rc,
    std::sync::Arc,
};

mod input_config_server;
mod input_device_registry_server;
mod input_pipeline;

enum ExposedServices {
    AccessibilityViewRegistry(A11yViewRegistryRequestStream),
    SceneManager(SceneManagerRequestStream),
    InputDeviceRegistry(InputDeviceRegistryRequestStream),
    InputConfigFeatures(InputConfigFeaturesRequestStream),
}

#[fuchsia::main(logging_tags = [ "scene_manager" ])]
async fn main() -> Result<(), Error> {
    let mut fs = ServiceFs::new_local();

    inspect_runtime::serve(inspect::component::inspector(), &mut fs)?;

    fs.dir("svc").add_fidl_service(ExposedServices::AccessibilityViewRegistry);
    fs.dir("svc").add_fidl_service(ExposedServices::SceneManager);
    fs.dir("svc").add_fidl_service(ExposedServices::InputDeviceRegistry);
    fs.dir("svc").add_fidl_service(ExposedServices::InputConfigFeatures);
    fs.take_and_serve_directory_handle()?;

    let (input_device_registry_server, input_device_registry_request_stream_receiver) =
        input_device_registry_server::make_server_and_receiver();

    let (input_config_server, input_config_receiver) =
        input_config_server::make_server_and_receiver();

    // This call should normally never fail. The ICU data loader must be kept alive to ensure
    // Unicode data is kept in memory.
    let icu_data_loader = icu_data::Loader::new().unwrap();

    let scenic = connect_to_protocol::<ScenicMarker>()?;

    let use_flatland = scenic.uses_flatland().await.expect("Failed to get flatland info.");
    let display_ownership =
        scenic.get_display_ownership_event().await.expect("Failed to get display ownership.");
    fx_log_info!("Instantiating SceneManager, use_flatland: {:?}", use_flatland);

    let scene_manager: Arc<Mutex<Box<dyn SceneManager>>> = if use_flatland {
        // TODO(fxbug.dev/86379): Support for insertion of accessibility view.  Pass ViewRefInstalled
        // to the SceneManager, the same way we do for the Gfx branch.
        let display = connect_to_protocol::<fland::FlatlandDisplayMarker>()?;
        let root_flatland = connect_to_protocol::<fland::FlatlandMarker>()?;
        let pointerinjector_flatland = connect_to_protocol::<fland::FlatlandMarker>()?;
        let a11y_flatland = connect_to_protocol::<fland::FlatlandMarker>()?;
        let cursor_view_provider = connect_to_protocol::<ui_app::ViewProviderMarker>()?;
        let a11y_view_provider = connect_to_protocol::<a11y_view::ProviderMarker>()?;
        Arc::new(Mutex::new(Box::new(
            scene_management::FlatlandSceneManager::new(
                scenic,
                display,
                pointerinjector_flatland,
                root_flatland,
                a11y_flatland,
                cursor_view_provider,
                a11y_view_provider,
            )
            .await?,
        )))
    } else {
        let view_ref_installed = connect_to_protocol::<ui_views::ViewRefInstalledMarker>()?;
        let gfx_scene_manager: Arc<Mutex<Box<dyn SceneManager>>> = Arc::new(Mutex::new(Box::new(
            scene_management::GfxSceneManager::new(scenic, view_ref_installed, None, None).await?,
        )));
        if let Err(e) = register_gfx_as_magnifier(Arc::clone(&gfx_scene_manager)) {
            fx_log_warn!("failed to register as the magnification handler: {:?}", e);
        }
        gfx_scene_manager
    };

    // Create a node under root to hang all input pipeline inspect data off of.
    let inspect_node =
        Rc::new(inspect::component::inspector().root().create_child("input_pipeline"));

    // Start input pipeline.
    if let Ok(input_pipeline) = input_pipeline::handle_input(
        use_flatland,
        scene_manager.clone(),
        input_config_receiver,
        input_device_registry_request_stream_receiver,
        icu_data_loader,
        &inspect_node.clone(),
        display_ownership,
    )
    .await
    {
        fasync::Task::local(input_pipeline.handle_input_events()).detach();
    }

    while let Some(service_request) = fs.next().await {
        match service_request {
            ExposedServices::AccessibilityViewRegistry(request_stream) => {
                fasync::Task::local(handle_accessibility_view_registry_request_stream(
                    request_stream,
                    Arc::clone(&scene_manager),
                ))
                .detach()
            }
            ExposedServices::SceneManager(request_stream) => {
                fasync::Task::local(handle_scene_manager_request_stream(
                    request_stream,
                    Arc::clone(&scene_manager),
                ))
                .detach();
            }
            ExposedServices::InputDeviceRegistry(request_stream) => {
                match &input_device_registry_server.handle_request(request_stream).await {
                    Ok(()) => (),
                    Err(e) => {
                        // If `handle_request()` returns `Err`, then the `unbounded_send()` call
                        // from `handle_request()` failed with either:
                        // * `TrySendError::SendErrorKind::Full`, or
                        // * `TrySendError::SendErrorKind::Disconnected`.
                        //
                        // These are unexpected, because:
                        // * `Full` can't happen, because `InputDeviceRegistryServer`
                        //   uses an `UnboundedSender`.
                        // * `Disconnected` is highly unlikely, because the corresponding
                        //   `UnboundedReceiver` lives in `main::input_fut`, and `input_fut`'s
                        //   lifetime is nearly as long as `input_device_registry_server`'s.
                        //
                        // Nonetheless, InputDeviceRegistry isn't critical to production use.
                        // So we just log the error and move on.
                        fx_log_warn!(
                            "failed to forward InputDeviceRegistryRequestStream: {:?}; \
                                must restart to enable input injection",
                            e
                        )
                    }
                }
            }
            ExposedServices::InputConfigFeatures(request_stream) => {
                match &input_config_server.handle_request(request_stream).await {
                    Ok(()) => (),
                    Err(e) => {
                        fx_log_warn!("failed to forward InputConfigFeaturesRequestStream: {:?}", e)
                    }
                }
            }
        }
    }

    fx_log_info!("Finished service handler loop; exiting main.");
    Ok(())
}

pub async fn handle_scene_manager_request_stream(
    mut request_stream: SceneManagerRequestStream,
    scene_manager: Arc<Mutex<Box<dyn scene_management::SceneManager>>>,
) {
    while let Ok(Some(request)) = request_stream.try_next().await {
        match request {
            SceneManagerRequest::SetRootView { view_provider, responder } => {
                if let Ok(proxy) = view_provider.into_proxy() {
                    let mut scene_manager = scene_manager.lock().await;
                    match scene_manager.set_root_view(proxy).await {
                        Ok(mut view_ref) => match responder.send(&mut view_ref) {
                            Ok(_) => {}
                            Err(e) => {
                                fx_log_err!("Error responding to SetRootView(): {}", e);
                            }
                        },
                        Err(e) => {
                            // Log an error and close the connection.  This can be a consequence of
                            // the child View not connecting to the scene graph (hence we don't
                            // receive the ViewRef to return), or perhaps an internal bug which
                            // requires further investigation.
                            fx_log_err!("Failed to obtain ViewRef from set_root_view(): {}", e);
                        }
                    }
                }
            }
        };
    }
}

pub async fn handle_accessibility_view_registry_request_stream(
    mut request_stream: A11yViewRegistryRequestStream,
    scene_manager: Arc<Mutex<Box<dyn scene_management::SceneManager>>>,
) {
    while let Ok(Some(request)) = request_stream.try_next().await {
        match request {
            A11yViewRegistryRequest::CreateAccessibilityViewHolder {
                a11y_view_ref: _,
                a11y_view_token,
                responder,
                ..
            } => {
                let mut scene_manager = scene_manager.lock().await;
                let r = scene_manager.insert_a11y_view(a11y_view_token);

                let _ = match r {
                    Ok(mut result) => {
                        let _ = responder.send(&mut result);
                    }
                    Err(e) => {
                        fx_log_warn!("Closing A11yViewRegistry connection due to error {:?}", e);
                        responder.control_handle().shutdown_with_epitaph(zx::Status::PEER_CLOSED);
                    }
                };
            }
            A11yViewRegistryRequest::CreateAccessibilityViewport {
                viewport_creation_token: _,
                responder,
                ..
            } => {
                fx_log_err!("A11yViewRegistry.CreateAccessibilityViewport not implemented!");
                responder.control_handle().shutdown_with_epitaph(zx::Status::PEER_CLOSED);
            }
        };
    }
}

fn register_gfx_as_magnifier(
    scene_manager: Arc<Mutex<Box<dyn SceneManager>>>,
) -> Result<(), anyhow::Error> {
    let (magnification_handler_client, magnification_handler_server) =
        fidl::endpoints::create_request_stream::<MagnificationHandlerMarker>()?;
    scene_management::GfxSceneManager::handle_magnification_handler_request_stream(
        magnification_handler_server,
        scene_manager,
    );
    let magnifier_proxy = connect_to_protocol::<MagnifierMarker>()?;
    magnifier_proxy.register_handler(magnification_handler_client)?;
    Ok(())
}
