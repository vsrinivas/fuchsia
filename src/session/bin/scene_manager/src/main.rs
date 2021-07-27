// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    ::input_pipeline::text_settings,
    anyhow::Error,
    fidl_fuchsia_input_injection::InputDeviceRegistryRequestStream,
    fidl_fuchsia_input_keymap as fkeymap,
    fidl_fuchsia_session_scene::{ManagerRequest, ManagerRequestStream},
    fidl_fuchsia_ui_accessibility_view::{RegistryRequest, RegistryRequestStream},
    fidl_fuchsia_ui_scenic::ScenicMarker,
    fuchsia_async as fasync,
    fuchsia_component::{client::connect_to_protocol, server::ServiceFs},
    fuchsia_syslog::fx_log_warn,
    fuchsia_zircon as zx,
    futures::lock::Mutex,
    futures::{StreamExt, TryFutureExt, TryStreamExt},
    scene_management::{self, SceneManager},
    std::sync::Arc,
};

mod input_device_registry_server;
mod input_pipeline;

enum ExposedServices {
    AccessibilityViewRegistry(RegistryRequestStream),
    Manager(ManagerRequestStream),
    InputDeviceRegistry(InputDeviceRegistryRequestStream),
    /// The requests for `fuchsia.input.keymap.Configuration`.
    TextSettingsConfig(fkeymap::ConfigurationRequestStream),
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["scene_manager"]).expect("Failed to init syslog");

    let mut fs = ServiceFs::new_local();
    fs.dir("svc").add_fidl_service(ExposedServices::AccessibilityViewRegistry);
    fs.dir("svc").add_fidl_service(ExposedServices::Manager);
    fs.dir("svc").add_fidl_service(ExposedServices::InputDeviceRegistry);
    fs.dir("svc").add_fidl_service(ExposedServices::TextSettingsConfig);
    fs.take_and_serve_directory_handle()?;

    let (input_device_registry_server, input_device_registry_request_stream_receiver) =
        input_device_registry_server::make_server_and_receiver();

    let mut input_receiver = Some(input_device_registry_request_stream_receiver);

    // text_handler is used to attach keymap and text editing settings to the input events.
    // It also listens to configuration.
    let text_handler = text_settings::Handler::new(None);

    let scenic = connect_to_protocol::<ScenicMarker>()?;
    let flat_scene_manager = scene_management::FlatSceneManager::new(scenic, None, None).await?;
    let scene_manager = Arc::new(Mutex::new(flat_scene_manager));

    while let Some(service_request) = fs.next().await {
        match service_request {
            ExposedServices::AccessibilityViewRegistry(request_stream) => {
                fasync::Task::local(handle_accessibility_view_registry_request_stream(
                    request_stream,
                    Arc::clone(&scene_manager),
                ))
                .detach()
            }
            ExposedServices::Manager(request_stream) => {
                if let Some(input_receiver) = input_receiver {
                    fasync::Task::local(handle_manager_request_stream(
                        request_stream,
                        Arc::clone(&scene_manager),
                        input_receiver,
                        // All text_handler clones share data, so it is OK to clone as needed.
                        text_handler.clone(),
                    ))
                    .detach();
                }
                input_receiver = None;
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
            // Serves calls to fuchsia.input.keymap.Configuration.
            ExposedServices::TextSettingsConfig(request_stream) => {
                let mut handler = text_handler.clone();
                fasync::Task::local(
                    async move { handler.process_keymap_configuration_from(request_stream).await }
                        .unwrap_or_else(|e| {
                            fx_log_warn!(
                                "failed to forward fkeymap::ConfigurationRequestStream: {:?}",
                                e
                            )
                        }),
                )
                .detach();
            }
        }
    }

    Ok(())
}

pub async fn handle_manager_request_stream(
    mut request_stream: ManagerRequestStream,
    scene_manager: Arc<Mutex<scene_management::FlatSceneManager>>,
    input_device_registry_request_stream_receiver: futures::channel::mpsc::UnboundedReceiver<
        InputDeviceRegistryRequestStream,
    >,
    text_handler: text_settings::Handler,
) {
    if let Ok(input_pipeline) = input_pipeline::handle_input(
        scene_manager.clone(),
        input_device_registry_request_stream_receiver,
        text_handler,
    )
    .await
    {
        fasync::Task::local(input_pipeline.handle_input_events()).detach();
    }

    while let Ok(Some(request)) = request_stream.try_next().await {
        match request {
            ManagerRequest::SetRootView { view_provider, responder, .. } => {
                if let Ok(proxy) = view_provider.into_proxy() {
                    let mut scene_manager = scene_manager.lock().await;
                    let mut r = scene_manager.set_root_view(proxy).await.unwrap();
                    let _ = responder.send(&mut r);
                }
            }
            ManagerRequest::RequestFocus { mut view_ref, responder, .. } => {
                let scene_manager = scene_manager.lock().await;
                if let Ok(mut response) = scene_manager.focuser.request_focus(&mut view_ref).await {
                    let _ = responder.send(&mut response);
                }
            }
        };
    }
}

