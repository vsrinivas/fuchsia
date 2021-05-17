// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::mouse_pointer_hack::*,
    crate::touch_pointer_hack::*,
    anyhow::{Context, Error},
    fidl_fuchsia_input_injection::InputDeviceRegistryRequestStream,
    fidl_fuchsia_ui_policy::PointerCaptureListenerHackProxy,
    fidl_fuchsia_ui_shortcut as ui_shortcut, fuchsia_async as fasync,
    fuchsia_component::client::connect_to_protocol,
    fuchsia_syslog::fx_log_warn,
    futures::lock::Mutex,
    futures::StreamExt,
    input_pipeline::{
        self,
        ime_handler::ImeHandler,
        input_device,
        input_handler::InputHandler,
        input_pipeline::{InputDeviceBindingHashMap, InputPipeline},
        mouse_handler::MouseHandler,
        shortcut_handler::ShortcutHandler,
        text_settings,
        touch_handler::TouchHandler,
        Position, Size,
    },
    scene_management::{self, FlatSceneManager, SceneManager, ScreenCoordinates},
    std::sync::Arc,
};

/// Begins handling input events. The returned future will complete when
/// input events are no longer being handled.
///
/// # Parameters
/// - `scene_manager`: The scene manager used by the session.
/// - `pointer_hack_server`: The pointer hack server, used to fetch listeners for pointer
///    hack input handlers.
/// - `input_device_registry_request_stream_receiver`: A receiving end of a MPSC channel for
///   `InputDeviceRegistry` messages.
/// - `text_settings_handler`: An input pipeline stage that decorates `InputEvent`s with
///    text settings (e.g. desired keymap IDs).
pub async fn handle_input(
    scene_manager: Arc<Mutex<FlatSceneManager>>,
    pointer_hack_listeners: Arc<Mutex<Vec<PointerCaptureListenerHackProxy>>>,
    input_device_registry_request_stream_receiver: futures::channel::mpsc::UnboundedReceiver<
        InputDeviceRegistryRequestStream,
    >,
    text_settings_handler: text_settings::Handler,
) -> Result<InputPipeline, Error> {
    let input_pipeline = InputPipeline::new(
        vec![
            input_device::InputDeviceType::Mouse,
            input_device::InputDeviceType::Touch,
            input_device::InputDeviceType::Keyboard,
        ],
        input_handlers(scene_manager, pointer_hack_listeners, text_settings_handler).await,
    )
    .await
    .context("Failed to create InputPipeline.")?;

    let input_device_registry_fut = handle_input_device_registry_request_streams(
        input_device_registry_request_stream_receiver,
        input_pipeline.input_device_types.clone(),
        input_pipeline.input_event_sender.clone(),
        input_pipeline.input_device_bindings.clone(),
    );

    fasync::Task::local(input_device_registry_fut).detach();

    Ok(input_pipeline)
}

async fn input_handlers(
    scene_manager: Arc<Mutex<FlatSceneManager>>,
    pointer_hack_listeners: Arc<Mutex<Vec<PointerCaptureListenerHackProxy>>>,
    text_settings_handler: text_settings::Handler,
) -> Vec<Box<dyn InputHandler>> {
    let mut handlers: Vec<Box<dyn InputHandler>> = vec![];

    {
        let locked_scene_manager = scene_manager.lock().await;
        // Adds the text settings handler early in the pipeline.
        handlers.push(Box::new(text_settings_handler));
        // Touch and mouse hack handlers are inserted first.
        add_touch_hack(&locked_scene_manager, pointer_hack_listeners.clone(), &mut handlers).await;
        add_mouse_hack(&locked_scene_manager, pointer_hack_listeners.clone(), &mut handlers).await;
        // Shortcut needs to go before IME.
        add_shortcut_handler(&mut handlers).await;
        add_ime(&mut handlers).await;

        add_touch_handler(&locked_scene_manager, &mut handlers).await;
    }

    add_mouse_handler(scene_manager, &mut handlers).await;

    handlers
}

