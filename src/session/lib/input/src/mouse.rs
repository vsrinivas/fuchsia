// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::input_device::{self, InputDeviceBinding},
    anyhow::{format_err, Error},
    async_trait::async_trait,
    fidl_fuchsia_input_report::{InputDeviceProxy, InputReport},
    fuchsia_async as fasync,
    fuchsia_syslog::fx_log_info,
    futures::channel::mpsc::{Receiver, Sender},
    futures::StreamExt,
    std::collections::HashSet,
    std::iter::FromIterator,
};

pub type MouseButton = u8;

/// A [`MouseEvent`] represents a pointer event with a specified phase, and the buttons
/// involved in said phase. The supported phases for mice include Up, Down, and Move.
///
/// # Example
/// The following MouseEvent represents a movement of 40 units in the x axis and 20 units
/// in the y axis while hold the primary button down.
///
/// ```
/// let mouse_device_event = input_device::InputDeviceEvent::Mouse({
///     MouseEvent {
///         movement_x: 40,
///         movement_y: 20,
///         phase: fidl_fuchsia_ui_input::PointerEventPhase::Move,
///         buttons: 1,
///     }});
/// ```
#[derive(Clone, Debug, PartialEq)]
pub struct MouseEvent {
    /// The movement in the x axis.
    pub movement_x: i64,

    /// The movement in the y axis.
    pub movement_y: i64,

    /// The phase of the [`buttons`] associated with this input event.
    pub phase: fidl_fuchsia_ui_input::PointerEventPhase,

    /// The buttons relevant to this event.
    pub buttons: HashSet<MouseButton>,
}

/// A [`MouseBinding`] represents a connection to a mouse input device.
///
/// The [`MouseBinding`] parses and exposes mouse descriptor properties (e.g., the range of
/// possible x values) for the device it is associated with. It also parses [`InputReport`]s
/// from the device, and sends them to clients via [`MouseBinding::input_event_stream()`].
///
/// # Example
/// ```
/// let mut mouse_device: MouseBinding = input_device::InputDeviceBinding::new().await?;
///
/// while let Some(report) = mouse_device.input_event_stream().next().await {}
/// ```
pub struct MouseBinding {
    /// The channel to stream InputEvents to.
    event_sender: Sender<input_device::InputEvent>,

    /// The receiving end of the input event channel. Clients use this indirectly via
    /// [`input_event_stream()`].
    event_receiver: Receiver<input_device::InputEvent>,

    /// Holds information about this device. Currently empty because no information is needed for
    /// the supported use cases.
    device_descriptor: MouseDeviceDescriptor,
}

#[derive(Copy, Clone, Debug, PartialEq)]
pub struct MouseDeviceDescriptor {
    /// The id of the connected mouse input device.
    pub device_id: u32,
}

#[async_trait]
impl input_device::InputDeviceBinding for MouseBinding {
    fn input_event_sender(&self) -> Sender<input_device::InputEvent> {
        self.event_sender.clone()
    }

    fn input_event_stream(&mut self) -> &mut Receiver<input_device::InputEvent> {
        return &mut self.event_receiver;
    }

    fn get_device_descriptor(&self) -> input_device::InputDeviceDescriptor {
        input_device::InputDeviceDescriptor::Mouse(self.device_descriptor.clone())
    }

