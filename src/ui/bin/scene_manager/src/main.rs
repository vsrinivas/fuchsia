// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::color_transform_manager::ColorTransformManager,
    ::input_pipeline::activity::ActivityManager,
    anyhow::{Context, Error},
    fidl::prelude::*,
    fidl_fuchsia_accessibility::{
        ColorTransformHandlerMarker, ColorTransformMarker, MagnificationHandlerMarker,
        MagnifierMarker,
    },
    fidl_fuchsia_accessibility_scene as a11y_view,
    fidl_fuchsia_input_injection::InputDeviceRegistryRequestStream,
    fidl_fuchsia_input_interaction::NotifierRequestStream,
    fidl_fuchsia_input_interaction_observation::AggregatorRequestStream,
    fidl_fuchsia_recovery_policy::DeviceRequestStream as FactoryResetDeviceRequestStream,
    fidl_fuchsia_recovery_ui::FactoryResetCountdownRequestStream,
    fidl_fuchsia_session_scene::{
        ManagerRequest as SceneManagerRequest, ManagerRequestStream as SceneManagerRequestStream,
        PresentRootViewError,
    },
    fidl_fuchsia_ui_accessibility_view::{
        RegistryRequest as A11yViewRegistryRequest,
        RegistryRequestStream as A11yViewRegistryRequestStream,
    },
    fidl_fuchsia_ui_app as ui_app,
    fidl_fuchsia_ui_brightness::ColorAdjustmentHandlerRequestStream,
    fidl_fuchsia_ui_composition as flatland, fidl_fuchsia_ui_display_color as color,
    fidl_fuchsia_ui_display_singleton as singleton_display,
    fidl_fuchsia_ui_focus::FocusChainProviderRequestStream,
    fidl_fuchsia_ui_input_config::FeaturesRequestStream as InputConfigFeaturesRequestStream,
    fidl_fuchsia_ui_policy::{
        DeviceListenerRegistryRequestStream as MediaButtonsListenerRegistryRequestStream,
        DisplayBacklightRequestStream,
    },
    fidl_fuchsia_ui_scenic::ScenicMarker,
    fidl_fuchsia_ui_views as ui_views, fuchsia_async as fasync,
    fuchsia_component::{client::connect_to_protocol, server::ServiceFs},
    fuchsia_inspect as inspect, fuchsia_zircon as zx,
    futures::lock::Mutex,
    futures::{StreamExt, TryStreamExt},
    input_config_lib::Config,
    scene_management::{self, SceneManager, ViewingDistance, ViewportToken},
    std::rc::Rc,
    std::sync::Arc,
    tracing::{error, info, warn},
};

mod color_transform_manager;
mod factory_reset_countdown_server;
mod factory_reset_device_server;
mod input_config_server;
mod input_device_registry_server;
mod input_pipeline;
mod media_buttons_listener_registry_server;

enum ExposedServices {
    AccessibilityViewRegistry(A11yViewRegistryRequestStream),
    ColorAdjustmentHandler(ColorAdjustmentHandlerRequestStream),
    MediaButtonsListenerRegistry(MediaButtonsListenerRegistryRequestStream),
    DisplayBacklight(DisplayBacklightRequestStream),
    FactoryResetCountdown(FactoryResetCountdownRequestStream),
    FactoryReset(FactoryResetDeviceRequestStream),
    FocusChainProvider(FocusChainProviderRequestStream),
    InputConfigFeatures(InputConfigFeaturesRequestStream),
    InputDeviceRegistry(InputDeviceRegistryRequestStream),
    SceneManager(SceneManagerRequestStream),
    UserInteractionObservation(AggregatorRequestStream),
    UserInteraction(NotifierRequestStream),
}

#[fuchsia::main(logging_tags = [ "scene_manager" ])]
async fn main() -> Result<(), Error> {
    let result = inner_main().await;
    if let Err(e) = result {
        error!("Uncaught error in main(): {}", e);
        return Err(e);
    }
    Ok(())
}

