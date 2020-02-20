// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fidl_fuchsia_ui_scenic::ScenicMarker,
    fuchsia_async as fasync,
    fuchsia_component::client::connect_to_service,
    fuchsia_syslog as fsyslog,
    futures::channel::mpsc::Receiver,
    futures::StreamExt,
    input::{input_device, mouse},
    scene_management::{self, SceneManager},
};

/// Connects to the `scenic` service and creates an instance of the `FlatSceneManager`. It then
/// sets the cursor position to the middle of the screen.
#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    fsyslog::init_with_tags(&["scene_manager_session"]).expect("Failed to init syslog");
    let scenic = connect_to_service::<ScenicMarker>()?;
    let mut scene_manager = scene_management::FlatSceneManager::new(scenic, None, None).await?;

    let width = scene_manager.display_metrics.width_in_pips();
    let height = scene_manager.display_metrics.height_in_pips();

    let mut x = width / 2.0;
    let mut y = height / 2.0;
    let mut mouse_receiver: Receiver<input_device::InputEvent> = mouse::all_mouse_events().await?;
    while let Some(input_event) = mouse_receiver.next().await {
        match input_event {
            input_device::InputEvent {
                device_event: input_device::InputDeviceEvent::Mouse(mouse_event_descriptor),
                device_descriptor: _,
                event_time: _,
            } => {
                x += mouse_event_descriptor.movement_x as f32;
                y += mouse_event_descriptor.movement_y as f32;
                clamp(&mut x, 0.0, width);
                clamp(&mut y, 0.0, height);
                scene_manager.set_cursor_location(x, y);
            }
            _ => {}
        }
    }

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
