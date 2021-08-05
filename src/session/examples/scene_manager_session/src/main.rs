// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Error},
    async_trait::async_trait,
    fidl_fuchsia_ui_scenic::ScenicMarker,
    fuchsia_async as fasync,
    fuchsia_component::client::connect_to_protocol,
    fuchsia_syslog as fsyslog,
    input_pipeline::{
        self, input_device, input_handler::InputHandler, input_pipeline::InputPipeline, mouse,
        Position,
    },
    scene_management::{self, SceneManager, ScreenCoordinates},
    std::cell::RefCell,
    std::rc::Rc,
};

/// A simple InputHandler that draws a cursor on screen.
struct SimpleCursor {
    position: RefCell<Position>,
    max_position: Position,
    scene_manager: RefCell<scene_management::FlatSceneManager>,
}

#[async_trait(?Send)]
impl InputHandler for SimpleCursor {
    async fn handle_input_event(
        self: Rc<Self>,
        input_event: input_device::InputEvent,
    ) -> Vec<input_device::InputEvent> {
        match input_event {
            input_device::InputEvent {
                device_event: input_device::InputDeviceEvent::Mouse(mouse_event),
                device_descriptor: input_device::InputDeviceDescriptor::Mouse(_mouse_descriptor),
                event_time: _,
            } => {
                let mut mut_position = self.position.borrow_mut();
                *mut_position = match mouse_event.location {
                    mouse::MouseLocation::Relative(offset) if offset != Position::zero() => {
                        *mut_position + offset
                    }
                    mouse::MouseLocation::Absolute(position) if position != *mut_position => {
                        position
                    }
                    _ => return vec![],
                };

                Position::clamp(&mut *mut_position, Position { x: 0.0, y: 0.0 }, self.max_position);

                let mut scene_manager = self.scene_manager.borrow_mut();
                let display_metrics = scene_manager.display_metrics.clone();
                scene_manager.set_cursor_location(ScreenCoordinates::from_position(
                    &mut *mut_position,
                    display_metrics,
                ));

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
    let scenic = connect_to_protocol::<ScenicMarker>()?;
    let scene_manager = scene_management::FlatSceneManager::new(scenic, None, None).await?;

    let width = scene_manager.display_metrics.width_in_pixels() as f32;
    let height = scene_manager.display_metrics.height_in_pixels() as f32;

    let position = Position { x: width / 2.0, y: height / 2.0 };
    let max_position = Position { x: width, y: height };

    let input_pipeline = InputPipeline::new(
        vec![input_device::InputDeviceType::Mouse],
        vec![Rc::new(SimpleCursor {
            position: RefCell::new(position),
            max_position,
            scene_manager: RefCell::new(scene_manager),
        })],
    )
    .await
    .context("Failed to create InputPipeline.")?;

    input_pipeline.handle_input_events().await;

    Ok(())
}

#[cfg(test)]
mod tests {
    #[test]
    fn noop_test() {
        println!("Don't panic!(), you've got this!");
    }
}