// TODO(fxbug.dev/89425): Ideally we wouldn't need to have separate inner_main() and main()
// functions in order to catch and log top-level errors.  Instead, the #[fuchsia::main] macro
// could catch and log the error.
async fn inner_main() -> Result<(), Error> {
    let mut fs = ServiceFs::new_local();

    // Create an inspector that's large enough to store 10 seconds of touchpad
    // events.
    // * Empirically, when all events have two fingers, the total inspect data
    //   size is about 260 KB.
    // * Use a slightly larger value here to allow some headroom. E.g. perhaps
    //   some events have a third finger.
    let inspector = inspect::component::init_inspector_with_size(300 * 1024);
    inspect_runtime::serve(inspector, &mut fs)?;

    // Report data on the size of the inspect VMO, and the number of allocation
    // failures encountered. (Allocation failures can lead to missing data.)
    inspect::component::serve_inspect_stats();

    // Initialize tracing.
    //
    // This is done once by the process, rather than making the libraries
    // linked into the component (e.g. input pipeline) initialize tracing.
    //
    // Initializing at the process-level more closely models how a trace
    // provider (e.g. scene_manager) interacts with the trace manager.
    fuchsia_trace_provider::trace_provider_create_with_fdio();

    fs.dir("svc")
        .add_fidl_service(ExposedServices::AccessibilityViewRegistry)
        .add_fidl_service(ExposedServices::ColorAdjustmentHandler)
        .add_fidl_service(ExposedServices::MediaButtonsListenerRegistry)
        .add_fidl_service(ExposedServices::DisplayBacklight)
        .add_fidl_service(ExposedServices::FactoryResetCountdown)
        .add_fidl_service(ExposedServices::FactoryReset)
        .add_fidl_service(ExposedServices::FocusChainProvider)
        .add_fidl_service(ExposedServices::InputConfigFeatures)
        .add_fidl_service(ExposedServices::InputDeviceRegistry)
        .add_fidl_service(ExposedServices::SceneManager)
        .add_fidl_service(ExposedServices::UserInteractionObservation)
        .add_fidl_service(ExposedServices::UserInteraction);
    fs.take_and_serve_directory_handle()?;

    let (input_device_registry_server, input_device_registry_request_stream_receiver) =
        input_device_registry_server::make_server_and_receiver();

    let (input_config_server, input_config_receiver) =
        input_config_server::make_server_and_receiver();

    let (
        media_buttons_listener_registry_server,
        media_buttons_listener_registry_request_stream_receiver,
    ) = media_buttons_listener_registry_server::make_server_and_receiver();

    let (factory_reset_countdown_server, factory_reset_countdown_request_stream_receiver) =
        factory_reset_countdown_server::make_server_and_receiver();

    let (factory_reset_device_server, factory_reset_device_request_stream_receiver) =
        factory_reset_device_server::make_server_and_receiver();

    // This call should normally never fail. The ICU data loader must be kept alive to ensure
    // Unicode data is kept in memory.
    let icu_data_loader = icu_data::Loader::new().unwrap();

    let scenic = connect_to_protocol::<ScenicMarker>()?;
    let use_flatland = scenic.uses_flatland().await.expect("Failed to get flatland info.");
    let display_ownership =
        scenic.get_display_ownership_event().await.expect("Failed to get display ownership.");
    info!(use_flatland, "Instantiating SceneManager");

    // Read config files to discover display attributes.
    let display_rotation = match std::fs::read_to_string("/config/data/display_rotation") {
        Ok(contents) => {
            let contents = contents.trim();
            contents.parse::<i32>().context(format!(
                "Failed to parse /config/data/display_rotation - \
            expected an integer, got {contents}"
            ))?
        }
        Err(e) => {
            warn!(
                "Wasn't able to read config/data/display_rotation, \
                defaulting to a display rotation of 0 degrees: {}",
                e
            );
            0
        }
    };
    let display_pixel_density = match std::fs::read_to_string("/config/data/display_pixel_density")
    {
        Ok(contents) => {
            let contents = contents.trim();
            Some(contents.parse::<f32>().context(format!(
                "Failed to parse /config/data/display_pixel_density - \
                expected a decimal, got {contents}"
            ))?)
        }
        Err(e) => {
            warn!(
                "Wasn't able to read config/data/display_pixel_density, \
                    guessing based on display size: {}",
                e
            );
            Some(9.0) // Unknown display: match Root Presenter's 9.0f default pixel density
        }
    };
    let viewing_distance = match std::fs::read_to_string("/config/data/display_usage") {
        Ok(s) => Some(match s.trim() {
            "handheld" => ViewingDistance::Handheld,
            "close" => ViewingDistance::Close,
            "near" => ViewingDistance::Near,
            "midrange" => ViewingDistance::Midrange,
            "far" => ViewingDistance::Far,
            unknown => anyhow::bail!("Invalid /config/data/display_usage value: {unknown}"),
        }),
        Err(e) => {
            warn!(
                "Wasn't able to read config/data/display_usage, \
                guessing based on display size: {}",
                e
            );
            None
        }
    };

    let scene_manager: Arc<Mutex<dyn SceneManager>> = if use_flatland {
        // TODO(fxbug.dev/86379): Support for insertion of accessibility view.  Pass ViewRefInstalled
        // to the SceneManager, the same way we do for the Gfx branch.
        let flatland_display = connect_to_protocol::<flatland::FlatlandDisplayMarker>()?;
        let singleton_display_info = connect_to_protocol::<singleton_display::InfoMarker>()?;
        let root_flatland = connect_to_protocol::<flatland::FlatlandMarker>()?;
        let pointerinjector_flatland = connect_to_protocol::<flatland::FlatlandMarker>()?;
        let scene_flatland = connect_to_protocol::<flatland::FlatlandMarker>()?;
        let cursor_view_provider = connect_to_protocol::<ui_app::ViewProviderMarker>()?;
        let a11y_view_provider = connect_to_protocol::<a11y_view::ProviderMarker>()?;
        Arc::new(Mutex::new(
            scene_management::FlatlandSceneManager::new(
                flatland_display,
                singleton_display_info,
                root_flatland,
                pointerinjector_flatland,
                scene_flatland,
                cursor_view_provider,
                a11y_view_provider,
                display_rotation,
                display_pixel_density,
                viewing_distance,
            )
            .await?,
        ))
    } else {
        let view_ref_installed = connect_to_protocol::<ui_views::ViewRefInstalledMarker>()?;
        let gfx_scene_manager: Arc<Mutex<dyn SceneManager>> = Arc::new(Mutex::new(
            scene_management::GfxSceneManager::new(
                scenic,
                view_ref_installed,
                display_rotation,
                display_pixel_density,
                viewing_distance,
            )
            .await?,
        ));
        if let Err(e) = register_gfx_as_magnifier(Arc::clone(&gfx_scene_manager)) {
            warn!("failed to register as the magnification handler: {:?}", e);
        }
        gfx_scene_manager
    };
    let (focus_chain_publisher, focus_chain_stream_handler) =
        focus_chain_provider::make_publisher_and_stream_handler();

    // Create a node under root to hang all input pipeline inspect data off of.
    let inspect_node = Rc::new(inspector.root().create_child("input_pipeline"));

    // Start input pipeline.
    let Config { idle_threshold_minutes, supported_input_devices } =
        Config::take_from_startup_handle();
    if let Ok(input_pipeline) = input_pipeline::handle_input(
        use_flatland,
        scene_manager.clone(),
        input_config_receiver,
        input_device_registry_request_stream_receiver,
        media_buttons_listener_registry_request_stream_receiver,
        factory_reset_countdown_request_stream_receiver,
        factory_reset_device_request_stream_receiver,
        icu_data_loader,
        &inspect_node.clone(),
        display_ownership,
        focus_chain_publisher,
        supported_input_devices,
    )
    .await
    {
        fasync::Task::local(input_pipeline.handle_input_events()).detach();
    }

    // Create Activity Manager.
    let activity_manager =
        ActivityManager::new(zx::Duration::from_minutes(idle_threshold_minutes as i64));

    // Create and register a ColorTransformManager.
    let color_converter = connect_to_protocol::<color::ConverterMarker>()?;
    let color_transform_manager = ColorTransformManager::new(color_converter);

    let (color_transform_handler_client, color_transform_handler_server) =
        fidl::endpoints::create_request_stream::<ColorTransformHandlerMarker>()?;
    match connect_to_protocol::<ColorTransformMarker>() {
        Err(e) => {
            error!("Failed to connect to fuchsia.accessibility.color_transform: {:?}", e);
        }
        Ok(proxy) => match proxy.register_color_transform_handler(color_transform_handler_client) {
            Err(e) => {
                error!("Failed to call RegisterColorTransformHandler: {:?}", e);
            }
            Ok(()) => {
                ColorTransformManager::handle_color_transform_request_stream(
                    Arc::clone(&color_transform_manager),
                    color_transform_handler_server,
                );
            }
        },
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
            ExposedServices::ColorAdjustmentHandler(request_stream) => {
                ColorTransformManager::handle_color_adjustment_request_stream(
                    Arc::clone(&color_transform_manager),
                    request_stream,
                );
            }
            ExposedServices::DisplayBacklight(request_stream) => {
                ColorTransformManager::handle_display_backlight_request_stream(
                    Arc::clone(&color_transform_manager),
                    request_stream,
                );
            }
            ExposedServices::FocusChainProvider(request_stream) => {
                focus_chain_stream_handler.handle_request_stream(request_stream).detach();
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
                        warn!(
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
                        warn!("failed to forward InputConfigFeaturesRequestStream: {:?}", e)
                    }
                }
            }
            ExposedServices::MediaButtonsListenerRegistry(request_stream) => {
                match &media_buttons_listener_registry_server.handle_request(request_stream).await {
                    Ok(()) => (),
                    Err(e) => {
                        warn!(
                            "failed to forward media buttons listener request via DeviceListenerRegistryRequestStream: {:?}",
                            e
                        )
                    }
                }
            }
            ExposedServices::FactoryResetCountdown(request_stream) => {
                match &factory_reset_countdown_server.handle_request(request_stream).await {
                    Ok(()) => (),
                    Err(e) => {
                        warn!("failed to forward FactoryResetCountdown: {:?}", e)
                    }
                }
            }
            ExposedServices::FactoryReset(request_stream) => {
                match &factory_reset_device_server.handle_request(request_stream).await {
                    Ok(()) => (),
                    Err(e) => {
                        warn!("failed to forward fuchsia.recovery.policy.Device: {:?}", e)
                    }
                }
            }
            ExposedServices::UserInteractionObservation(stream) => {
                let activity_manager = activity_manager.clone();
                fasync::Task::local(async move {
                match activity_manager
                    .handle_interaction_aggregator_request_stream(stream)
                    .await
                {
                    Ok(()) => (),
                    Err(e) => {
                        warn!(
                      "failure while serving fuchsia.input.interaction.observation.Aggregator: {:?}",
                      e
                  );
                    }}
                }).detach();
            }
            ExposedServices::UserInteraction(stream) => {
                let activity_manager = activity_manager.clone();
                fasync::Task::local(async move {
                    match activity_manager.handle_interaction_notifier_request_stream(stream).await
                    {
                        Ok(()) => (),
                        Err(e) => {
                            warn!(
                                "failure while serving fuchsia.input.interaction.Notifier: {:?}",
                                e
                            );
                        }
                    }
                })
                .detach();
            }
        }
    }

    info!("Finished service handler loop; exiting main.");
    Ok(())
}

