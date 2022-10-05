// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    ::input_pipeline::{text_settings_handler::TextSettingsHandler, CursorMessage},
    anyhow::{Context, Error},
    fidl_fuchsia_input_injection::InputDeviceRegistryRequestStream,
    fidl_fuchsia_recovery_policy::DeviceRequestStream,
    fidl_fuchsia_recovery_ui::FactoryResetCountdownRequestStream,
    fidl_fuchsia_settings as fsettings,
    fidl_fuchsia_ui_input_config::FeaturesRequestStream as InputConfigFeaturesRequestStream,
    fidl_fuchsia_ui_pointerinjector_configuration::SetupProxy,
    fidl_fuchsia_ui_policy::DeviceListenerRegistryRequestStream,
    fidl_fuchsia_ui_shortcut as ui_shortcut,
    focus_chain_provider::FocusChainProviderPublisher,
    fuchsia_async as fasync,
    fuchsia_component::client::connect_to_protocol,
    fuchsia_inspect as inspect, fuchsia_zircon as zx,
    futures::{lock::Mutex, StreamExt},
    icu_data,
    input_pipeline::{
        self, dead_keys_handler,
        factory_reset_handler::FactoryResetHandler,
        ime_handler::ImeHandler,
        immersive_mode_shortcut_handler::ImmersiveModeShortcutHandler,
        input_device,
        input_pipeline::{InputDeviceBindingHashMap, InputPipeline, InputPipelineAssembly},
        keyboard_handler, keymap_handler,
        media_buttons_handler::MediaButtonsHandler,
        mouse_injector_handler::MouseInjectorHandler,
        shortcut_handler::ShortcutHandler,
        touch_injector_handler::TouchInjectorHandler,
    },
    scene_management::{self, SceneManager},
    std::collections::HashSet,
    std::iter::FromIterator,
    std::rc::Rc,
    std::sync::Arc,
    tracing::{error, info, warn},
};

/// Begins handling input events. The returned future will complete when
/// input events are no longer being handled.
///
/// # Parameters
/// - `scene_manager`: The scene manager used by the session.
/// - `input_config_request_stream_receiver`:  A receiving end of a MPSC channel for
///   `InputConfig` messages.
/// - `input_device_registry_request_stream_receiver`: A receiving end of a MPSC channel for
///   `InputDeviceRegistry` messages.
/// - `node`: The inspect node to insert individual inspect handler nodes into.
/// - `focus_chain_publisher`: Forwards focus chain changes to downstream watchers.
pub async fn handle_input(
    // If this is false, it means we're using the legacy Scenic Gfx API, instead of the
    // new Flatland API.
    use_flatland: bool,
    scene_manager: Arc<Mutex<dyn SceneManager>>,
    input_config_request_stream_receiver: futures::channel::mpsc::UnboundedReceiver<
        InputConfigFeaturesRequestStream,
    >,
    input_device_registry_request_stream_receiver: futures::channel::mpsc::UnboundedReceiver<
        InputDeviceRegistryRequestStream,
    >,
    media_buttons_listener_registry_request_stream_receiver: futures::channel::mpsc::UnboundedReceiver<
        DeviceListenerRegistryRequestStream,
    >,
    factory_reset_countdown_request_stream_receiver: futures::channel::mpsc::UnboundedReceiver<
        FactoryResetCountdownRequestStream,
    >,
    factory_reset_device_request_stream_receiver: futures::channel::mpsc::UnboundedReceiver<
        DeviceRequestStream,
    >,
    icu_data_loader: icu_data::Loader,
    node: &inspect::Node,
    display_ownership_event: zx::Event,
    focus_chain_publisher: FocusChainProviderPublisher,
    supported_input_devices: Vec<String>,
) -> Result<InputPipeline, Error> {
    let factory_reset_handler = FactoryResetHandler::new();
    let media_buttons_handler = MediaButtonsHandler::new();

    let supported_input_devices: Vec<input_device::InputDeviceType> = supported_input_devices
        .iter()
        .filter_map(|device| match device.as_str() {
            "button" => Some(input_device::InputDeviceType::ConsumerControls),
            "keyboard" => Some(input_device::InputDeviceType::Keyboard),
            "mouse" => Some(input_device::InputDeviceType::Mouse),
            "touchscreen" => Some(input_device::InputDeviceType::Touch),
            unsupported => {
                warn!("Ignoring unsupported device configuration: {}", unsupported);
                None
            }
        })
        .collect();
    let input_pipeline = InputPipeline::new(
        supported_input_devices.clone(),
        build_input_pipeline_assembly(
            use_flatland,
            scene_manager,
            icu_data_loader,
            node,
            display_ownership_event,
            factory_reset_handler.clone(),
            media_buttons_handler.clone(),
            HashSet::from_iter(supported_input_devices.iter()),
            focus_chain_publisher,
        )
        .await,
    )
    .context("Failed to create InputPipeline.")?;

    let input_device_registry_fut = handle_input_device_registry_request_streams(
        input_device_registry_request_stream_receiver,
        input_pipeline.input_device_types().clone(),
        input_pipeline.input_event_sender().clone(),
        input_pipeline.input_device_bindings().clone(),
    );
    fasync::Task::local(input_device_registry_fut).detach();

    let input_config_fut = handle_input_config_request_streams(
        input_config_request_stream_receiver,
        input_pipeline.input_device_bindings().clone(),
    );
    fasync::Task::local(input_config_fut).detach();

    let factory_reset_countdown_fut = handle_factory_reset_countdown_request_stream(
        factory_reset_countdown_request_stream_receiver,
        factory_reset_handler.clone(),
    );
    fasync::Task::local(factory_reset_countdown_fut).detach();

    let factory_reset_device_device_fut = handle_recovery_policy_device_request_stream(
        factory_reset_device_request_stream_receiver,
        factory_reset_handler.clone(),
    );
    fasync::Task::local(factory_reset_device_device_fut).detach();

    let media_buttons_listener_registry_fut = handle_device_listener_registry_request_stream(
        media_buttons_listener_registry_request_stream_receiver,
        media_buttons_handler.clone(),
    );
    fasync::Task::local(media_buttons_listener_registry_fut).detach();

    Ok(input_pipeline)
}

