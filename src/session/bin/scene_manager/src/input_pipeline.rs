// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Error},
    fidl_fuchsia_input_injection::InputDeviceRegistryRequestStream,
    fidl_fuchsia_ui_shortcut as ui_shortcut, fuchsia_async as fasync,
    fuchsia_component::client::connect_to_protocol,
    fuchsia_inspect as inspect,
    fuchsia_syslog::fx_log_warn,
    futures::lock::Mutex,
    futures::StreamExt,
    input_pipeline::{
        self,
        ime_handler::ImeHandler,
        input_device,
        input_pipeline::{InputDeviceBindingHashMap, InputPipeline, InputPipelineAssembly},
        keymap,
        mouse_handler::MouseHandler,
        shortcut_handler::ShortcutHandler,
        text_settings,
        touch_handler::TouchHandler,
        Position, Size,
    },
    scene_management::{self, FlatSceneManager, SceneManager, ScreenCoordinates},
    std::rc::Rc,
    std::sync::Arc,
};

/// Begins handling input events. The returned future will complete when
/// input events are no longer being handled.
///
/// # Parameters
/// - `scene_manager`: The scene manager used by the session.
/// - `input_device_registry_request_stream_receiver`: A receiving end of a MPSC channel for
///   `InputDeviceRegistry` messages.
/// - `text_settings_handler`: An input pipeline stage that decorates `InputEvent`s with
///    text settings (e.g. desired keymap IDs).
/// - `node`: The inspect node to insert individual inspect handler nodes into.
pub async fn handle_input(
    scene_manager: Arc<Mutex<FlatSceneManager>>,
    input_device_registry_request_stream_receiver: futures::channel::mpsc::UnboundedReceiver<
        InputDeviceRegistryRequestStream,
    >,
    text_settings_handler: Rc<text_settings::Handler>,
    node: &inspect::Node,
) -> Result<InputPipeline, Error> {
    let input_pipeline = InputPipeline::new(
        vec![
            input_device::InputDeviceType::Mouse,
            input_device::InputDeviceType::Touch,
            input_device::InputDeviceType::Keyboard,
        ],
        input_handlers(scene_manager, text_settings_handler, node).await,
    )
    .await
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

async fn input_handlers(
    scene_manager: Arc<Mutex<FlatSceneManager>>,
    text_settings_handler: Rc<text_settings::Handler>,
    node: &inspect::Node,
) -> InputPipelineAssembly {
    let mut assembly = InputPipelineAssembly::new();
    {
        let locked_scene_manager = scene_manager.lock().await;
        assembly = add_inspect_handler(node.create_child("input_pipeline_entry"), assembly);
        // Add the text settings handler early in the pipeline to use the
        // keymap settings in the remainder of the pipeline.
        assembly = add_text_settings_handler(text_settings_handler, assembly);
        assembly = add_keymap_handler(assembly);
        // Shortcut needs to go before IME.
        assembly = add_shortcut_handler(assembly).await;
        assembly = add_ime(assembly).await;

        assembly = add_touch_handler(&locked_scene_manager, assembly).await;
    }
    assembly = add_mouse_handler(scene_manager, assembly).await;
    assembly = add_inspect_handler(node.create_child("input_pipeline_exit"), assembly);

    assembly
}

/// Hooks up the inspect handler.
fn add_inspect_handler(
    node: inspect::Node,
    assembly: InputPipelineAssembly,
) -> InputPipelineAssembly {
    assembly.add_handler(input_pipeline::inspect::Handler::new(node))
}

/// Hooks up the text settings handler.
fn add_text_settings_handler(
    text_settings_handler: Rc<text_settings::Handler>,
    assembly: InputPipelineAssembly,
) -> InputPipelineAssembly {
    assembly.add_handler(text_settings_handler)
}

/// Hooks up the keymapper.  The keymapper requires the text settings handler to
/// be added as well to support keymapping.  Otherwise, it defaults to applying
/// the US QWERTY keymap.
fn add_keymap_handler(assembly: InputPipelineAssembly) -> InputPipelineAssembly {
    assembly.add_handler(keymap::Handler::new())
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

async fn add_touch_handler(
    scene_manager: &FlatSceneManager,
    mut assembly: InputPipelineAssembly,
) -> InputPipelineAssembly {
    let (width_pixels, height_pixels) = scene_manager.display_size.pixels();
    if let Ok(touch_handler) = TouchHandler::new(
        scene_manager.session.clone(),
        scene_manager.compositor_id,
        Size { width: width_pixels, height: height_pixels },
    )
    .await
    {
        assembly = assembly.add_handler(touch_handler);
    }
    assembly
}

async fn add_mouse_handler(
    scene_manager: Arc<Mutex<FlatSceneManager>>,
    mut assembly: InputPipelineAssembly,
) -> InputPipelineAssembly {
    let (sender, mut receiver) = futures::channel::mpsc::channel(0);
    {
        let scene_manager = scene_manager.lock().await;
        let (width_pixels, height_pixels) = scene_manager.display_size.pixels();
        let mouse_handler = MouseHandler::new(
            Position { x: width_pixels, y: height_pixels },
            sender,
            scene_manager.session.clone(),
            scene_manager.compositor_id,
        );
        assembly = assembly.add_handler(mouse_handler);
    }

    fasync::Task::spawn(async move {
        while let Some(Position { x, y }) = receiver.next().await {
            let mut scene_manager = scene_manager.lock().await;
            let screen_coordinates =
                ScreenCoordinates::from_pixels(x, y, scene_manager.display_metrics);
            scene_manager.set_cursor_location(screen_coordinates);
        }
    })
    .detach();
    assembly
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
