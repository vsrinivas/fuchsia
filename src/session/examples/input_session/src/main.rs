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
                fx_log_info!("movement_x: {}", mouse_event.movement().x);
                fx_log_info!("movement_y: {}", mouse_event.movement().y);
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
        fidl_fuchsia_test_events::EventType,
        fuchsia_async as fasync,
        futures::future,
        session_manager_lib,
        test_utils_lib::events::{EventSource, Ordering, RecordedEvent},
    };

    /// Verifies that the session is routed the expected capabilities.
    #[fasync::run_singlethreaded(test)]
    async fn test_capability_routing() {
        let event_source = EventSource::new().expect("EventSource is unavailable");

        event_source.start_component_tree().await.expect("Failed to start InputSession");

        let event_future = async move {
            let expected_events = vec![
                RecordedEvent {
                    event_type: EventType::CapabilityRouted,
                    target_moniker: "./session:session:*".to_string(),
                    capability_id: Some("elf".to_string()),
                },
                RecordedEvent {
                    event_type: EventType::CapabilityRouted,
                    target_moniker: "./session:session:*".to_string(),
                    capability_id: Some("/svc/fuchsia.logger.LogSink".to_string()),
                },
                RecordedEvent {
                    event_type: EventType::CapabilityRouted,
                    target_moniker: "./session:session:*".to_string(),
                    capability_id: Some("/dev/class/input-report".to_string()),
                },
            ];

            event_source
                .expect_events(Ordering::Unordered, expected_events)
                .await
                .expect("Failed to expect capability events");
        };

        let session_future = async move {
            let session_url = "fuchsia-pkg://fuchsia.com/input_session#meta/input_session.cm";
            session_manager_lib::startup::launch_session(&session_url)
                .await
                .expect("Failed starting input session");
        };

        future::join(event_future, session_future).await;
    }

    /// Verifies that the session is correctly resolved and launched with out errors.
    #[fasync::run_singlethreaded(test)]
    async fn test_session_lifecycle() {
        let event_source = EventSource::new().expect("EventSource is unavailable");
        event_source.start_component_tree().await.expect("Failed to start InputSession");

        let event_future = async move {
            let expected_events = vec![
                RecordedEvent {
                    event_type: EventType::Resolved,
                    target_moniker: "./session:session:*".to_string(),
                    capability_id: None,
                },
                RecordedEvent {
                    event_type: EventType::Started,
                    target_moniker: "./session:session:*".to_string(),
                    capability_id: None,
                },
            ];

            event_source
                .expect_events(Ordering::Ordered, expected_events)
                .await
                .expect("Failed to expect lifecycle events");
        };

        let session_future = async move {
            let session_url = "fuchsia-pkg://fuchsia.com/input_session#meta/input_session.cm";
            session_manager_lib::startup::launch_session(&session_url)
                .await
                .expect("Failed starting input session");
        };

        future::join(event_future, session_future).await;
    }
}
