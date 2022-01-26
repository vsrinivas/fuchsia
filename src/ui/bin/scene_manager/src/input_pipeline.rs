// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    ::input_pipeline::{text_settings_handler::TextSettingsHandler, CursorMessage},
    anyhow::{Context, Error},
    fidl_fuchsia_input_injection::InputDeviceRegistryRequestStream,
    fidl_fuchsia_settings as fsettings,
    fidl_fuchsia_ui_pointerinjector_configuration::SetupProxy,
    fidl_fuchsia_ui_shortcut as ui_shortcut, fuchsia_async as fasync,
    fuchsia_component::client::connect_to_protocol,
    fuchsia_inspect as inspect,
    fuchsia_syslog::{fx_log_err, fx_log_warn},
    futures::{lock::Mutex, StreamExt},
    icu_data,
    input_pipeline::{
        self, dead_keys_handler,
        ime_handler::ImeHandler,
        input_device,
        input_pipeline::{InputDeviceBindingHashMap, InputPipeline, InputPipelineAssembly},
        keymap_handler,
        mouse_injector_handler::MouseInjectorHandler,
        shortcut_handler::ShortcutHandler,
        touch_injector_handler::TouchInjectorHandler,
    },
    scene_management::{self, SceneManager},
    std::sync::Arc,
};

/// Begins handling input events. The returned future will complete when
/// input events are no longer being handled.
///
/// # Parameters
/// - `scene_manager`: The scene manager used by the session.
/// - `input_device_registry_request_stream_receiver`: A receiving end of a MPSC channel for
///   `InputDeviceRegistry` messages.
/// - `node`: The inspect node to insert individual inspect handler nodes into.
pub async fn handle_input(
    // If this is false, it means we're using the legacy Scenic Gfx API, instead of the
    // new Flatland API.
    use_flatland: bool,
    scene_manager: Arc<Mutex<Box<dyn SceneManager>>>,
    input_device_registry_request_stream_receiver: futures::channel::mpsc::UnboundedReceiver<
        InputDeviceRegistryRequestStream,
    >,
    icu_data_loader: icu_data::Loader,
    node: &inspect::Node,
) -> Result<InputPipeline, Error> {
    let input_pipeline = InputPipeline::new(
        vec![
            input_device::InputDeviceType::Mouse,
            input_device::InputDeviceType::Touch,
            input_device::InputDeviceType::Keyboard,
        ],
        build_input_pipeline_assembly(use_flatland, scene_manager, icu_data_loader, node).await,
    )
    .context("Failed to create InputPipeline.")?;

    let input_device_registry_fut = handle_input_device_registry_request_streams(
        input_device_registry_request_stream_receiver,
        input_pipeline.input_device_types().clone(),
        input_pipeline.input_event_sender().clone(),
        input_pipeline.input_device_bindings().clone(),
    );

    fasync::Task::local(input_device_registry_fut).detach();

    Ok(input_pipeline)
}

fn setup_pointer_injector_config_request_stream(
    scene_manager: Arc<Mutex<Box<dyn SceneManager>>>,
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

async fn add_flatland_touch_handler(
    scene_manager: Arc<Mutex<Box<dyn SceneManager>>>,
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
        Err(e) => fx_log_err!(
            "build_input_pipeline_assembly(): Touch injector handler was not installed: {:?}",
            e
        ),
    };
    assembly
}

async fn add_flatland_mouse_handler(
    scene_manager: Arc<Mutex<Box<dyn SceneManager>>>,
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
        Err(e) => fx_log_err!(
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
const CLICK_TO_DRAG_THRESHOLD: f32 = 16.0;

async fn build_input_pipeline_assembly(
    use_flatland: bool,
    scene_manager: Arc<Mutex<Box<dyn SceneManager>>>,
    icu_data_loader: icu_data::Loader,
    node: &inspect::Node,
) -> InputPipelineAssembly {
    let mut assembly = InputPipelineAssembly::new();
    let (sender, mut receiver) = futures::channel::mpsc::channel(0);
    {
        assembly = add_modifier_handler(assembly);
        assembly = add_inspect_handler(node.create_child("input_pipeline_entry"), assembly);
        // Add the text settings handler early in the pipeline to use the
        // keymap settings in the remainder of the pipeline.
        assembly = add_text_settings_handler(assembly);
        assembly = add_keymap_handler(assembly);
        assembly = assembly.add_autorepeater();
        assembly = add_dead_keys_handler(assembly, icu_data_loader);
        // Shortcut needs to go before IME.
        assembly = add_shortcut_handler(assembly).await;
        assembly = add_ime(assembly).await;
        // Add the click-drag handler before the mouse handler, to allow
        // the click-drag handler to filter events seen by the mouse
        // handler.
        assembly = add_click_drag_handler(assembly);

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
        assembly = add_pointer_motion_scale_handler(assembly, pointer_scale);

        if use_flatland {
            assembly = add_flatland_touch_handler(scene_manager.clone(), assembly).await;
            assembly = add_flatland_mouse_handler(scene_manager.clone(), assembly, sender).await;
        } else {
            let locked_scene_manager = scene_manager.lock().await;
            assembly = locked_scene_manager.add_touch_handler(assembly).await;
            assembly = locked_scene_manager.add_mouse_handler(sender, assembly).await;
        }

        assembly = add_inspect_handler(node.create_child("input_pipeline_exit"), assembly);
    }

    {
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

    assembly
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
        CLICK_TO_DRAG_THRESHOLD,
    ))
}

fn add_pointer_motion_scale_handler(
    assembly: InputPipelineAssembly,
    scale_factor: f32,
) -> InputPipelineAssembly {
    match input_pipeline::pointer_motion_scale_handler::PointerMotionScaleHandler::new(scale_factor)
    {
        Ok(handler) => assembly.add_handler(handler),
        Err(e) => {
            fx_log_err!("Failed to install pointer scaler: {}", e);
            assembly
        }
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
        match InputPipeline::handle_input_device_registry_request_stream(
            stream,
            &input_device_types,
            &input_event_sender,
            &input_device_bindings,
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
