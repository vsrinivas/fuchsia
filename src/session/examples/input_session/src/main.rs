// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Error},
    async_trait::async_trait,
    fuchsia_async as fasync,
    fuchsia_syslog::fx_log_info,
    input_pipeline::{
        self, input_device, input_handler::InputHandler, input_pipeline::InputPipeline,
        input_pipeline::InputPipelineAssembly,
    },
    std::rc::Rc,
};

/// A simple InputHandler that prints MouseEvents as they're received.
struct MouseEventPrinter;

impl MouseEventPrinter {
    pub fn new() -> Self {
        MouseEventPrinter {}
    }
}

#[async_trait(?Send)]
impl InputHandler for MouseEventPrinter {
    async fn handle_input_event(
        self: Rc<Self>,
        input_event: input_device::InputEvent,
    ) -> Vec<input_device::InputEvent> {
        match input_event {
            input_device::InputEvent {
                device_event: input_device::InputDeviceEvent::Mouse(mouse_event),
                device_descriptor: input_device::InputDeviceDescriptor::Mouse(_mouse_descriptor),
                event_time,
            } => {
                fx_log_info!("location: {:?}", mouse_event.location);
                fx_log_info!("phase: {:?}", mouse_event.phase);
                fx_log_info!("buttons: {:?}", mouse_event.buttons);
                fx_log_info!("event_time: {:?}", event_time);

                vec![]
            }
            _ => vec![input_event],
        }
    }
}

/// Creates an `InputPipeline` that binds to mouse devices. Logs each mouse event as received.
#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["input_session"]).expect("Failed to initialize logger.");

    let input_pipeline = InputPipeline::new(
        vec![input_device::InputDeviceType::Mouse],
        InputPipelineAssembly::new().add_handler(Rc::new(MouseEventPrinter::new())),
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
        // Note, this test replaces the invalid tests mentioned in fxbug.dev/67160
        println!("Don't panic!(), you've got this!");
    }
}
