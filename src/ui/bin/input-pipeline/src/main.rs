// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fidl_fuchsia_input_injection::InputDeviceRegistryRequestStream,
    fidl_fuchsia_input_interaction::NotifierRequestStream,
    fidl_fuchsia_input_interaction_observation::AggregatorRequestStream,
    fidl_fuchsia_recovery_policy::DeviceRequestStream,
    fidl_fuchsia_recovery_ui::FactoryResetCountdownRequestStream,
    fidl_fuchsia_ui_policy::DeviceListenerRegistryRequestStream as MediaButtonsListenerRegistryRequestStream,
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_syslog::fx_log_warn,
    fuchsia_zircon::Duration,
    futures::StreamExt,
    input_config_lib::Config,
    input_pipeline as input_pipeline_lib,
    input_pipeline::input_device,
    input_pipeline::{
        activity::ActivityManager, factory_reset_handler::FactoryResetHandler,
        media_buttons_handler::MediaButtonsHandler,
    },
    std::rc::Rc,
};

const IGNORE_REAL_DEVICES_CONFIG_PATH: &'static str = "/config/data/ignore_real_devices";

enum ExposedServices {
    FactoryResetCountdown(FactoryResetCountdownRequestStream),
    InputDeviceRegistry(InputDeviceRegistryRequestStream),
    MediaButtonsListenerRegistry(MediaButtonsListenerRegistryRequestStream),
    RecoveryPolicy(DeviceRequestStream),
    UserInteractionObservation(AggregatorRequestStream),
    UserInteraction(NotifierRequestStream),
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["input-pipeline"]).expect("Failed to init syslog");
    fuchsia_trace_provider::trace_provider_create_with_fdio();

    let mut fs = ServiceFs::new_local();
    let inspector = fuchsia_inspect::component::inspector();
    inspect_runtime::serve(inspector, &mut fs)?;

    // Expose InputDeviceRegistry to allow input injection for testing.
    fs.dir("svc").add_fidl_service(ExposedServices::FactoryResetCountdown);
    fs.dir("svc").add_fidl_service(ExposedServices::InputDeviceRegistry);
    fs.dir("svc").add_fidl_service(ExposedServices::MediaButtonsListenerRegistry);
    fs.dir("svc").add_fidl_service(ExposedServices::RecoveryPolicy);
    fs.dir("svc").add_fidl_service(ExposedServices::UserInteractionObservation);
    fs.dir("svc").add_fidl_service(ExposedServices::UserInteraction);
    fs.take_and_serve_directory_handle()?;

    // Declare the types of input to support.
    let device_types =
        vec![input_device::InputDeviceType::Touch, input_device::InputDeviceType::ConsumerControls];

    // Create a new input pipeline.
    let factory_reset_handler = FactoryResetHandler::new();
    let media_buttons_handler = MediaButtonsHandler::new();
    let input_handlers: Vec<Rc<dyn input_pipeline_lib::input_handler::InputHandler>> = vec![
        input_pipeline_lib::inspect_handler::InspectHandler::new(
            inspector.root().create_child("input_pipeline_entry"),
        ),
        factory_reset_handler.clone(),
        media_buttons_handler.clone(),
        make_touch_injector_handler().await,
        input_pipeline_lib::inspect_handler::InspectHandler::new(
            inspector.root().create_child("input_pipeline_exit"),
        ),
    ];

    let input_pipeline = if std::path::Path::new(IGNORE_REAL_DEVICES_CONFIG_PATH).exists() {
        input_pipeline_lib::input_pipeline::InputPipeline::new_for_test(
            device_types.clone(),
            input_pipeline_lib::input_pipeline::InputPipelineAssembly::new()
                .add_all_handlers(input_handlers),
        )
    } else {
        input_pipeline_lib::input_pipeline::InputPipeline::new(
            device_types.clone(),
            input_pipeline_lib::input_pipeline::InputPipelineAssembly::new()
                .add_all_handlers(input_handlers)
                .add_autorepeater(),
        )
        .expect("Failed to create input pipeline")
    };

    let input_event_sender = input_pipeline.input_event_sender().clone();
    let bindings = input_pipeline.input_device_bindings().clone();

    // Handle input events.
    fasync::Task::local(input_pipeline.handle_input_events()).detach();

    // Create Activity Manager.
    let Config { idle_threshold_minutes } = Config::take_from_startup_handle();
    let activity_manager =
        ActivityManager::new(Duration::from_minutes(idle_threshold_minutes as i64));

    // Handle incoming injection input devices. Start with u32::Max to avoid conflicting device ids.
    let mut injected_device_id = u32::MAX;
    while let Some(service_request) = fs.next().await {
        match service_request {
            ExposedServices::FactoryResetCountdown(stream) => {
                let factory_reset_countdown_fut = handle_factory_reset_countdown_request_stream(
                    factory_reset_handler.clone(),
                    stream,
                );

                fasync::Task::local(factory_reset_countdown_fut).detach();
            }
            ExposedServices::InputDeviceRegistry(stream) => {
                let input_device_registry_fut = handle_input_device_registry_request_stream(
                    stream,
                    device_types.clone(),
                    input_event_sender.clone(),
                    bindings.clone(),
                    injected_device_id,
                );

                fasync::Task::local(input_device_registry_fut).detach();
                injected_device_id -= 1;
            }
            ExposedServices::MediaButtonsListenerRegistry(stream) => {
                let device_listener_registry_fut =
                    handle_media_buttons_listener_registry(media_buttons_handler.clone(), stream);

                fasync::Task::local(device_listener_registry_fut).detach();
            }
            ExposedServices::RecoveryPolicy(stream) => {
                let recovery_policy_fut = handle_recovery_policy_device_request_stream(
                    factory_reset_handler.clone(),
                    stream,
                );

                fasync::Task::local(recovery_policy_fut).detach();
            }
            ExposedServices::UserInteractionObservation(stream) => {
                let interaction_aggregator_fut =
                    handle_interaction_aggregator_request_stream(activity_manager.clone(), stream);

                fasync::Task::local(interaction_aggregator_fut).detach();
            }
            ExposedServices::UserInteraction(stream) => {
                let interaction_notifier_fut =
                    handle_interaction_notifier_request_stream(activity_manager.clone(), stream);

                fasync::Task::local(interaction_notifier_fut).detach();
            }
        }
    }

    Ok(())
}