pub async fn handle_accessibility_view_registry_request_stream(
    mut request_stream: A11yViewRegistryRequestStream,
    scene_manager: Arc<Mutex<dyn scene_management::SceneManager>>,
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
                let r = scene_manager.insert_a11y_view(a11y_view_token).await;

                let _ = match r {
                    Ok(mut result) => {
                        let _ = responder.send(&mut result);
                    }
                    Err(e) => {
                        warn!("Closing A11yViewRegistry connection due to error {:?}", e);
                        responder.control_handle().shutdown_with_epitaph(zx::Status::PEER_CLOSED);
                    }
                };
            }
            A11yViewRegistryRequest::CreateAccessibilityViewport {
                viewport_creation_token: _,
                responder,
                ..
            } => {
                error!("A11yViewRegistry.CreateAccessibilityViewport not implemented!");
                responder.control_handle().shutdown_with_epitaph(zx::Status::PEER_CLOSED);
            }
        };
    }
}

pub async fn handle_scene_manager_request_stream(
    mut request_stream: SceneManagerRequestStream,
    scene_manager: Arc<Mutex<dyn scene_management::SceneManager>>,
) {
    while let Ok(Some(request)) = request_stream.try_next().await {
        match request {
            SceneManagerRequest::SetRootView { view_provider, responder } => {
                if let Ok(proxy) = view_provider.into_proxy() {
                    let mut scene_manager = scene_manager.lock().await;
                    let mut set_root_view_result =
                        scene_manager.set_root_view_deprecated(proxy).await.map_err(|e| {
                            error!("Failed to obtain ViewRef from SetRootView(): {}", e);
                            PresentRootViewError::InternalError
                        });
                    if let Err(e) = responder.send(&mut set_root_view_result) {
                        error!("Error responding to SetRootView(): {}", e);
                    }
                }
            }
            SceneManagerRequest::PresentRootViewLegacy {
                view_holder_token,
                view_ref,
                responder,
            } => {
                let mut scene_manager = scene_manager.lock().await;
                let mut set_root_view_result = scene_manager
                    .set_root_view(ViewportToken::Gfx(view_holder_token), Some(view_ref))
                    .await
                    .map_err(|e| {
                        error!("Failed to obtain ViewRef from PresentRootViewLegacy(): {}", e);
                        PresentRootViewError::InternalError
                    });
                if let Err(e) = responder.send(&mut set_root_view_result) {
                    error!("Error responding to PresentRootViewLegacy(): {}", e);
                }
            }
            SceneManagerRequest::PresentRootView { viewport_creation_token, responder } => {
                let mut scene_manager = scene_manager.lock().await;
                let mut set_root_view_result = scene_manager
                    .set_root_view(ViewportToken::Flatland(viewport_creation_token), None)
                    .await
                    .map_err(|e| {
                        error!("Failed to obtain ViewRef from PresentRootView(): {}", e);
                        PresentRootViewError::InternalError
                    });
                if let Err(e) = responder.send(&mut set_root_view_result) {
                    error!("Error responding to PresentRootView(): {}", e);
                }
            }
        };
    }
}

fn register_gfx_as_magnifier(
    scene_manager: Arc<Mutex<dyn SceneManager>>,
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