fn setup_pointer_injector_config_request_stream(
    scene_manager: Arc<Mutex<dyn SceneManager>>,
) -> SetupProxy {
    let (setup_proxy, setup_request_stream) = fidl::endpoints::create_proxy_and_stream::<
        fidl_fuchsia_ui_pointerinjector_configuration::SetupMarker,
    >()
    .expect("Failed to create pointerinjector.configuration.Setup channel.");

    scene_management::handle_pointer_injector_configuration_setup_request_stream(
        setup_request_stream,
        scene_manager,
    );

    setup_proxy
}

async fn add_touchscreen_handler(
    scene_manager: Arc<Mutex<dyn SceneManager>>,
    mut assembly: InputPipelineAssembly,
) -> InputPipelineAssembly {
    let setup_proxy = setup_pointer_injector_config_request_stream(scene_manager.clone());
    let size = scene_manager.lock().await.get_pointerinjection_display_size();
    let touch_handler = TouchInjectorHandler::new_with_config_proxy(setup_proxy, size).await;
    match touch_handler {
        Ok(touch_handler) => {
            fasync::Task::local(touch_handler.clone().watch_viewport()).detach();
            assembly = assembly.add_handler(touch_handler);
        }
        Err(e) => error!(
            "build_input_pipeline_assembly(): Touch injector handler was not installed: {:?}",
            e
        ),
    };
    assembly
}

async fn add_mouse_handler(
    scene_manager: Arc<Mutex<dyn SceneManager>>,
    mut assembly: InputPipelineAssembly,
    sender: futures::channel::mpsc::Sender<CursorMessage>,
) -> InputPipelineAssembly {
    let setup_proxy = setup_pointer_injector_config_request_stream(scene_manager.clone());
    let size = scene_manager.lock().await.get_pointerinjection_display_size();
    let mouse_handler =
        MouseInjectorHandler::new_with_config_proxy(setup_proxy, size, sender).await;
    match mouse_handler {
        Ok(mouse_handler) => {
            fasync::Task::local(mouse_handler.clone().watch_viewport()).detach();
            assembly = assembly.add_handler(mouse_handler);
        }
        Err(e) => error!(
            "build_input_pipeline_assembly(): Mouse injector handler was not installed: {:?}",
            e
        ),
    };
    assembly
}

// Maximum pointer movement during a clickpad press for the gesture to
// be guaranteed to be interpreted as a click. For movement greater than
// this value, upper layers may, e.g., interpret the gesture as a drag.
//
// This value has been tuned for Atlas, and may need further tuning or
// configuration for other devices.
const CLICK_TO_DRAG_THRESHOLD_MM: f32 = 16.0 / 12.0;