async fn handle_factory_reset_countdown_request_stream(
    factory_reset_handler: Rc<FactoryResetHandler>,
    stream: FactoryResetCountdownRequestStream,
) {
    if let Err(error) =
        factory_reset_handler.handle_factory_reset_countdown_request_stream(stream).await
    {
        fx_log_warn!("failure while serving FactoryResetCountdown: {}", error);
    }
}

async fn handle_input_device_registry_request_stream(
    stream: InputDeviceRegistryRequestStream,
    device_types: Vec<input_pipeline_lib::input_device::InputDeviceType>,
    input_event_sender: futures::channel::mpsc::Sender<
        input_pipeline_lib::input_device::InputEvent,
    >,
    bindings: input_pipeline_lib::input_pipeline::InputDeviceBindingHashMap,
    device_id: u32,
) {
    match input_pipeline_lib::input_pipeline::InputPipeline::handle_input_device_registry_request_stream(
        stream,
        &device_types,
        &input_event_sender,
        &bindings,
        device_id,
    )
    .await
    {
        Ok(()) => (),
        Err(e) => {
            fx_log_warn!(
                "failure while serving InputDeviceRegistry: {}; \
                     will continue serving other clients",
                e
            );
        }
    }
}

async fn handle_media_buttons_listener_registry(
    media_buttons_handler: Rc<MediaButtonsHandler>,
    stream: MediaButtonsListenerRegistryRequestStream,
) {
    match media_buttons_handler.handle_device_listener_registry_request_stream(stream).await {
        Ok(()) => (),
        Err(e) => {
            fx_log_warn!("failure while serving DeviceListenerRegistry: {}", e);
        }
    }
}

async fn handle_recovery_policy_device_request_stream(
    factory_reset_handler: Rc<FactoryResetHandler>,
    stream: DeviceRequestStream,
) {
    match factory_reset_handler.handle_recovery_policy_device_request_stream(stream).await {
        Ok(()) => (),
        Err(e) => {
            fx_log_warn!("failure while serving fuchsia.recovery.policy.Device: {}", e);
        }
    }
}

async fn handle_interaction_aggregator_request_stream(
    activity_manager: Rc<ActivityManager>,
    stream: AggregatorRequestStream,
) {
    if let Err(error) = activity_manager.handle_interaction_aggregator_request_stream(stream).await
    {
        fx_log_warn!(
            "failure while serving fuchsia.input.interaction.observation.Aggregator: {}; \
   will continue serving other clients",
            error
        );
    }
}

async fn handle_interaction_notifier_request_stream(
    activity_manager: Rc<ActivityManager>,
    stream: NotifierRequestStream,
) {
    if let Err(error) = activity_manager.handle_interaction_notifier_request_stream(stream).await {
        fx_log_warn!(
            "failure while serving fuchsia.input.interaction.Notifier: {}; \
 will continue serving other clients",
            error
        );
    }
}

async fn make_touch_injector_handler(
) -> Rc<input_pipeline_lib::touch_injector_handler::TouchInjectorHandler> {
    let scenic =
        fuchsia_component::client::connect_to_protocol::<fidl_fuchsia_ui_scenic::ScenicMarker>()
            .expect("Failed to connect to Scenic.");
    let display_info = scenic.get_display_info().await.expect("Failed to get display info.");
    let display_size = input_pipeline_lib::Size {
        width: display_info.width_in_px as f32,
        height: display_info.height_in_px as f32,
    };

    let touch_handler =
        input_pipeline_lib::touch_injector_handler::TouchInjectorHandler::new(display_size)
            .await
            .expect("Failed to create TouchInjectorHandler.");
    fuchsia_async::Task::local(touch_handler.clone().watch_viewport()).detach();
    touch_handler
}
