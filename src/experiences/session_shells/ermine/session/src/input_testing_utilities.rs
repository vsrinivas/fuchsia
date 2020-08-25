// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[cfg(test)]
use {
    fidl_fuchsia_ui_input as fidl_ui_input, input::input_device, input::mouse,
    std::collections::HashSet,
};

/// Creates a [`mouse::MouseEvent`] with the provided parameters.
///
/// # Parameters
/// - `location`: The mouse location to report in the event.
/// - `phase`: The phase of the buttons in the event.
/// - `buttons`: The buttons to report in the event.
/// - `event_time`: The time of event.
/// - `device_descriptor`: The device descriptor to add to the event.
#[cfg(test)]
pub fn create_mouse_event(
    location: mouse::MouseLocation,
    phase: fidl_ui_input::PointerEventPhase,
    buttons: HashSet<mouse::MouseButton>,
    event_time: input_device::EventTime,
    device_descriptor: &input_device::InputDeviceDescriptor,
) -> input_device::InputEvent {
    input_device::InputEvent {
        device_event: input_device::InputDeviceEvent::Mouse(mouse::MouseEvent::new(
            location, phase, buttons,
        )),
        device_descriptor: device_descriptor.clone(),
        event_time,
    }
}

/// Asserts that the given sequence of input events sends events to
/// PointerCaptureListenerHack when the input events are processed by
/// the given input handler.
#[cfg(test)]
#[macro_export]
macro_rules! assert_input_event_sequence_generates_pointer_hack_events {
    (
        // The input handler that will handle input events.
        input_handler: $input_handler:expr,
        // The InputEvents to handle.
        input_events: $input_events:expr,
        // The PointerEvents the listener should receive.
        expected_events: $expected_events:expr,
        // The PointerCaptureListenerHack request stream.
        listener_request_stream: $listener_request_stream:expr,
        // A function to validate the events.
        assert_event: $assert_event:expr,
    ) => {
        for input_event in $input_events {
            $input_handler.handle_input_event(input_event).await;
        }

        let mut expected_event_iter = $expected_events.into_iter().peekable();
        while let Some(request) = $listener_request_stream.next().await {
            match request {
                Ok(fidl_ui_policy::PointerCaptureListenerHackRequest::OnPointerEvent {
                    event,
                    control_handle: _,
                }) => {
                    // There should be an expected event if the listener received one.
                    assert!(expected_event_iter.peek().is_some());

                    let expected_event = expected_event_iter.next().unwrap();
                    $assert_event(event, expected_event);

                    // All the expected events have been received.
                    if expected_event_iter.peek().is_none() {
                        return;
                    }
                }
                _ => {
                    assert!(false);
                }
            }
        }
    };
}