    fn process_reports(
        report: InputReport,
        previous_report: Option<InputReport>,
        device_descriptor: &input_device::InputDeviceDescriptor,
        input_event_sender: &mut Sender<input_device::InputEvent>,
    ) -> Option<InputReport> {
        // Fail early if the new InputReport isn't a MouseInputReport
        let mouse_report: &fidl_fuchsia_input_report::MouseInputReport = match &report.mouse {
            Some(mouse) => mouse,
            None => {
                fx_log_info!("Not processing non-mouse InputReport.");
                return previous_report;
            }
        };

        let previous_buttons: HashSet<MouseButton> =
            buttons_from_optional_report(&previous_report.as_ref());
        let current_buttons: HashSet<MouseButton> = buttons_from_report(&report);

        // Down events are sent for the buttons which did not exist in the previous report.
        send_mouse_event(
            0,
            0,
            fidl_fuchsia_ui_input::PointerEventPhase::Down,
            current_buttons.difference(&previous_buttons).cloned().collect(),
            device_descriptor,
            input_event_sender,
        );

        // Move events are sent for both previous and current buttons.
        send_mouse_event(
            mouse_report.movement_x.unwrap_or_default(),
            mouse_report.movement_y.unwrap_or_default(),
            fidl_fuchsia_ui_input::PointerEventPhase::Move,
            current_buttons.union(&previous_buttons).cloned().collect(),
            device_descriptor,
            input_event_sender,
        );

        // Up events are sent for previous buttons that are no longer present in the current buttons.
        send_mouse_event(
            0,
            0,
            fidl_fuchsia_ui_input::PointerEventPhase::Up,
            previous_buttons.difference(&current_buttons).cloned().collect(),
            device_descriptor,
            input_event_sender,
        );

        Some(report)
    }

    async fn any_input_device() -> Result<InputDeviceProxy, Error> {
        let mut devices = Self::all_devices().await?;
        devices.pop().ok_or(format_err!("Couldn't find a default mouse."))
    }

    async fn all_devices() -> Result<Vec<InputDeviceProxy>, Error> {
        input_device::all_devices(input_device::InputDeviceType::Mouse).await
    }

    async fn bind_device(device: &InputDeviceProxy) -> Result<Self, Error> {
        let device_descriptor: fidl_fuchsia_input_report::DeviceDescriptor =
            device.get_descriptor().await?;
        match device_descriptor.mouse {
            Some(_) => {
                let (event_sender, event_receiver) =
                    futures::channel::mpsc::channel(input_device::INPUT_EVENT_BUFFER_SIZE);

                let device_id = match device_descriptor.device_info {
                    Some(info) => info.product_id,
                    None => {
                        return Err(format_err!("Mouse Descriptor doesn't contain a product id."))
                    }
                };

                let device_descriptor: MouseDeviceDescriptor = MouseDeviceDescriptor { device_id };

                Ok(MouseBinding { event_sender, event_receiver, device_descriptor })
            }
            descriptor => Err(format_err!("Mouse Descriptor failed to parse: \n {:?}", descriptor)),
        }
    }
}

/// Sends an InputEvent over `sender`.
///
/// When no buttons are present, only [`fidl_fuchsia_ui_input::PointerEventPhase::Move`] events will
/// be sent.
///
/// # Parameters
/// - `movement_x`: The movement in the x axis.
/// - `movement_y`: The movement in the y axis.
/// - `phase`: The phase of the [`buttons`] associated with the input event.
/// - `buttons`: The buttons relevant to the event.
/// - `sender`: The stream to send the MouseEvent to.
fn send_mouse_event(
    movement_x: i64,
    movement_y: i64,
    phase: fidl_fuchsia_ui_input::PointerEventPhase,
    buttons: HashSet<MouseButton>,
    device_descriptor: &input_device::InputDeviceDescriptor,
    sender: &mut Sender<input_device::InputEvent>,
) {
    // Only send move events when there are no buttons pressed.
    if phase != fidl_fuchsia_ui_input::PointerEventPhase::Move && buttons.is_empty() {
        return;
    }

    // Don't send move events when there is no movement.
    if phase == fidl_fuchsia_ui_input::PointerEventPhase::Move && movement_x == 0 && movement_y == 0
    {
        return;
    }

    match sender.try_send(input_device::InputEvent {
        device_event: input_device::InputDeviceEvent::Mouse(MouseEvent {
            movement_x,
            movement_y,
            phase,
            buttons,
        }),
        device_descriptor: device_descriptor.clone(),
    }) {
        Err(e) => fx_log_info!("Failed to send MouseEvent with error: {:?}", e),
        _ => {}
    }
}

