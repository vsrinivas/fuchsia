// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Error},
    fidl_fuchsia_factory::MiscFactoryStoreProviderMarker,
    fidl_fuchsia_input_injection::InputDeviceRegistryRequestStream,
    fidl_fuchsia_input_interaction::NotifierRequestStream,
    fidl_fuchsia_input_interaction_observation::AggregatorRequestStream,
    fidl_fuchsia_lightsensor::SensorRequestStream,
    fidl_fuchsia_recovery_policy::DeviceRequestStream,
    fidl_fuchsia_recovery_ui::FactoryResetCountdownRequestStream,
    fidl_fuchsia_settings::LightMarker,
    fidl_fuchsia_ui_brightness::ControlMarker as BrightnessControlMarker,
    fidl_fuchsia_ui_policy::DeviceListenerRegistryRequestStream as MediaButtonsListenerRegistryRequestStream,
    fuchsia_async as fasync,
    fuchsia_component::client::connect_to_protocol,
    fuchsia_component::server::ServiceFs,
    fuchsia_syslog::fx_log_warn,
    fuchsia_zircon::Duration,
    futures::StreamExt,
    input_config_lib::Config,
    input_pipeline as input_pipeline_lib,
    input_pipeline::input_device,
    input_pipeline::{
        activity::ActivityManager,
        factory_reset_handler::FactoryResetHandler,
        light_sensor::{Calibration, Configuration, FactoryFileLoader},
        light_sensor_handler::{
            make_light_sensor_handler_and_spawn_led_watcher, CalibratedLightSensorHandler,
        },
        media_buttons_handler::MediaButtonsHandler,
    },
    std::{fs::File, io::Read, rc::Rc},
};

const IGNORE_REAL_DEVICES_CONFIG_PATH: &'static str = "/config/data/ignore_real_devices";
const LIGHT_SENSOR_CONFIGURATION: &'static str = "/sensor-config/config.json";

enum ExposedServices {
    FactoryResetCountdown(FactoryResetCountdownRequestStream),
    InputDeviceRegistry(InputDeviceRegistryRequestStream),
    LightSensor(SensorRequestStream),
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
    fs.dir("svc").add_fidl_service(ExposedServices::LightSensor);
    fs.dir("svc").add_fidl_service(ExposedServices::RecoveryPolicy);
    fs.dir("svc").add_fidl_service(ExposedServices::UserInteractionObservation);
    fs.dir("svc").add_fidl_service(ExposedServices::UserInteraction);
    fs.take_and_serve_directory_handle()?;

    // Declare the types of input to support.
    let mut device_types =
        vec![input_device::InputDeviceType::Touch, input_device::InputDeviceType::ConsumerControls];

    let configuration: Option<Configuration> = match File::open(LIGHT_SENSOR_CONFIGURATION) {
        Ok(mut file) => {
            let mut contents = String::new();
            let _ = file.read_to_string(&mut contents).context("reading configuration")?;
            Some(serde_json::from_str(&contents).context("parsing configuration")?)
        }
        Err(e) if e.kind() == std::io::ErrorKind::NotFound => None,
        Err(e) => {
            return Result::<(), _>::Err(e).context("opening light sensor config");
        }
    };
    let (light_sensor_handler, led_watcher_task) = if let Some(configuration) = configuration {
        device_types.push(input_device::InputDeviceType::LightSensor);
        let light_proxy = connect_to_protocol::<LightMarker>().expect("should get light proxy");
        let brightness_proxy = connect_to_protocol::<BrightnessControlMarker>()
            .expect("should get brightness control proxy");
        let factory_store_proxy = connect_to_protocol::<MiscFactoryStoreProviderMarker>()
            .expect("should get factory proxy");
        let factory_file_loader = FactoryFileLoader::new(factory_store_proxy)
            .expect("should be able to load factory data");
        let calibration = Calibration::new(configuration.calibration, factory_file_loader).await?;
        let (handler, task) = make_light_sensor_handler_and_spawn_led_watcher(
            light_proxy,
            brightness_proxy,
            calibration,
            configuration.sensor,
        )
        .await
        .expect("should get light sensor handler");
        (Some(handler), Some(task))
    } else {
        (None, None)
    };

    // Create a new input pipeline.
    let factory_reset_handler = FactoryResetHandler::new();
    let media_buttons_handler = MediaButtonsHandler::new();
    let mut input_handlers: Vec<Rc<dyn input_pipeline_lib::input_handler::InputHandler>> = vec![
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
    if let Some(light_sensor_handler) = light_sensor_handler.as_ref() {
        input_handlers.push(light_sensor_handler.clone());
    }

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
            ExposedServices::LightSensor(stream) => {
                if let Some(light_sensor_handler) = light_sensor_handler.as_ref() {
                    let light_sensor_fut =
                        handle_light_sensor_request_stream(light_sensor_handler.clone(), stream);
                    fasync::Task::local(light_sensor_fut).detach();
                }
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

    if let Some(led_watcher_task) = led_watcher_task {
        led_watcher_task.cancel().await;
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

async fn handle_light_sensor_request_stream(
    light_sensor_handler: Rc<CalibratedLightSensorHandler>,
    stream: SensorRequestStream,
) {
    if let Err(e) = light_sensor_handler.handle_light_sensor_request_stream(stream).await {
        fx_log_warn!("failure while serving LightSensor: {e:?}");
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
