// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::mouse_pointer_hack::*,
    crate::pointer_hack_server::PointerHackServer,
    crate::touch_pointer_hack::*,
    fidl_fuchsia_ui_shortcut as ui_shortcut, fuchsia_async as fasync,
    fuchsia_component::client::connect_to_service,
    futures::StreamExt,
    input::{
        ime_handler::ImeHandler, input_device::InputDeviceBinding, input_handler::InputHandler,
        input_pipeline::InputPipeline, keyboard, mouse, mouse_handler, mouse_handler::MouseHandler,
        shortcut_handler::ShortcutHandler, touch, touch_handler::TouchHandler,
    },
    scene_management::{self, FlatSceneManager, SceneManager},
};

/// Begins handling input events. The returned future will complete when
/// input events are no longer being handled.
///
/// # Parameters
/// - `scene_manager`: The scene manager used by the session.
/// - `pointer_hack_server`: The pointer hack server, used to fetch listeners for pointer
/// hack input handlers.
pub async fn handle_input(
    scene_manager: FlatSceneManager,
    pointer_hack_server: &PointerHackServer,
) {
    let input_pipeline = InputPipeline::new(
        device_bindings().await,
        input_handlers(scene_manager, pointer_hack_server).await,
    );

    input_pipeline.handle_input_events().await;
}

async fn device_bindings() -> Vec<Box<dyn InputDeviceBinding>> {
    let mut bindings: Vec<Box<dyn InputDeviceBinding>> = vec![];
    if let Ok(touch_bindings) = touch::all_touch_bindings().await {
        let mut boxed_bindings: Vec<Box<dyn InputDeviceBinding>> = touch_bindings
            .into_iter()
            .map(|binding| Box::new(binding) as Box<dyn InputDeviceBinding>)
            .collect();
        bindings.append(&mut boxed_bindings);
    }

    if let Ok(mouse_bindings) = mouse::all_mouse_bindings().await {
        let mut boxed_bindings: Vec<Box<dyn InputDeviceBinding>> = mouse_bindings
            .into_iter()
            .map(|binding| Box::new(binding) as Box<dyn InputDeviceBinding>)
            .collect();
        bindings.append(&mut boxed_bindings);
    }

    if let Ok(keyboard_bindings) = keyboard::all_keyboard_bindings().await {
        let mut boxed_bindings: Vec<Box<dyn InputDeviceBinding>> = keyboard_bindings
            .into_iter()
            .map(|binding| Box::new(binding) as Box<dyn InputDeviceBinding>)
            .collect();
        bindings.append(&mut boxed_bindings);
    }

    bindings
}

async fn input_handlers(
    scene_manager: FlatSceneManager,
    pointer_hack_server: &PointerHackServer,
) -> Vec<Box<dyn InputHandler>> {
    let mut handlers: Vec<Box<dyn InputHandler>> = vec![];

    // Touch and mouse hack handlers are inserted first.
    add_touch_hack(&scene_manager, &pointer_hack_server, &mut handlers).await;
    add_mouse_hack(&scene_manager, &pointer_hack_server, &mut handlers).await;
    // Shortcut needs to go before IME.
    add_shortcut_handler(&mut handlers).await;
    add_ime(&scene_manager, &mut handlers).await;
    add_touch_handler(&scene_manager, &mut handlers).await;
    add_mouse_handler(scene_manager, &mut handlers).await;

    handlers
}

async fn add_shortcut_handler(handlers: &mut Vec<Box<dyn InputHandler>>) {
    if let Ok(manager) = connect_to_service::<ui_shortcut::ManagerMarker>() {
        if let Ok(shortcut_handler) = ShortcutHandler::new(manager) {
            handlers.push(Box::new(shortcut_handler));
        }
    }
}

async fn add_ime(scene_manager: &FlatSceneManager, handlers: &mut Vec<Box<dyn InputHandler>>) {
    if let Ok(ime_handler) =
        ImeHandler::new(scene_manager.session.clone(), scene_manager.compositor_id).await
    {
        handlers.push(Box::new(ime_handler));
    }
}

async fn add_touch_handler(
    scene_manager: &FlatSceneManager,
    handlers: &mut Vec<Box<dyn InputHandler>>,
) {
    if let Ok(touch_handler) = TouchHandler::new(
        scene_manager.session.clone(),
        scene_manager.compositor_id,
        scene_manager.display_width as i64,
        scene_manager.display_height as i64,
    )
    .await
    {
        handlers.push(Box::new(touch_handler));
    }
}

async fn add_mouse_handler(
    mut scene_manager: FlatSceneManager,
    handlers: &mut Vec<Box<dyn InputHandler>>,
) {
    let (sender, mut receiver) = futures::channel::mpsc::channel(0);
    let mouse_handler = MouseHandler::new(
        mouse_handler::CursorLocation {
            x: scene_manager.display_width,
            y: scene_manager.display_height,
        },
        Some(sender),
        scene_manager.session.clone(),
        scene_manager.compositor_id,
    );
    handlers.push(Box::new(mouse_handler));

    fasync::spawn(async move {
        while let Some(mouse_handler::CursorLocation { x, y }) = receiver.next().await {
            scene_manager.set_cursor_location(
                x / scene_manager.display_metrics.pixels_per_pip(),
                y / scene_manager.display_metrics.pixels_per_pip(),
            );
        }
    });
}

async fn add_mouse_hack(
    scene_manager: &FlatSceneManager,
    pointer_hack_server: &PointerHackServer,
    handlers: &mut Vec<Box<dyn InputHandler>>,
) {
    let mouse_hack = MousePointerHack::new(
        scene_manager.display_width,
        scene_manager.display_height,
        1.0 / scene_manager.display_metrics.pixels_per_pip(),
        pointer_hack_server.pointer_listeners.clone(),
    );
    handlers.push(Box::new(mouse_hack));
}

async fn add_touch_hack(
    scene_manager: &FlatSceneManager,
    pointer_hack_server: &PointerHackServer,
    handlers: &mut Vec<Box<dyn InputHandler>>,
) {
    let touch_hack = TouchPointerHack::new(
        scene_manager.display_width,
        scene_manager.display_height,
        1.0 / scene_manager.display_metrics.pixels_per_pip(),
        pointer_hack_server.pointer_listeners.clone(),
    );

    handlers.push(Box::new(touch_hack));
}