/// Returns a u32 representation of `vector`, where each u8 of `vector` is an id of a button and
/// indicates the position of a bit to set.
///
/// This supports vectors with numbers from 1 - fidl_fuchsia_input_report::MOUSE_MAX_NUM_BUTTONS.
///
/// # Parameters
/// - `vector`: The vector containing the position of bits to be set.
///
/// # Example
/// ```
/// let bits = get_u32_from_vector(&vec![1, 3, 5]);
/// assert_eq!(bits, 21 /* ...00010101 */)
/// ```
pub fn get_u32_from_buttons(buttons: &HashSet<MouseButton>) -> u32 {
    let mut bits: u32 = 0;
    for button in buttons {
        if *button > 0 && *button <= fidl_fuchsia_input_report::MOUSE_MAX_NUM_BUTTONS as u8 {
            bits = ((1 as u32) << *button - 1) | bits;
        }
    }

    bits
}

/// Returns the set of pressed buttons present in the given input report.
///
/// # Parameters
/// - `report`: The input report to parse the mouse buttons from.
fn buttons_from_report(
    input_report: &fidl_fuchsia_input_report::InputReport,
) -> HashSet<MouseButton> {
    buttons_from_optional_report(&Some(input_report))
}

/// Returns the set of pressed buttons present in the given input report.
///
/// # Parameters
/// - `report`: The input report to parse the mouse buttons from.
fn buttons_from_optional_report(
    input_report: &Option<&fidl_fuchsia_input_report::InputReport>,
) -> HashSet<MouseButton> {
    input_report
        .as_ref()
        .and_then(|unwrapped_report| unwrapped_report.mouse.as_ref())
        .and_then(|mouse_report| match &mouse_report.pressed_buttons {
            Some(buttons) => Some(HashSet::from_iter(buttons.iter().cloned())),
            None => None,
        })
        .unwrap_or_default()
}

/// Returns a vector of [`MouseBindings`] for all currently connected mice.
///
/// # Errors
/// If there was an error binding to any mouse.
async fn all_mouse_bindings() -> Result<Vec<MouseBinding>, Error> {
    let device_proxies = input_device::all_devices(input_device::InputDeviceType::Mouse).await?;
    let mut device_bindings: Vec<MouseBinding> = vec![];

    for device_proxy in device_proxies {
        let device_binding: MouseBinding =
            input_device::InputDeviceBinding::new(device_proxy).await?;
        device_bindings.push(device_binding);
    }

    Ok(device_bindings)
}

