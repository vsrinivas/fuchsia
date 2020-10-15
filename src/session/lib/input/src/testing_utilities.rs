// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[cfg(test)]
use {
    crate::utils::Position,
    crate::{input_device, keyboard, mouse, touch},
    fidl_fuchsia_input_report as fidl_input_report, fidl_fuchsia_ui_input as fidl_ui_input,
    fidl_fuchsia_ui_input2 as fidl_ui_input2, fuchsia_zircon as zx,
    maplit::hashmap,
    std::collections::HashMap,
    std::collections::HashSet,
};

/// Returns the current time as an i64 for InputReports and input_device::EventTime for InputEvents.
#[cfg(test)]
pub fn event_times() -> (i64, input_device::EventTime) {
    let event_time = zx::Time::get_monotonic().into_nanos();
    (event_time, event_time as input_device::EventTime)
}

/// Creates a [`fidl_input_report::InputReport`] with a keyboard report.
///
/// # Parameters
/// -`pressed_keys`: The keys that will be added to the returned input report.
#[cfg(test)]
pub fn create_keyboard_input_report(
    pressed_keys: Vec<fidl_ui_input2::Key>,
    event_time: i64,
) -> fidl_input_report::InputReport {
    fidl_input_report::InputReport {
        event_time: Some(event_time),
        keyboard: Some(fidl_input_report::KeyboardInputReport {
            pressed_keys: Some(pressed_keys),
            pressed_keys3: None,
        }),
        mouse: None,
        touch: None,
        sensor: None,
        consumer_control: None,
        trace_id: None,
    }
}

/// Creates a [`keyboard::KeyboardEvent`] with the provided keys.
///
/// # Parameters
/// - `pressed_keys`: The keys which are to be included as pressed.
/// - `released_keys`: The keys which are to be included as released.
/// - `modifiers`: The modifiers that are to be included as pressed.
/// - `device_descriptor`: The device descriptor to add to the event.
#[cfg(test)]
pub fn create_keyboard_event(
    pressed_keys: Vec<fidl_ui_input2::Key>,
    released_keys: Vec<fidl_ui_input2::Key>,
    modifiers: Option<fidl_ui_input2::Modifiers>,
    event_time: input_device::EventTime,
    device_descriptor: &input_device::InputDeviceDescriptor,
) -> input_device::InputEvent {
    input_device::InputEvent {
        device_event: input_device::InputDeviceEvent::Keyboard(keyboard::KeyboardEvent {
            keys: hashmap! {
                fidl_ui_input2::KeyEventPhase::Pressed => pressed_keys,
                fidl_ui_input2::KeyEventPhase::Released => released_keys
            },
            modifiers,
        }),
        device_descriptor: device_descriptor.clone(),
        event_time,
    }
}

/// Creates a [`fidl_input_report::InputReport`] with a mouse report.
///
/// # Parameters
/// - `location`: The movement or position of the mouse report, in input device coordinates.
///     [`MouseLocation::Relative`] represents movement, and
///     [`MouseLocation::Absolute`] represents position.
/// - `buttons`: The buttons to report as pressed in the mouse report.
/// - `event_time`: The time of event.
#[cfg(test)]
pub fn create_mouse_input_report(
    location: mouse::MouseLocation,
    buttons: Vec<u8>,
    event_time: i64,
) -> fidl_input_report::InputReport {
    fidl_input_report::InputReport {
        event_time: Some(event_time),
        keyboard: None,
        mouse: Some(fidl_input_report::MouseInputReport {
            movement_x: match location {
                mouse::MouseLocation::Relative(Position { x, .. }) => Some(x as i64),
                _ => None,
            },
            movement_y: match location {
                mouse::MouseLocation::Relative(Position { y, .. }) => Some(y as i64),
                _ => None,
            },
            position_x: match location {
                mouse::MouseLocation::Absolute(Position { x, .. }) => Some(x as i64),
                _ => None,
            },
            position_y: match location {
                mouse::MouseLocation::Absolute(Position { y, .. }) => Some(y as i64),
                _ => None,
            },
            scroll_h: None,
            scroll_v: None,
            pressed_buttons: Some(buttons),
        }),
        touch: None,
        sensor: None,
        consumer_control: None,
        trace_id: None,
    }
}

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

/// Creates a [`fidl_input_report::InputReport`] with a touch report.
///
/// # Parameters
/// - `contacts`: The contacts in the touch report.
/// - `event_time`: The time of event.
#[cfg(test)]
pub fn create_touch_input_report(
    contacts: Vec<fidl_input_report::ContactInputReport>,
    event_time: i64,
) -> fidl_input_report::InputReport {
    fidl_input_report::InputReport {
        event_time: Some(event_time),
        keyboard: None,
        mouse: None,
        touch: Some(fidl_input_report::TouchInputReport {
            contacts: Some(contacts),
            pressed_buttons: None,
        }),
        sensor: None,
        consumer_control: None,
        trace_id: None,
    }
}

