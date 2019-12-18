// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::Error,
    fuchsia_async as fasync,
    fuchsia_syslog::fx_log_info,
    futures::channel::mpsc::Receiver,
    futures::StreamExt,
    input::{input_device, mouse},
};

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["input_session"]).expect("Failed to initialize logger.");

    let mut mouse_receiver: Receiver<input_device::InputMessage> =
        mouse::all_mouse_messages().await?;
    while let Some(message) = mouse_receiver.next().await {
        match message {
            input_device::InputMessage::Mouse(mouse_message) => {
                fx_log_info!("movement_x: {}", mouse_message.movement_x);
                fx_log_info!("movement_y: {}", mouse_message.movement_y);
                fx_log_info!("phase: {:?}", mouse_message.phase);
                fx_log_info!("buttons: {}", mouse_message.buttons);
            }
            _ => {}
        }
    }
    Ok(())
}