/// Returns a stream of InputEvents from all mouse devices.
///
/// # Errors
/// If there was an error binding to any mouse.
pub async fn all_mouse_events() -> Result<Receiver<input_device::InputEvent>, Error> {
    let bindings = all_mouse_bindings().await?;
    let (event_sender, event_receiver) =
        futures::channel::mpsc::channel(input_device::INPUT_EVENT_BUFFER_SIZE);

    for mut mouse in bindings {
        let mut sender = event_sender.clone();
        fasync::spawn(async move {
            while let Some(input_event) = mouse.input_event_stream().next().await {
                let _ = sender.try_send(input_event);
            }
        });
    }

    Ok(event_receiver)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::testing_utilities;

    // Tests that the right u32 representation is returned from a vector of digits.
    #[test]
    fn get_u32_from_buttons_test() {
        let bits = get_u32_from_buttons(&HashSet::from_iter(vec![1, 3, 5].into_iter()));
        assert_eq!(bits, 21 /* 0...00010101 */)
    }

    // Tests that the right u32 representation is returned from a vector of digits that includes 0.
    #[test]
    fn get_u32_with_0_in_vector() {
        let bits = get_u32_from_buttons(&HashSet::from_iter(vec![0, 1, 3].into_iter()));
        assert_eq!(bits, 5 /* 0...00000101 */)
    }

    // Tests that the right u32 representation is returned from an empty vector.
    #[test]
    fn get_u32_with_empty_vector() {
        let bits = get_u32_from_buttons(&HashSet::new());
        assert_eq!(bits, 0 /* 0...00000000 */)
    }

    // Tests that the right u32 representation is returned from a vector containing std::u8::MAX.
    #[test]
    fn get_u32_with_u8_max_in_vector() {
        let bits = get_u32_from_buttons(&HashSet::from_iter(vec![1, 3, std::u8::MAX].into_iter()));
        assert_eq!(bits, 5 /* 0...00000101 */)
    }

    // Tests that the right u32 representation is returned from a vector containing the largest
    // button id possible.
    #[test]
    fn get_u32_with_max_mouse_buttons() {
        let bits = get_u32_from_buttons(&HashSet::from_iter(
            vec![1, 3, fidl_fuchsia_input_report::MOUSE_MAX_NUM_BUTTONS as MouseButton].into_iter(),
        ));
        assert_eq!(bits, 2147483653 /* 10...00000101 */)
    }

    /// Tests that a report containing no buttons but with movement generates a move event.
    #[fasync::run_singlethreaded(test)]
    async fn movement_without_button() {
        const MOVEMENT_X: i64 = 10;
        const MOVEMENT_Y: i64 = 16;
        let first_report =
            testing_utilities::create_mouse_input_report(MOVEMENT_X, MOVEMENT_Y, vec![]);
        let descriptor =
            input_device::InputDeviceDescriptor::Mouse(MouseDeviceDescriptor { device_id: 1 });

        let input_reports = vec![first_report];
        let expected_events = vec![testing_utilities::create_mouse_event(
            MOVEMENT_X,
            MOVEMENT_Y,
            fidl_fuchsia_ui_input::PointerEventPhase::Move,
            HashSet::new(),
            &descriptor,
        )];

        assert_input_report_sequence_generates_events!(
            input_reports: input_reports,
            expected_events: expected_events,
            device_descriptor: descriptor,
            device_type: MouseBinding,
        );
    }

    /// Tests that a report containing a new mouse button generates a down event.
    #[fasync::run_singlethreaded(test)]
    async fn down_without_movement() {
        let mouse_button: MouseButton = 3;
        let first_report = testing_utilities::create_mouse_input_report(0, 0, vec![mouse_button]);
        let descriptor =
            input_device::InputDeviceDescriptor::Mouse(MouseDeviceDescriptor { device_id: 1 });

        let input_reports = vec![first_report];
        let expected_events = vec![testing_utilities::create_mouse_event(
            0,
            0,
            fidl_fuchsia_ui_input::PointerEventPhase::Down,
            HashSet::from_iter(vec![mouse_button].into_iter()),
            &descriptor,
        )];

        assert_input_report_sequence_generates_events!(
            input_reports: input_reports,
            expected_events: expected_events,
            device_descriptor: descriptor,
            device_type: MouseBinding,
        );
    }

    /// Tests that a report containing a new mouse button with movement generates a down event and a
    /// move event.
    #[fasync::run_singlethreaded(test)]
    async fn down_with_movement() {
        const MOVEMENT_X: i64 = 10;
        const MOVEMENT_Y: i64 = 16;
        let mouse_button: MouseButton = 3;
        let first_report = testing_utilities::create_mouse_input_report(
            MOVEMENT_X,
            MOVEMENT_Y,
            vec![mouse_button],
        );
        let descriptor =
            input_device::InputDeviceDescriptor::Mouse(MouseDeviceDescriptor { device_id: 1 });

        let input_reports = vec![first_report];
        let expected_events = vec![
            testing_utilities::create_mouse_event(
                0,
                0,
                fidl_fuchsia_ui_input::PointerEventPhase::Down,
                HashSet::from_iter(vec![mouse_button].into_iter()),
                &descriptor,
            ),
            testing_utilities::create_mouse_event(
                MOVEMENT_X,
                MOVEMENT_Y,
                fidl_fuchsia_ui_input::PointerEventPhase::Move,
                HashSet::from_iter(vec![mouse_button].into_iter()),
                &descriptor,
            ),
        ];

        assert_input_report_sequence_generates_events!(
            input_reports: input_reports,
            expected_events: expected_events,
            device_descriptor: descriptor,
            device_type: MouseBinding,
        );
    }

    /// Tests that a press and release of a mouse button without movement generates a down and up event.
    #[fasync::run_singlethreaded(test)]
    async fn down_up() {
        let button = 1;
        let first_report = testing_utilities::create_mouse_input_report(0, 0, vec![button]);
        let second_report = testing_utilities::create_mouse_input_report(0, 0, vec![]);
        let descriptor =
            input_device::InputDeviceDescriptor::Mouse(MouseDeviceDescriptor { device_id: 1 });

        let input_reports = vec![first_report, second_report];
        let expected_events = vec![
            testing_utilities::create_mouse_event(
                0,
                0,
                fidl_fuchsia_ui_input::PointerEventPhase::Down,
                HashSet::from_iter(vec![button].into_iter()),
                &descriptor,
            ),
            testing_utilities::create_mouse_event(
                0,
                0,
                fidl_fuchsia_ui_input::PointerEventPhase::Up,
                HashSet::from_iter(vec![button].into_iter()),
                &descriptor,
            ),
        ];

        assert_input_report_sequence_generates_events!(
            input_reports: input_reports,
            expected_events: expected_events,
            device_descriptor: descriptor,
            device_type: MouseBinding,
        );
    }

    /// Tests that a press and release of a mouse button with movement generates down, move, and up events.
    #[fasync::run_singlethreaded(test)]
    async fn down_up_with_movement() {
        const MOVEMENT_X: i64 = 10;
        const MOVEMENT_Y: i64 = 16;
        let button = 1;

        let first_report = testing_utilities::create_mouse_input_report(0, 0, vec![button]);
        let second_report =
            testing_utilities::create_mouse_input_report(MOVEMENT_X, MOVEMENT_Y, vec![]);
        let descriptor =
            input_device::InputDeviceDescriptor::Mouse(MouseDeviceDescriptor { device_id: 1 });

        let input_reports = vec![first_report, second_report];
        let expected_events = vec![
            testing_utilities::create_mouse_event(
                0,
                0,
                fidl_fuchsia_ui_input::PointerEventPhase::Down,
                HashSet::from_iter(vec![button].into_iter()),
                &descriptor,
            ),
            testing_utilities::create_mouse_event(
                MOVEMENT_X,
                MOVEMENT_Y,
                fidl_fuchsia_ui_input::PointerEventPhase::Move,
                HashSet::from_iter(vec![button].into_iter()),
                &descriptor,
            ),
            testing_utilities::create_mouse_event(
                0,
                0,
                fidl_fuchsia_ui_input::PointerEventPhase::Up,
                HashSet::from_iter(vec![button].into_iter()),
                &descriptor,
            ),
        ];

        assert_input_report_sequence_generates_events!(
            input_reports: input_reports,
            expected_events: expected_events,
            device_descriptor: descriptor,
            device_type: MouseBinding,
        );
    }

    /// Tests that a press, move, and release of a button generates down, move, and up events.
    /// This specifically tests the separate input report containing the movement, instead of sending
    /// the movement as part of the down or up events.
    #[fasync::run_singlethreaded(test)]
    async fn down_move_up() {
        const MOVEMENT_X: i64 = 10;
        const MOVEMENT_Y: i64 = 16;
        let button = 1;

        let first_report = testing_utilities::create_mouse_input_report(0, 0, vec![button]);
        let second_report =
            testing_utilities::create_mouse_input_report(MOVEMENT_X, MOVEMENT_Y, vec![button]);
        let third_report = testing_utilities::create_mouse_input_report(0, 0, vec![]);
        let descriptor =
            input_device::InputDeviceDescriptor::Mouse(MouseDeviceDescriptor { device_id: 1 });

        let input_reports = vec![first_report, second_report, third_report];
        let expected_events = vec![
            testing_utilities::create_mouse_event(
                0,
                0,
                fidl_fuchsia_ui_input::PointerEventPhase::Down,
                HashSet::from_iter(vec![button].into_iter()),
                &descriptor,
            ),
            testing_utilities::create_mouse_event(
                MOVEMENT_X,
                MOVEMENT_Y,
                fidl_fuchsia_ui_input::PointerEventPhase::Move,
                HashSet::from_iter(vec![button].into_iter()),
                &descriptor,
            ),
            testing_utilities::create_mouse_event(
                0,
                0,
                fidl_fuchsia_ui_input::PointerEventPhase::Up,
                HashSet::from_iter(vec![button].into_iter()),
                &descriptor,
            ),
        ];

        assert_input_report_sequence_generates_events!(
            input_reports: input_reports,
            expected_events: expected_events,
            device_descriptor: descriptor,
            device_type: MouseBinding,
        );
    }
}