pub async fn handle_accessibility_view_registry_request_stream(
    mut request_stream: RegistryRequestStream,
    scene_manager: Arc<Mutex<scene_management::FlatSceneManager>>,
) {
    while let Ok(Some(request)) = request_stream.try_next().await {
        match request {
            RegistryRequest::CreateAccessibilityViewHolder {
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
                    Err(_) => {
                        responder.control_handle().shutdown_with_epitaph(zx::Status::PEER_CLOSED);
                    }
                };
            }
        };
    }
}

#[cfg(test)]
mod tests {
    use fidl_fuchsia_ui_accessibility_view::RegistryMarker;
    use fidl_fuchsia_ui_scenic as ui_scenic;
    use fidl_fuchsia_ui_views as ui_views;
    use fuchsia_async as fasync;
    use fuchsia_component_test::{builder::*, RealmInstance};
    use fuchsia_scenic as scenic;

    const SCENIC_URL: &str = "fuchsia-pkg://fuchsia.com/scenic#meta/scenic.cmx";

    const MOCK_COBALT_URL: &str = "#meta/mock_cobalt.cm";

    const SCENE_MANAGER_URL: &str = "#meta/scene_manager.cm";

    const FAKE_HDCP_URL: &str = "#meta/hdcp.cm";

    async fn setup_realm() -> Result<RealmInstance, anyhow::Error> {
        let mut builder = RealmBuilder::new().await?;

        builder.add_eager_component("mock_cobalt", ComponentSource::url(MOCK_COBALT_URL)).await?;

        builder
            .add_eager_component("hdcp", ComponentSource::url(FAKE_HDCP_URL))
            .await?
            .add_route(CapabilityRoute {
                capability: Capability::protocol("fuchsia.sysmem.Allocator"),
                source: RouteEndpoint::AboveRoot,
                targets: vec![RouteEndpoint::component("hdcp")],
            })?
            .add_route(CapabilityRoute {
                capability: Capability::protocol("fuchsia.tracing.provider.Registry"),
                source: RouteEndpoint::AboveRoot,
                targets: vec![RouteEndpoint::component("hdcp")],
            })?;

        builder
            .add_eager_component("scenic", ComponentSource::legacy_url(SCENIC_URL))
            .await
            .expect("Failed to start scenic")
            .add_route(CapabilityRoute {
                capability: Capability::protocol("fuchsia.hardware.display.Provider"),
                source: RouteEndpoint::component("hdcp"),
                targets: vec![RouteEndpoint::component("scenic")],
            })?
            .add_route(CapabilityRoute {
                capability: Capability::protocol("fuchsia.scheduler.ProfileProvider"),
                source: RouteEndpoint::AboveRoot,
                targets: vec![RouteEndpoint::component("scenic")],
            })?
            .add_route(CapabilityRoute {
                capability: Capability::protocol("fuchsia.sysmem.Allocator"),
                source: RouteEndpoint::AboveRoot,
                targets: vec![RouteEndpoint::component("scenic")],
            })?
            .add_route(CapabilityRoute {
                capability: Capability::protocol("fuchsia.tracing.provider.Registry"),
                source: RouteEndpoint::AboveRoot,
                targets: vec![RouteEndpoint::component("scenic")],
            })?
            .add_route(CapabilityRoute {
                capability: Capability::protocol("fuchsia.ui.input.ImeService"),
                source: RouteEndpoint::AboveRoot,
                targets: vec![RouteEndpoint::component("scenic")],
            })?
            .add_route(CapabilityRoute {
                capability: Capability::protocol("fuchsia.sysmem.Allocator"),
                source: RouteEndpoint::AboveRoot,
                targets: vec![RouteEndpoint::component("scenic")],
            })?
            .add_route(CapabilityRoute {
                capability: Capability::protocol("fuchsia.vulkan.loader.Loader"),
                source: RouteEndpoint::AboveRoot,
                targets: vec![RouteEndpoint::component("scenic")],
            })?
            .add_route(CapabilityRoute {
                capability: Capability::protocol("fuchsia.cobalt.LoggerFactory"),
                source: RouteEndpoint::component("mock_cobalt"),
                targets: vec![RouteEndpoint::component("scenic")],
            })?
            .add_route(CapabilityRoute {
                capability: Capability::protocol("fuchsia.ui.views.ViewRefInstalled"),
                source: RouteEndpoint::component("scenic"),
                targets: vec![RouteEndpoint::AboveRoot],
            })?
            .add_route(CapabilityRoute {
                capability: Capability::protocol("fuchsia.ui.scenic.Scenic"),
                source: RouteEndpoint::component("scenic"),
                targets: vec![RouteEndpoint::AboveRoot, RouteEndpoint::component("scene_manager")],
            })?
            .add_route(CapabilityRoute {
                capability: Capability::protocol("fuchsia.ui.scenic.Session"),
                source: RouteEndpoint::component("scenic"),
                targets: vec![RouteEndpoint::AboveRoot, RouteEndpoint::component("scene_manager")],
            })?;

        builder
            .add_eager_component("scene_manager", ComponentSource::url(SCENE_MANAGER_URL))
            .await?
            .add_route(CapabilityRoute {
                capability: Capability::protocol("fuchsia.ui.accessibility.view.Registry"),
                source: RouteEndpoint::component("scene_manager"),
                targets: vec![RouteEndpoint::above_root()],
            })?;

        builder.add_route(CapabilityRoute {
            capability: Capability::protocol("fuchsia.logger.LogSink"),
            source: RouteEndpoint::AboveRoot,
            targets: vec![
                RouteEndpoint::component("hdcp"),
                RouteEndpoint::component("mock_cobalt"),
                RouteEndpoint::component("scene_manager"),
                RouteEndpoint::component("scenic"),
            ],
        })?;

        let instance = builder.build().create().await.expect("Failed to build instance");
        Ok(instance)
    }

