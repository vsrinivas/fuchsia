// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[cfg(test)]
use {
    crate::{input_device, keyboard, mouse},
    fidl_fuchsia_input_report as fidl_input_report, fidl_fuchsia_ui_input as fidl_ui_input,
    fidl_fuchsia_ui_input2 as fidl_ui_input2,
    maplit::hashmap,
    std::collections::HashSet,
};

/// Creates a [`fidl_input_report::InputReport`] with a keyboard report.
///
/// # Parameters
/// -`pressed_keys`: The keys that will be added to the returned input report.
#[cfg(test)]
pub fn create_keyboard_input_report(
    pressed_keys: Vec<fidl_ui_input2::Key>,
) -> fidl_input_report::InputReport {
    fidl_input_report::InputReport {
        event_time: None,
        keyboard: Some(fidl_input_report::KeyboardInputReport { pressed_keys: Some(pressed_keys) }),
        mouse: None,
        touch: None,
        sensor: None,
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
    device_descriptor: &input_device::InputDeviceDescriptor,
) -> input_device::InputEvent {
    input_device::InputEvent {
        device_event: input_device::InputDeviceEvent::Keyboard(keyboard::KeyboardEvent {
            keys: hashmap! {
                fidl_ui_input2::KeyEventPhase::Pressed => pressed_keys,
                fidl_ui_input2::KeyEventPhase::Released => released_keys
            },
            modifiers: modifiers,
        }),
        device_descriptor: device_descriptor.clone(),
    }
}

/// Creates a [`fidl_input_report::InputReport`] with a mouse report.
///
/// # Parameters
/// - `x`: The x location of the mouse report, in input device coordinates.
/// - `y`: The y location of the mouse report, in input device coordinates.
/// - `buttons`: The buttons to report as pressed in the mouse report.
#[cfg(test)]
pub fn create_mouse_input_report(
    x: i64,
    y: i64,
    buttons: Vec<u8>,
) -> fidl_input_report::InputReport {
    fidl_input_report::InputReport {
        event_time: None,
        keyboard: None,
        mouse: Some(fidl_input_report::MouseInputReport {
            movement_x: Some(x),
            movement_y: Some(y),
            scroll_h: None,
            scroll_v: None,
            pressed_buttons: Some(buttons),
        }),
        touch: None,
        sensor: None,
        trace_id: None,
    }
}

/// Creates a [`mouse::MouseEvent`] with the provided parameters.
///
/// # Parameters
/// - `movement_x`: The x-movement to report in the event.
/// - `movement_y`: The y-movement to report in the event.
/// - `phase`: The phase of the buttons in the event.
/// - `buttons`: The buttons to report in the event.
/// - `device_descriptor`: The device descriptor to add to the event.
#[cfg(test)]
pub fn create_mouse_event(
    movement_x: i64,
    movement_y: i64,
    phase: fidl_ui_input::PointerEventPhase,
    buttons: HashSet<mouse::MouseButton>,
    device_descriptor: &input_device::InputDeviceDescriptor,
) -> input_device::InputEvent {
    input_device::InputEvent {
        device_event: input_device::InputDeviceEvent::Mouse(mouse::MouseEvent {
            movement_x,
            movement_y,
            phase,
            buttons,
        }),
        device_descriptor: device_descriptor.clone(),
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
        let (event_sender, mut event_receiver) =
            futures::channel::mpsc::channel($input_reports.len());

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