async fn build_input_pipeline_assembly(
    use_flatland: bool,
    scene_manager: Arc<Mutex<dyn SceneManager>>,
    icu_data_loader: icu_data::Loader,
    node: &inspect::Node,
    display_ownership_event: zx::Event,
    factory_reset_handler: Rc<FactoryResetHandler>,
    media_buttons_handler: Rc<MediaButtonsHandler>,
    supported_input_devices: HashSet<&input_device::InputDeviceType>,
    focus_chain_publisher: FocusChainProviderPublisher,
) -> InputPipelineAssembly {
    let mut assembly = InputPipelineAssembly::new();
    let (sender, mut receiver) = futures::channel::mpsc::channel(0);
    {
        // Keep this handler first because it keeps performance measurement counters
        // for the rest of the pipeline at entry.
        assembly = add_inspect_handler(node.create_child("input_pipeline_entry"), assembly);

        if supported_input_devices.contains(&input_device::InputDeviceType::Keyboard) {
            info!("Registering keyboard-related input handlers.");

            // Add as early as possible, but not before inspect handlers.
            assembly = add_chromebook_keyboard_handler(assembly);

            // Display ownership deals with keyboard events.
            assembly = assembly.add_display_ownership(display_ownership_event);
            assembly = add_modifier_handler(assembly);

            // Add the text settings handler early in the pipeline to use the
            // keymap settings in the remainder of the pipeline.
            assembly = add_text_settings_handler(assembly);
            assembly = add_keyboard_handler(assembly);
            assembly = add_keymap_handler(assembly);
            assembly = assembly.add_autorepeater();
            assembly = add_dead_keys_handler(assembly, icu_data_loader);
            assembly = add_immersive_mode_shortcut_handler(assembly);

            // Shortcut needs to go before IME.
            assembly = add_shortcut_handler(assembly).await;
            assembly = add_ime(assembly).await;

            // Forward focus to Shortcut Manager.
            // This requires `fuchsia.ui.focus.FocusChainListenerRegistry`
            assembly = assembly.add_focus_listener(focus_chain_publisher);
        }

        if supported_input_devices.contains(&input_device::InputDeviceType::ConsumerControls) {
            info!("Registering consumer controls-related input handlers.");

            // Add factory reset handler before media buttons handler.
            assembly = assembly.add_handler(factory_reset_handler);
            assembly = assembly.add_handler(media_buttons_handler);
        }

        if supported_input_devices.contains(&input_device::InputDeviceType::Mouse) {
            info!("Registering mouse-related input handlers.");

            // Add the click-drag handler before the mouse handler, to allow
            // the click-drag handler to filter events seen by the mouse
            // handler.
            assembly = add_click_drag_handler(assembly);

            // Add the touchpad gestures handler after the click-drag handler,
            // since the gestures handler creates mouse events but already
            // disambiguates between click and drag gestures.
            assembly = add_touchpad_gestures_handler(assembly, node);

            // Add handler to scale pointer motion based on speed of sensor
            // motion. This allows touchpads and mice to be easily used for
            // both precise pointing, and quick motion across the width
            // (or height) of the screen.
            //
            // This handler must come before the PointerMotionDisplayScaleHandler.
            // Otherwise the display scale will be applied quadratically in some
            // cases.
            assembly = add_pointer_sensor_scale_handler(assembly);

            // Add handler to scale pointer motion on high-DPI displays.
            //
            // * This handler is added _after_ the click-drag handler, since the
            //   motion denoising done by click drag handler is a property solely
            //   of the trackpad, and not of the display.
            //
            // * This handler is added _before_ the mouse handler, since _all_
            //   mouse events should be scaled.
            let pointer_scale =
                scene_manager.lock().await.get_display_metrics().physical_pixel_ratio().max(1.0);
            assembly = add_pointer_display_scale_handler(assembly, pointer_scale);

            if use_flatland {
                assembly = add_mouse_handler(scene_manager.clone(), assembly, sender).await;
            } else {
                // We don't have mouse support for GFX. But that's okay,
                // because the devices still using GFX don't support mice
                // anyway.
            }

            let scene_manager = scene_manager.clone();
            fasync::Task::spawn(async move {
                while let Some(message) = receiver.next().await {
                    let mut scene_manager = scene_manager.lock().await;
                    match message {
                        CursorMessage::SetPosition(position) => {
                            scene_manager.set_cursor_position(position)
                        }
                        CursorMessage::SetVisibility(visible) => {
                            scene_manager.set_cursor_visibility(visible)
                        }
                    }
                }
            })
            .detach();
        }

        if supported_input_devices.contains(&input_device::InputDeviceType::Touch) {
            info!("Registering touchscreen-related input handlers.");

            assembly = add_touchscreen_handler(scene_manager.clone(), assembly).await;
        }
    }

    // Keep this handler last because it keeps performance measurement counters
    // for the rest of the pipeline at exit.  We compare these values to the
    // values at entry.
    assembly = add_inspect_handler(node.create_child("input_pipeline_exit"), assembly);

    assembly
}

