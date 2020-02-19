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
    use {
        fidl_fuchsia_test_events::EventType,
        fuchsia_async as fasync,
        futures::future,
        session_manager_lib,
        test_utils_lib::events::{EventSource, RecordedEvent},
    };

    /// Verifies that the session is routed the expected capabilities in the expected order.
    #[fasync::run_singlethreaded(test)]
    async fn test_capability_routing() {
        let event_source = EventSource::new().expect("EventSource is unavailable");

        event_source.start_component_tree().await.expect("Failed to start InputSession");

        let event_future = async move {
            let expected_events = vec![
                RecordedEvent {
                    event_type: EventType::RouteCapability,
                    target_moniker: "./session:session:*".to_string(),
                    capability_id: Some("elf".to_string()),
                },
                RecordedEvent {
                    event_type: EventType::RouteCapability,
                    target_moniker: "./session:session:*".to_string(),
                    capability_id: Some("/svc/fuchsia.logger.LogSink".to_string()),
                },
                RecordedEvent {
                    event_type: EventType::RouteCapability,
                    target_moniker: "./session:session:*".to_string(),
                    capability_id: Some("/dev/class/input-report".to_string()),
                },
            ];

            event_source
                .expect_events(expected_events)
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
                    event_type: EventType::ResolveInstance,
                    target_moniker: "./session:session:*".to_string(),
                    capability_id: None,
                },
                RecordedEvent {
                    event_type: EventType::BeforeStartInstance,
                    target_moniker: "./session:session:*".to_string(),
                    capability_id: None,
                },
            ];

            event_source
                .expect_events(expected_events)
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
