// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    ::input_pipeline::text_settings,
    anyhow::Error,
    fidl::prelude::*,
    fidl_fuchsia_input_injection::InputDeviceRegistryRequestStream,
    fidl_fuchsia_input_keymap as fkeymap,
    fidl_fuchsia_session_scene::{ManagerRequest, ManagerRequestStream},
    fidl_fuchsia_ui_accessibility_view::{RegistryRequest, RegistryRequestStream},
    fidl_fuchsia_ui_scenic::ScenicMarker,
    fidl_fuchsia_ui_views as ui_views, fuchsia_async as fasync,
    fuchsia_component::{client::connect_to_protocol, server::ServiceFs},
    fuchsia_inspect as inspect,
    fuchsia_syslog::fx_log_warn,
    fuchsia_zircon as zx,
    futures::lock::Mutex,
    futures::{StreamExt, TryFutureExt, TryStreamExt},
    scene_management::{self, SceneManager},
    std::rc::Rc,
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

    inspect_runtime::serve(inspect::component::inspector(), &mut fs)?;

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
    let view_ref_installed = connect_to_protocol::<ui_views::ViewRefInstalledMarker>()?;
    let flat_scene_manager =
        scene_management::FlatSceneManager::new(scenic, view_ref_installed, None, None).await?;
    let scene_manager = Arc::new(Mutex::new(flat_scene_manager));

    // Create a node under root to hang all input pipeline inspect data off of.
    let inspect_node =
        Rc::new(inspect::component::inspector().root().create_child("input_pipeline"));

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
                        inspect_node.clone(),
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
                let handler = text_handler.clone();
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
    text_handler: Rc<text_settings::Handler>,
    inspect_root: Rc<inspect::Node>,
) {
    if let Ok(input_pipeline) = input_pipeline::handle_input(
        scene_manager.clone(),
        input_device_registry_request_stream_receiver,
        text_handler,
        &inspect_root,
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
    use anyhow::format_err;
    use fidl::endpoints::create_endpoints;
    use fidl_fuchsia_session_scene::{
        ManagerMarker as SceneManagerMarker, ManagerProxy as SceneManagerProxy,
    };
    use fidl_fuchsia_ui_accessibility_view::{RegistryMarker, RegistryProxy};
    use fidl_fuchsia_ui_app::{ViewProviderMarker, ViewProviderRequest};
    use fidl_fuchsia_ui_scenic as ui_scenic;
    use fidl_fuchsia_ui_views as ui_views;
    use fidl_fuchsia_ui_views::ViewToken;
    use fuchsia_async as fasync;
    use fuchsia_component_test::{builder::*, RealmInstance};
    use fuchsia_scenic as scenic;
    use futures::TryStreamExt;

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
                targets: vec![RouteEndpoint::AboveRoot, RouteEndpoint::component("scene_manager")],
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
            })?
            .add_route(CapabilityRoute {
                capability: Capability::protocol("fuchsia.ui.views.ViewRefFocused"),
                source: RouteEndpoint::component("scenic"),
                targets: vec![RouteEndpoint::AboveRoot],
            })?;

        builder
            .add_eager_component("scene_manager", ComponentSource::url(SCENE_MANAGER_URL))
            .await?
            .add_route(CapabilityRoute {
                capability: Capability::protocol("fuchsia.ui.accessibility.view.Registry"),
                source: RouteEndpoint::component("scene_manager"),
                targets: vec![RouteEndpoint::above_root()],
            })?
            .add_route(CapabilityRoute {
                capability: Capability::protocol("fuchsia.session.scene.Manager"),
                source: RouteEndpoint::component("scene_manager"),
                targets: vec![RouteEndpoint::above_root()],
            })?
            .add_route(CapabilityRoute {
                capability: Capability::protocol("fuchsia.ui.app.ViewProvider"),
                source: RouteEndpoint::AboveRoot,
                targets: vec![RouteEndpoint::component("scene_manager")],
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

    async fn present_session_changes(session: &scenic::SessionPtr) {
        let _ = session
            .lock()
            // Passing 0 for requested_presentation_time tells scenic that that it should process
            // enqueued operation as soon as possible.
            // Passing 0 for requested_prediction_span guarantees that scenic will provide at least
            // one future time.
            .present2(0, 0)
            .await
            .expect("Failed to present");
    }

    async fn view_is_focused(
        view_ref_focused_proxy: &ui_views::ViewRefFocusedProxy,
    ) -> Result<bool, anyhow::Error> {
        view_ref_focused_proxy
            .watch()
            .await
            .map_err(|err| format_err!("ViewRefFocused fidl error for client view: {:?}", err))
            .map(|focused_state| focused_state.focused.unwrap_or(false))
    }

    async fn insert_a11y_view(
        session: scenic::SessionPtr,
        accessibility_view_registry_proxy: &RegistryProxy,
    ) -> Result<(scenic::View, scenic::ViewHolder, ui_views::ViewRef), anyhow::Error> {
        // Create a11y ViewRef and ViewToken pairs.
        let mut a11y_view_token_pair = scenic::ViewTokenPair::new()?;
        let a11y_viewref_pair = scenic::ViewRefPair::new()?;
        let mut viewref_dup_for_registry =
            fuchsia_scenic::duplicate_view_ref(&a11y_viewref_pair.view_ref)?;
        let viewref_dup_to_return =
            fuchsia_scenic::duplicate_view_ref(&a11y_viewref_pair.view_ref)?;
        // Create a11y view and try inserting into scene.
        let a11y_view = scenic::View::new3(
            session.clone(),
            a11y_view_token_pair.view_token,
            a11y_viewref_pair.control_ref,
            a11y_viewref_pair.view_ref,
            Some(String::from("a11y view")),
        );

        present_session_changes(&session).await;

        let proxy_view_holder_token = accessibility_view_registry_proxy
            .create_accessibility_view_holder(
                &mut viewref_dup_for_registry,
                &mut a11y_view_token_pair.view_holder_token,
            )
            .await
            .unwrap();

        // Attach proxy view holder to the scene. This step is necessary to ensure that the client
        // view is attached to the scene.
        let proxy_view_holder = scenic::ViewHolder::new(
            session.clone(),
            proxy_view_holder_token,
            Some(String::from("proxy view holder")),
        );
        a11y_view.add_child(&proxy_view_holder);
        present_session_changes(&session).await;

        Ok((a11y_view, proxy_view_holder, viewref_dup_to_return))
    }

    async fn set_root_view(
        client_session: scenic::SessionPtr,
        scene_manager: SceneManagerProxy,
    ) -> Result<(scenic::View, ui_views::ViewRef), anyhow::Error> {
        // Create ViewProvider endpoints. The scene manager uses this service to create the root
        // view.
        let (view_provider_client, view_provider_server) = create_endpoints::<ViewProviderMarker>()
            .expect("Failed to create local ViewProvider proxy and stream");

        let client_view_ref = scene_manager.set_root_view(view_provider_client).await.unwrap();

        // Create a mock ViewProvider.
        let mut view_provider_stream = view_provider_server.into_stream()?;
        let client_view = match view_provider_stream.try_next().await.unwrap() {
            Some(ViewProviderRequest::CreateViewWithViewRef {
                token,
                view_ref_control,
                view_ref,
                ..
            }) => {
                let view_token = ViewToken { value: token };
                Ok(scenic::View::new3(
                    client_session.clone(),
                    view_token,
                    view_ref_control,
                    view_ref,
                    Some(String::from("root view")),
                ))
            }

            _ => Err("Unexpected method call"),
        }
        .expect("CreateViewWithViewRef was not called");

        present_session_changes(&client_session).await;

        Ok((client_view, client_view_ref))
    }

    #[allow(unused_variables)]
    #[fasync::run_singlethreaded(test)]
    async fn test_attach_a11y_view() -> Result<(), anyhow::Error> {
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

        // Create a11y view. Proxy view holder is not required for this test case, and can be dropped.
        let (a11y_view, _, mut a11y_view_ref) =
            insert_a11y_view(session, &accessibility_view_registry_proxy).await.unwrap();

        // Listen for a11y view ref installed event.
        let watch_result = view_ref_installed_proxy
            .watch(&mut a11y_view_ref)
            .await
            .expect("ViewRefInstalled fidl error for a11y view")
            .expect("ViewRefInstalledError for a11y view");

        Ok(())
    }

    #[allow(unused_variables)]
    #[fasync::run_singlethreaded(test)]
    async fn test_set_root_view_before_a11y_view() -> Result<(), anyhow::Error> {
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

        let scene_manager =
            realm_instance.root.connect_to_protocol_at_exposed_dir::<SceneManagerMarker>().unwrap();

        // Instantiate scenic session in which to create a11y view.
        let (session_proxy, session_request_stream) = fidl::endpoints::create_proxy()?;
        scenic_proxy.create_session2(session_request_stream, None, None)?;
        let session = scenic::Session::new(session_proxy);

        // Insert a11y view. The a11y view ref is not required for this test case, and can be
        // dropped.
        let (a11y_view, proxy_view_holder, _) =
            insert_a11y_view(session, &accessibility_view_registry_proxy).await.unwrap();

        // Create session in which to create root view.
        let (client_session_proxy, client_session_request_stream) =
            fidl::endpoints::create_proxy()?;
        scenic_proxy.create_session2(client_session_request_stream, None, None)?;
        let client_session = scenic::Session::new(client_session_proxy);

        // Set root view.
        let (client_view, mut client_view_ref) =
            set_root_view(client_session, scene_manager).await.unwrap();

        // Verify that the client view was attached to the scene.
        let watch_result = view_ref_installed_proxy
            .watch(&mut client_view_ref)
            .await
            .expect("ViewRefInstalled fidl error for client view")
            .expect("ViewRefInstalledError for client view");

        Ok(())
    }

    #[allow(unused_variables)]
    #[fasync::run_singlethreaded(test)]
    async fn test_set_root_view_after_a11y_view() -> Result<(), anyhow::Error> {
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

        let scene_manager =
            realm_instance.root.connect_to_protocol_at_exposed_dir::<SceneManagerMarker>().unwrap();

        // Create session in which to create root view.
        // The |view_ref_focused| endpoint is required to verify that the client view is refocused
        // after the a11y view is inserted.
        let (client_session_proxy, client_session_request_stream) =
            fidl::endpoints::create_proxy()?;
        let (view_ref_focused_proxy, view_ref_focused_request_stream) =
            fidl::endpoints::create_proxy()?;
        let mut session_endpoints = ui_scenic::SessionEndpoints::EMPTY;
        session_endpoints.session = Some(client_session_request_stream);
        session_endpoints.view_ref_focused = Some(view_ref_focused_request_stream);
        scenic_proxy.create_session_t(session_endpoints).await?;
        let client_session = scenic::Session::new(client_session_proxy);

        // Create ViewProvider endpoints. The scene manager uses this service to create the root
        // view.
        let (view_provider_client, view_provider_server) = create_endpoints::<ViewProviderMarker>()
            .expect("Failed to create local ViewProvider proxy and stream");

        // Set root view. Client view ref is not required for this test case, and can be dropped.
        let (client_view, _) = set_root_view(client_session, scene_manager).await.unwrap();

        // Instantiate scenic session in which to create a11y view.
        let (session_proxy, session_request_stream) = fidl::endpoints::create_proxy()?;
        scenic_proxy.create_session2(session_request_stream, None, None)?;
        let session = scenic::Session::new(session_proxy);

        // Insert a11y view. The a11y view ref is not required for this test case, and can be
        // dropped.
        let (a11y_view, proxy_view_holder, _) =
            insert_a11y_view(session, &accessibility_view_registry_proxy).await.unwrap();

        // Verify that the client view was focused.
        let client_view_focused = view_is_focused(&view_ref_focused_proxy).await.unwrap();
        if !client_view_focused {
            panic!("Client view ref not focused after a11y view inserted");
        }

        Ok(())
    }
}