fn add_chromebook_keyboard_handler(assembly: InputPipelineAssembly) -> InputPipelineAssembly {
    assembly
        .add_handler(input_pipeline::chromebook_keyboard_handler::ChromebookKeyboardHandler::new())
}

/// Hooks up the modifier keys handler.
fn add_modifier_handler(assembly: InputPipelineAssembly) -> InputPipelineAssembly {
    assembly.add_handler(input_pipeline::modifier_handler::ModifierHandler::new())
}

/// Hooks up the inspect handler.
fn add_inspect_handler(
    node: inspect::Node,
    assembly: InputPipelineAssembly,
) -> InputPipelineAssembly {
    assembly.add_handler(input_pipeline::inspect_handler::InspectHandler::new(node))
}

/// Hooks up the text settings handler.
fn add_text_settings_handler(assembly: InputPipelineAssembly) -> InputPipelineAssembly {
    let proxy = connect_to_protocol::<fsettings::KeyboardMarker>()
        .expect("needs a connection to fuchsia.settings.Keyboard");
    let text_handler = TextSettingsHandler::new(None, None);
    text_handler.clone().serve(proxy);
    assembly.add_handler(text_handler)
}

/// Hooks up the keyboard handler.
fn add_keyboard_handler(assembly: InputPipelineAssembly) -> InputPipelineAssembly {
    assembly.add_handler(keyboard_handler::KeyboardHandler::new())
}

/// Hooks up the keymapper.  The keymapper requires the text settings handler to
/// be added as well to support keymapping.  Otherwise, it defaults to applying
/// the US QWERTY keymap.
fn add_keymap_handler(assembly: InputPipelineAssembly) -> InputPipelineAssembly {
    assembly.add_handler(keymap_handler::KeymapHandler::new())
}

/// Hooks up the dead keys handler. This allows us to input accented characters by composing a
/// diacritic and a character.
fn add_dead_keys_handler(
    assembly: InputPipelineAssembly,
    loader: icu_data::Loader,
) -> InputPipelineAssembly {
    assembly.add_handler(dead_keys_handler::Handler::new(loader))
}

async fn add_shortcut_handler(mut assembly: InputPipelineAssembly) -> InputPipelineAssembly {
    if let Ok(manager) = connect_to_protocol::<ui_shortcut::ManagerMarker>() {
        if let Ok(shortcut_handler) = ShortcutHandler::new(manager) {
            assembly = assembly.add_handler(shortcut_handler);
        }
    }
    assembly
}

async fn add_ime(mut assembly: InputPipelineAssembly) -> InputPipelineAssembly {
    if let Ok(ime_handler) = ImeHandler::new().await {
        assembly = assembly.add_handler(ime_handler);
    }
    assembly
}

fn add_click_drag_handler(assembly: InputPipelineAssembly) -> InputPipelineAssembly {
    assembly.add_handler(input_pipeline::click_drag_handler::ClickDragHandler::new(
        CLICK_TO_DRAG_THRESHOLD_MM,
    ))
}

fn add_pointer_display_scale_handler(
    assembly: InputPipelineAssembly,
    scale_factor: f32,
) -> InputPipelineAssembly {
    match input_pipeline::pointer_display_scale_handler::PointerDisplayScaleHandler::new(
        scale_factor,
    ) {
        Ok(handler) => assembly.add_handler(handler),
        Err(e) => {
            error!("Failed to install pointer scaler: {}", e);
            assembly
        }
    }
}

fn add_pointer_sensor_scale_handler(assembly: InputPipelineAssembly) -> InputPipelineAssembly {
    assembly
        .add_handler(input_pipeline::pointer_sensor_scale_handler::PointerSensorScaleHandler::new())
}

fn add_immersive_mode_shortcut_handler(assembly: InputPipelineAssembly) -> InputPipelineAssembly {
    assembly.add_handler(ImmersiveModeShortcutHandler::new())
}

