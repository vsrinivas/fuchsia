// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::mouse_pointer_hack::*,
    crate::pointer_hack_server::PointerHackServer,
    crate::touch_pointer_hack::*,
    anyhow::{Context, Error},
    fidl_fuchsia_ui_shortcut as ui_shortcut, fuchsia_async as fasync,
    fuchsia_component::client::connect_to_service,
    futures::StreamExt,
    input::{
        ime_handler::ImeHandler, input_device, input_handler::InputHandler,
        input_pipeline::InputPipeline, mouse_handler::MouseHandler,
        shortcut_handler::ShortcutHandler, touch_handler::TouchHandler, Position, Size,
    },
    scene_management::{self, FlatSceneManager, SceneManager, ScreenCoordinates},
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
) -> Result<(), Error> {
    let input_pipeline = InputPipeline::new(
        vec![
            input_device::InputDeviceType::Mouse,
            input_device::InputDeviceType::Touch,
            input_device::InputDeviceType::Keyboard,
        ],
        input_handlers(scene_manager, pointer_hack_server).await,
    )
    .await
    .context("Failed to create InputPipeline.")?;

    input_pipeline.handle_input_events().await;
    Ok(())
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
    let (width_pixels, height_pixels) = scene_manager.display_size().pixels();
    if let Ok(touch_handler) = TouchHandler::new2(
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
    mut scene_manager: FlatSceneManager,
    handlers: &mut Vec<Box<dyn InputHandler>>,
) {
    let (sender, mut receiver) = futures::channel::mpsc::channel(0);
    let (width_pixels, height_pixels) = scene_manager.display_size().pixels();
    let mouse_handler = MouseHandler::new2(
        Position { x: width_pixels, y: height_pixels },
        Some(sender),
        scene_manager.session.clone(),
        scene_manager.compositor_id,
    );
    handlers.push(Box::new(mouse_handler));

    fasync::spawn(async move {
        while let Some(Position { x, y }) = receiver.next().await {
            let screen_coordinates = ScreenCoordinates::from_pixels(x, y, scene_manager.display_metrics);
            scene_manager.set_cursor_location2(screen_coordinates);
        }
    });
}

async fn add_mouse_hack(
    scene_manager: &FlatSceneManager,
    pointer_hack_server: &PointerHackServer,
    handlers: &mut Vec<Box<dyn InputHandler>>,
) {
    let mouse_hack = MousePointerHack::new(
        scene_manager.display_size().size(),
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
        scene_manager.display_size().size(),
        1.0 / scene_manager.display_metrics.pixels_per_pip(),
        pointer_hack_server.pointer_listeners.clone(),
    );

    handlers.push(Box::new(touch_hack));
}