    #[allow(unused_variables)]
    #[fasync::run_singlethreaded(test)]
    async fn attach_a11y_view() -> Result<(), anyhow::Error> {
        // Setup realm instance.
        let realm_instance = setup_realm().await.expect("Failed to setup realm");

        // Connect to services.
        let accessibility_view_registry_proxy =
            realm_instance.root.connect_to_protocol_at_exposed_dir::<RegistryMarker>()?;

        let view_ref_installed_proxy = realm_instance
            .root
            .connect_to_protocol_at_exposed_dir::<ui_views::ViewRefInstalledMarker>()?;

        let scenic_proxy =
            realm_instance.root.connect_to_protocol_at_exposed_dir::<ui_scenic::ScenicMarker>()?;

        // Instantiate scenic session in which to create a11y view.
        let (session_proxy, session_request_stream) = fidl::endpoints::create_proxy()?;
        scenic_proxy.create_session2(session_request_stream, None, None)?;
        let session = scenic::Session::new(session_proxy);

        // Create a11y ViewRef and ViewToken pairs.
        let mut a11y_view_token_pair = scenic::ViewTokenPair::new()?;
        let a11y_viewref_pair = scenic::ViewRefPair::new()?;
        let mut viewref_dup_for_registry =
            fuchsia_scenic::duplicate_view_ref(&a11y_viewref_pair.view_ref)?;
        let mut viewref_dup_for_watcher =
            fuchsia_scenic::duplicate_view_ref(&a11y_viewref_pair.view_ref)?;

        // Create a11y view and try inserting into scene.
        let a11y_view = scenic::View::new3(
            session.clone(),
            a11y_view_token_pair.view_token,
            a11y_viewref_pair.control_ref,
            a11y_viewref_pair.view_ref,
            Some(String::from("a11y view")),
        );

        let _ = session
            .lock()
            // Passing 0 for requested_presentation_time tells scenic that that it should process
            // enqueued operation as soon as possible.
            // Passing 0 for requested_prediction_span guarantees that scenic will provide at least
            // one future time.
            .present2(0, 0)
            .await
            .expect("Failed to present a11y view");

        accessibility_view_registry_proxy
            .create_accessibility_view_holder(
                &mut viewref_dup_for_registry,
                &mut a11y_view_token_pair.view_holder_token,
            )
            .await
            .expect("Failed to create a11y view holder");

        // Listen for a11y view ref installed event.
        let watch_result = view_ref_installed_proxy.watch(&mut viewref_dup_for_watcher).await;

        match watch_result {
            // Handle fidl::Errors.
            Err(err) => {
                panic!("ViewRefInstalled fidl error: {:?}", err);
            }
            // Handle ui_views::ViewRefInstalledError.
            Ok(Err(err)) => {
                panic!("ViewRefInstalledError: {:?}", err);
            }
            Ok(_) => Ok(()),
        }
    }
}