fn add_touchpad_gestures_handler(
    assembly: InputPipelineAssembly,
    inspect_node: &inspect::Node,
) -> InputPipelineAssembly {
    assembly.add_handler(input_pipeline::make_touchpad_gestures_handler(inspect_node))
}

pub async fn handle_input_config_request_streams(
    mut stream_receiver: futures::channel::mpsc::UnboundedReceiver<
        InputConfigFeaturesRequestStream,
    >,
    input_device_bindings: InputDeviceBindingHashMap,
) {
    while let Some(stream) = stream_receiver.next().await {
        match InputPipeline::handle_input_config_request_stream(stream, &input_device_bindings)
            .await
        {
            Ok(()) => (),
            Err(e) => {
                warn!(
                    "failure while serving InputConfig.Features: {}; \
                     will continue serving other clients",
                    e
                );
            }
        }
    }
}

pub async fn handle_device_listener_registry_request_stream(
    mut stream_receiver: futures::channel::mpsc::UnboundedReceiver<
        DeviceListenerRegistryRequestStream,
    >,
    media_buttons_handler: Rc<MediaButtonsHandler>,
) {
    while let Some(stream) = stream_receiver.next().await {
        let media_buttons_handler = media_buttons_handler.clone();
        fasync::Task::local(async move {
            match media_buttons_handler.handle_device_listener_registry_request_stream(stream).await
            {
                Ok(()) => (),
                Err(e) => {
                    warn!("failure while serving DeviceListenerRegistry: {}", e);
                }
            }
        })
        .detach();
    }
}

pub async fn handle_factory_reset_countdown_request_stream(
    mut stream_receiver: futures::channel::mpsc::UnboundedReceiver<
        FactoryResetCountdownRequestStream,
    >,
    factory_reset_handler: Rc<FactoryResetHandler>,
) {
    while let Some(stream) = stream_receiver.next().await {
        let factory_reset_handler = factory_reset_handler.clone();
        fasync::Task::local(async move {
            match factory_reset_handler.handle_factory_reset_countdown_request_stream(stream).await
            {
                Ok(()) => (),
                Err(e) => {
                    warn!("failure while serving FactoryResetCountdown: {}", e);
                }
            }
        })
        .detach();
    }
}

pub async fn handle_recovery_policy_device_request_stream(
    mut stream_receiver: futures::channel::mpsc::UnboundedReceiver<DeviceRequestStream>,
    factory_reset_handler: Rc<FactoryResetHandler>,
) {
    while let Some(stream) = stream_receiver.next().await {
        let factory_reset_handler = factory_reset_handler.clone();
        fasync::Task::local(async move {
            match factory_reset_handler.handle_recovery_policy_device_request_stream(stream).await {
                Ok(()) => (),
                Err(e) => {
                    warn!("failure while serving fuchsia.recovery.policy.Device: {}", e);
                }
            }
        })
        .detach();
    }
}

pub async fn handle_input_device_registry_request_streams(
    mut stream_receiver: futures::channel::mpsc::UnboundedReceiver<
        InputDeviceRegistryRequestStream,
    >,
    input_device_types: Vec<input_device::InputDeviceType>,
    input_event_sender: futures::channel::mpsc::Sender<input_device::InputEvent>,
    input_device_bindings: InputDeviceBindingHashMap,
) {
    // Use a high value device id to avoid conflicting device ids.
    let mut device_id = u32::MAX;

    while let Some(stream) = stream_receiver.next().await {
        let input_device_types_clone = input_device_types.clone();
        let input_event_sender_clone = input_event_sender.clone();
        let input_device_bindings_clone = input_device_bindings.clone();
        // TODO(fxbug.dev/109772): Push this task down to InputPipeline.
        // I didn't do that here, to keep the scope of this change small.
        fasync::Task::local(async move {
            match InputPipeline::handle_input_device_registry_request_stream(
                stream,
                &input_device_types_clone,
                &input_event_sender_clone,
                &input_device_bindings_clone,
                device_id,
            )
            .await
            {
                Ok(()) => (),
                Err(e) => {
                    warn!(
                        "failure while serving InputDeviceRegistry: {}; \
                         will continue serving other clients",
                        e
                    );
                }
            }
        })
        .detach();
        device_id -= 1;
    }
}

#[cfg(test)]
mod tests {
    use fuchsia_async as fasync;

    #[fasync::run_singlethreaded(test)]
    async fn test_placeholder() {
        // TODO(fxb/73643): Add tests that verify the construction of the input pipeline.
    }
}
