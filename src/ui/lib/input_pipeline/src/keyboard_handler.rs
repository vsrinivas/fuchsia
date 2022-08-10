// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::input_device, crate::input_handler::UnhandledInputHandler, async_trait::async_trait,
    fidl_fuchsia_input_interaction_observation as interaction_observation,
    fuchsia_syslog::fx_log_err, fuchsia_zircon as zx, std::rc::Rc,
};

/// A `KeyboardHandler` reports keyboard activity to the activity service.
#[derive(Debug)]
pub struct KeyboardHandler {
    /// The FIDL proxy used to report key press activity to the activity
    /// service.
    aggregator_proxy: Option<interaction_observation::AggregatorProxy>,
}

/// This trait implementation allows the [KeyboardHandler] to be hooked up into
/// the input pipeline.
#[async_trait(?Send)]
impl UnhandledInputHandler for KeyboardHandler {
    async fn handle_unhandled_input_event(
        self: Rc<Self>,
        input_event: input_device::UnhandledInputEvent,
    ) -> Vec<input_device::InputEvent> {
        if let input_device::UnhandledInputEvent {
            device_event: input_device::InputDeviceEvent::Keyboard(_),
            device_descriptor: _,
            event_time,
            trace_id: _,
        } = input_event
        {
            // Report the event to the Activity Service.
            if let Err(e) = self.report_keyboard_activity(event_time).await {
                fx_log_err!("report_key_activity failed: {}", e);
            }
        }

        // Return event unchanged no matter what.
        vec![input_device::InputEvent::from(input_event)]
    }
}

impl KeyboardHandler {
    /// Creates a new instance of the [`KeyboardHandler`].
    pub fn new() -> Rc<Self> {
        let aggregator_proxy = match fuchsia_component::client::connect_to_protocol::<
            interaction_observation::AggregatorMarker,
        >() {
            Ok(proxy) => Some(proxy),
            Err(e) => {
                fx_log_err!("KeyboardHandler failed to connect to fuchsia.input.interaction.observation.Aggregator: {}", e);
                None
            }
        };

        Self::new_internal(aggregator_proxy)
    }

    fn new_internal(
        aggregator_proxy: Option<interaction_observation::AggregatorProxy>,
    ) -> Rc<Self> {
        Rc::new(Self { aggregator_proxy })
    }

    /// Reports the given event_time to the activity service, if available.
    async fn report_keyboard_activity(&self, event_time: zx::Time) -> Result<(), fidl::Error> {
        if let Some(proxy) = self.aggregator_proxy.clone() {
            return proxy.report_discrete_activity(event_time.into_nanos()).await;
        }

        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::input_device::InputDeviceDescriptor,
        crate::input_handler::InputHandler,
        crate::testing_utilities::{create_fake_input_event, create_keyboard_event},
        assert_matches::assert_matches,
        fidl_fuchsia_input::Key,
        fidl_fuchsia_ui_input3::KeyEventType,
        futures::StreamExt,
    };

    /// Handles |fidl_fuchsia_interaction_observation::AggregatorRequest|s.
    async fn handle_aggregator_request_stream(
        mut stream: interaction_observation::AggregatorRequestStream,
        expected_time: i64,
    ) {
        if let Some(request) = stream.next().await {
            match request {
                Ok(interaction_observation::AggregatorRequest::ReportDiscreteActivity {
                    event_time,
                    responder,
                }) => {
                    assert_eq!(event_time, expected_time);
                    responder.send().expect("failed to respond");
                }
                other => panic!("expected aggregator report request, but got {:?}", other),
            };
        } else {
            panic!("AggregatorRequestStream failed.");
        }
    }

    #[fuchsia::test(allow_stalls = false)]
    async fn handler_reports_keyboard_events() {
        // Set up fidl streams.
        let (aggregator_proxy, aggregator_request_stream) =
            fidl::endpoints::create_proxy_and_stream::<interaction_observation::AggregatorMarker>()
                .expect("Failed to create interaction observation Aggregator proxy and stream.");
        let keyboard_handler = KeyboardHandler::new_internal(Some(aggregator_proxy));

        let event_time = zx::Time::get_monotonic();
        let input_event = create_keyboard_event(
            Key::A,
            KeyEventType::Pressed,
            None,
            event_time,
            &InputDeviceDescriptor::Fake,
            None,
        );

        // Handle event.
        let handle_event_fut = keyboard_handler.handle_input_event(input_event);

        // Await all futures concurrently.
        let aggregator_fut =
            handle_aggregator_request_stream(aggregator_request_stream, event_time.into_nanos());
        let (handle_result, _) = futures::future::join(handle_event_fut, aggregator_fut).await;

        // Event remains unhandled.
        assert_matches!(
            handle_result.as_slice(),
            [input_device::InputEvent { handled: input_device::Handled::No, .. }]
        );
    }

    #[fuchsia::test(allow_stalls = false)]
    async fn handler_ignores_non_keyboard_events() {
        // Set up fidl streams.
        let (aggregator_proxy, mut aggregator_request_stream) =
            fidl::endpoints::create_proxy_and_stream::<interaction_observation::AggregatorMarker>()
                .expect("Failed to create interaction observation Aggregator proxy and stream.");
        let keyboard_handler = KeyboardHandler::new_internal(Some(aggregator_proxy));
        let input_event = create_fake_input_event(zx::Time::get_monotonic());

        assert_matches!(
            keyboard_handler.handle_input_event(input_event).await.as_slice(),
            [input_device::InputEvent { handled: input_device::Handled::No, .. }]
        );
        assert!(aggregator_request_stream.next().await.is_none());
    }
}
