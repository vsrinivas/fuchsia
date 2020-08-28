// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Error},
    async_trait::async_trait,
    fuchsia_async as fasync,
    fuchsia_syslog::fx_log_info,
    input::{input_device, input_handler::InputHandler, input_pipeline::InputPipeline},
};

/// A simple InputHandler that prints MouseEvents as they're received.
struct MouseEventPrinter;

impl MouseEventPrinter {
    pub fn new() -> Self {
        MouseEventPrinter {}
    }
}

#[async_trait]
impl InputHandler for MouseEventPrinter {
    async fn handle_input_event(
        &mut self,
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
        vec![Box::new(MouseEventPrinter::new())],
    )
    .await
    .context("Failed to create InputPipeline.")?;

    input_pipeline.handle_input_events().await;

    Ok(())
}

#[cfg(test)]
mod tests {
    use {
        fuchsia_async as fasync, session_manager_lib,
        test_utils_lib::events::{
            CapabilityRouted, EventMatcher, EventSource, Ordering, Resolved, Started,
        },
    };

    async fn run_input_session(ordering: Ordering, expected_events: Vec<EventMatcher>) {
        let event_source = EventSource::new_sync().unwrap();
        event_source.start_component_tree().await.unwrap();

        let expectation = event_source.expect_events(ordering, expected_events).await.unwrap();

        let session_url = "fuchsia-pkg://fuchsia.com/input_session#meta/input_session.cm";
        session_manager_lib::startup::launch_session(&session_url)
            .await
            .expect("Failed starting input session");

        expectation.await.unwrap();
    }

    /// Verifies that the session is routed the expected capabilities.
    #[fasync::run_singlethreaded(test)]
    async fn test_capability_routing() {
        let expected_events = vec![
            EventMatcher::new()
                .expect_type::<CapabilityRouted>()
                .expect_moniker("./session:session:*")
                .expect_capability_id("elf"),
            EventMatcher::new()
                .expect_type::<CapabilityRouted>()
                .expect_moniker("./session:session:*")
                .expect_capability_id("/svc/fuchsia.logger.LogSink"),
            EventMatcher::new()
                .expect_type::<CapabilityRouted>()
                .expect_moniker("./session:session:*")
                .expect_capability_id("/dev/class/input-report"),
        ];

        run_input_session(Ordering::Unordered, expected_events).await;
    }

    /// Verifies that the session is correctly resolved and launched with out errors.
    #[fasync::run_singlethreaded(test)]
    async fn test_session_lifecycle() {
        let expected_events = vec![
            EventMatcher::new().expect_type::<Resolved>().expect_moniker("./session:session:*"),
            EventMatcher::new().expect_type::<Started>().expect_moniker("./session:session:*"),
        ];

        run_input_session(Ordering::Ordered, expected_events).await;
    }
}