async fn add_shortcut_handler(handlers: &mut Vec<Box<dyn InputHandler>>) {
    if let Ok(manager) = connect_to_protocol::<ui_shortcut::ManagerMarker>() {
        if let Ok(shortcut_handler) = ShortcutHandler::new(manager) {
            handlers.push(Box::new(shortcut_handler));
        }
    }
}

async fn add_ime(handlers: &mut Vec<Box<dyn InputHandler>>) {
    if let Ok(ime_handler) = ImeHandler::new().await {
        handlers.push(Box::new(ime_handler));
    }
}

async fn add_touch_handler(
    scene_manager: &FlatSceneManager,
    handlers: &mut Vec<Box<dyn InputHandler>>,
) {
    let (width_pixels, height_pixels) = scene_manager.display_size.pixels();
    if let Ok(touch_handler) = TouchHandler::new(
        scene_manager.session.clone(),
        scene_manager.compositor_id,
        Size { width: width_pixels, height: height_pixels },
    )
    .await
    {
        handlers.push(Box::new(touch_handler));
    }
}

async fn add_mouse_handler(
    scene_manager: Arc<Mutex<FlatSceneManager>>,
    handlers: &mut Vec<Box<dyn InputHandler>>,
) {
    let (sender, mut receiver) = futures::channel::mpsc::channel(0);
    {
        let scene_manager = scene_manager.lock().await;
        let (width_pixels, height_pixels) = scene_manager.display_size.pixels();
        let mouse_handler = MouseHandler::new(
            Position { x: width_pixels, y: height_pixels },
            Some(sender),
            scene_manager.session.clone(),
            scene_manager.compositor_id,
        );
        handlers.push(Box::new(mouse_handler));
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
}

async fn add_mouse_hack(
    scene_manager: &FlatSceneManager,
    pointer_hack_listeners: Arc<Mutex<Vec<PointerCaptureListenerHackProxy>>>,
    handlers: &mut Vec<Box<dyn InputHandler>>,
) {
    let mouse_hack = MousePointerHack::new(
        scene_manager.display_size.size(),
        1.0 / scene_manager.display_metrics.pixels_per_pip(),
        pointer_hack_listeners.clone(),
    );
    handlers.push(Box::new(mouse_hack));
}

async fn add_touch_hack(
    scene_manager: &FlatSceneManager,
    pointer_hack_listeners: Arc<Mutex<Vec<PointerCaptureListenerHackProxy>>>,
    handlers: &mut Vec<Box<dyn InputHandler>>,
) {
    let touch_hack = TouchPointerHack::new(
        scene_manager.display_size.size(),
        1.0 / scene_manager.display_metrics.pixels_per_pip(),
        pointer_hack_listeners.clone(),
    );

    handlers.push(Box::new(touch_hack));
}

pub async fn handle_input_device_registry_request_streams(
    stream_receiver: futures::channel::mpsc::UnboundedReceiver<InputDeviceRegistryRequestStream>,
    input_device_types: Vec<input_device::InputDeviceType>,
    input_event_sender: futures::channel::mpsc::Sender<input_device::InputEvent>,
    input_device_bindings: InputDeviceBindingHashMap,
) {
    // It's unlikely that multiple clients will concurrently connect to the InputDeviceRegistry.
    // However, if multiple clients do connect concurrently, we don't want said clients
    // depending on the serialization that would be provided by `for_each()`.
    stream_receiver
        .for_each_concurrent(None, |stream| async {
            match InputPipeline::handle_input_device_registry_request_stream(
                stream,
                &input_device_types,
                &input_event_sender,
                &input_device_bindings,
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
        })
        .await;
}

#[cfg(test)]
mod tests {
    use fuchsia_async as fasync;

    #[fasync::run_singlethreaded(test)]
    async fn test_placeholder() {
        // TODO(fxb/73643): Add tests that verify the construction of the input pipeline.
    }
}
