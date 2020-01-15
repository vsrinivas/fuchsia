// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fuchsia_async as fasync,
    fuchsia_syslog::fx_log_info,
    futures::channel::mpsc::Receiver,
    futures::StreamExt,
    input::{input_device, mouse},
};

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["input_session"]).expect("Failed to initialize logger.");

    let mut mouse_receiver: Receiver<input_device::InputEvent> = mouse::all_mouse_events().await?;
    while let Some(input_event) = mouse_receiver.next().await {
        match input_event {
            input_device::InputEvent {
                device_event: input_device::InputDeviceEvent::Mouse(mouse_event_descriptor),
                device_descriptor: _,
            } => {
                fx_log_info!("movement_x: {}", mouse_event_descriptor.movement_x);
                fx_log_info!("movement_y: {}", mouse_event_descriptor.movement_y);
                fx_log_info!("phase: {:?}", mouse_event_descriptor.phase);
                fx_log_info!("buttons: {:?}", mouse_event_descriptor.buttons);
            }
            _ => {}
        }
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    #[test]
    fn dummy_test() {
        println!("Don't panic!(), you've got this!");
    }
}