#[cfg(test)]
pub fn create_touch_contact(id: u32, position: Position) -> touch::TouchContact {
    touch::TouchContact { id, position, pressure: None, contact_size: None }
}

/// Creates a [`touch::TouchEvent`] with the provided parameters.
///
/// # Parameters
/// - `contacts`: The contacts in the touch report.
/// - `event_time`: The time of event.
/// - `device_descriptor`: The device descriptor to add to the event.
#[cfg(test)]
pub fn create_touch_event(
    mut contacts: HashMap<fidl_ui_input::PointerEventPhase, Vec<touch::TouchContact>>,
    event_time: input_device::EventTime,
    device_descriptor: &input_device::InputDeviceDescriptor,
) -> input_device::InputEvent {
    contacts.entry(fidl_ui_input::PointerEventPhase::Add).or_insert(vec![]);
    contacts.entry(fidl_ui_input::PointerEventPhase::Down).or_insert(vec![]);
    contacts.entry(fidl_ui_input::PointerEventPhase::Move).or_insert(vec![]);
    contacts.entry(fidl_ui_input::PointerEventPhase::Up).or_insert(vec![]);
    contacts.entry(fidl_ui_input::PointerEventPhase::Remove).or_insert(vec![]);

    input_device::InputEvent {
        device_event: input_device::InputDeviceEvent::Touch(touch::TouchEvent { contacts }),
        device_descriptor: device_descriptor.clone(),
        event_time: event_time,
    }
}

/// Asserts that the given sequence of input reports generates the provided input events
/// when the reports are processed by the given device type.
#[cfg(test)]
#[macro_export]
macro_rules! assert_input_report_sequence_generates_events {
    (
        // The input reports to process.
        input_reports: $input_reports:expr,
        // The events which are expected.
        expected_events: $expected_events:expr,
        // The descriptor for the device that is sent to the input processor.
        device_descriptor: $device_descriptor:expr,
        // The type of device generating the events.
        device_type: $DeviceType:ty,
    ) => {
        let mut previous_report: Option<fidl_fuchsia_input_report::InputReport> = None;
        let (event_sender, mut event_receiver) = futures::channel::mpsc::channel(std::cmp::max(
            $input_reports.len(),
            $expected_events.len(),
        ));

        // Send all the reports prior to verifying the received events.
        for report in $input_reports {
            previous_report = <$DeviceType>::process_reports(
                report,
                previous_report,
                &$device_descriptor,
                &mut event_sender.clone(),
            );
        }

        for expected_event in $expected_events {
            let input_event = event_receiver.next().await;
            match input_event {
                Some(received_event) => assert_eq!(expected_event, received_event),
                _ => assert!(false),
            };
        }
    };
}

/// Asserts that the given sequence of input events generates the provided Scenic commands when the
/// events are processed by the given input handler.
#[cfg(test)]
#[macro_export]
macro_rules! assert_input_event_sequence_generates_scenic_events {
    (
        // The input handler that will handle input events.
        input_handler: $input_handler:expr,
        // The InputEvents to handle.
        input_events: $input_events:expr,
        // The commands the Scenic session should receive.
        expected_commands: $expected_commands:expr,
        // The Scenic session request stream.
        scenic_session_request_stream: $scenic_session_request_stream:expr,
        // A function to validate the Scenic commands.
        assert_command: $assert_command:expr,
    ) => {
        for input_event in $input_events {
            let events: Vec<input_device::InputEvent> =
                $input_handler.handle_input_event(input_event).await;
            assert_eq!(events.len(), 0);
        }

        let mut expected_command_iter = $expected_commands.into_iter().peekable();
        while let Some(request) = $scenic_session_request_stream.next().await {
            match request {
                Ok(fidl_ui_scenic::SessionRequest::Enqueue { cmds, control_handle: _ }) => {
                    let mut command_iter = cmds.into_iter().peekable();
                    while let Some(command) = command_iter.next() {
                        let expected_command = expected_command_iter.next().unwrap();
                        $assert_command(command, expected_command);

                        // All the expected events have been received, so make sure no more events
                        // are present before returning.
                        if expected_command_iter.peek().is_none() {
                            assert!(command_iter.peek().is_none());
                            return;
                        }
                    }
                }
                _ => {
                    assert!(false);
                }
            }
        }
    };
}
