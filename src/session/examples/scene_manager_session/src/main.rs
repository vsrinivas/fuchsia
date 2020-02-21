// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Error},
    async_trait::async_trait,
    fidl_fuchsia_ui_scenic::ScenicMarker,
    fuchsia_async as fasync,
    fuchsia_component::client::connect_to_service,
    fuchsia_syslog as fsyslog,
    input::{input_device, input_handler::InputHandler, input_pipeline::InputPipeline},
    scene_management::{self, SceneManager},
};

/// A simple InputHandler that draws a cursor on screen.
struct SimpleCursor {
    x: f32,
    y: f32,
    width: f32,
    height: f32,
    scene_manager: scene_management::FlatSceneManager,
}

impl SimpleCursor {
    pub fn new(
        x: f32,
        y: f32,
        width: f32,
        height: f32,
        scene_manager: scene_management::FlatSceneManager,
    ) -> Self {
        SimpleCursor { x, y, width, height, scene_manager }
    }
}

#[async_trait]
impl InputHandler for SimpleCursor {
    async fn handle_input_event(
        &mut self,
        input_event: input_device::InputEvent,
    ) -> Vec<input_device::InputEvent> {
        match input_event {
            input_device::InputEvent {
                device_event: input_device::InputDeviceEvent::Mouse(mouse_event),
                device_descriptor: input_device::InputDeviceDescriptor::Mouse(_mouse_descriptor),
                event_time: _,
            } => {
                self.x += mouse_event.movement_x as f32;
                self.y += mouse_event.movement_y as f32;
                clamp(&mut self.x, 0.0, self.width);
                clamp(&mut self.y, 0.0, self.height);
                self.scene_manager.set_cursor_location(self.x, self.y);

                vec![]
            }
            _ => vec![input_event],
        }
    }
}

/// Connects to the `scenic` service and creates an instance of the `FlatSceneManager`. It then
/// sets the cursor position to the middle of the screen.
#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    fsyslog::init_with_tags(&["scene_manager_session"]).expect("Failed to init syslog");
    let scenic = connect_to_service::<ScenicMarker>()?;
    let scene_manager = scene_management::FlatSceneManager::new(scenic, None, None).await?;

    let width = scene_manager.display_metrics.width_in_pips();
    let height = scene_manager.display_metrics.height_in_pips();
    let x = width / 2.0;
    let y = height / 2.0;

    let input_pipeline = InputPipeline::new(
        vec![input_device::InputDeviceType::Mouse],
        vec![Box::new(SimpleCursor::new(x, y, width, height, scene_manager))],
    )
    .await
    .context("Failed to create InputPipeline.")?;

    input_pipeline.handle_input_events().await;

    Ok(())
}

// TODO: f32::clamp is still experimental so just implement our own
// https://github.com/rust-lang/rust/issues/44095
fn clamp(target: &mut f32, min: f32, max: f32) {
    if *target < min {
        *target = min;
    }
    if *target > max {
        *target = max;
    }
}

#[cfg(test)]
mod tests {
    #[test]
    fn dummy_test() {
        println!("Don't panic!(), you've got this!");
    }
}
