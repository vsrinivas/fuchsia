// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::input_device,
    crate::input_device::InputDeviceBinding,
    async_trait::async_trait,
    failure::{self, format_err, Error},
    fidl_fuchsia_input_report::{InputDeviceProxy, InputReport},
    fuchsia_async as fasync,
    fuchsia_syslog::fx_log_info,
    futures::channel::mpsc::{Receiver, Sender},
    futures::StreamExt,
};

/// A [`MouseInputMessage`] represents a pointer event with a specified phase, and the buttons
/// involved in said phase. The supported phases for mice include Up, Down, and Move.
///
/// # Example
/// The following MouseInputMessage represents a movement of 40 units in the x axis and 20 units
/// in the y axis while hold the primary button down.
///
/// ```
/// let message = input_device::InputMessage::Mouse({
///     MouseInputMessage {
///         movement_x: 40,
///         movement_y: 20,
///         phase: fidl_fuchsia_ui_input::PointerEventPhase::Move,
///         buttons: 1,
///     }});
/// ```
#[derive(Copy, Clone)]
pub struct MouseInputMessage {
    /// The movement in the x axis.
    pub movement_x: i64,

    /// The movement in the y axis.
    pub movement_y: i64,

    /// The phase of the [`buttons`] associated with this input message.
    pub phase: fidl_fuchsia_ui_input::PointerEventPhase,

    /// The buttons relevant to this message represented as flipped bits.
    /// Example: If buttons 1 and 3 are pressed, [`buttons`] will be `0b101`.
    pub buttons: u32,
}

/// A [`MouseBinding`] represents a connection to a mouse input device.
///
/// The [`MouseBinding`] parses and exposes mouse descriptor properties (e.g., the range of
/// possible x values) for the device it is associated with. It also parses [`InputReport`]s
/// from the device, and sends them to clients via [`MouseBinding::input_message_stream()`].
///
/// # Example
/// ```
/// let mut mouse_device: MouseBinding = input_device::InputDeviceBinding::new().await?;
///
/// while let Some(report) = mouse_device.input_message_stream().next().await {}
/// ```
pub struct MouseBinding {
    /// The sender used to send available input reports.
    message_sender: Sender<input_device::InputMessage>,

    /// The receiving end of the input report channel. Clients use this indirectly via
    /// [`input_message_stream()`].
    message_receiver: Receiver<input_device::InputMessage>,

    /// Holds information about this device. Currently empty because no information is needed for
    /// the supported use cases.
    descriptor: MouseDescriptor,
}

#[derive(Copy, Clone)]
pub struct MouseDescriptor {}

#[async_trait]
impl input_device::InputDeviceBinding for MouseBinding {
    fn input_message_sender(&self) -> Sender<input_device::InputMessage> {
        self.message_sender.clone()
    }

    fn input_message_stream(&mut self) -> &mut Receiver<input_device::InputMessage> {
        return &mut self.message_receiver;
    }

    fn get_descriptor(&self) -> input_device::InputDescriptor {
        input_device::InputDescriptor::Mouse(self.descriptor)
    }

    fn process_reports(
        report: InputReport,
        previous_report: Option<InputReport>,
        _device_descriptor: &mut input_device::InputDescriptor,
        input_message_sender: &mut Sender<input_device::InputMessage>,
    ) -> Option<InputReport> {
        // Fail early if the new InputReport isn't a MouseReport
        let mouse_report: &fidl_fuchsia_input_report::MouseReport = match &report.mouse {
            Some(mouse) => mouse,
            None => {
                fx_log_info!("Not processing non-mouse InputReport.");
                return previous_report;
            }
        };

        // Get the previously and currently pressed buttons
        let mut previous_buttons: u32 = 0;
        if previous_report.is_some() {
            previous_buttons = buttons(&previous_report.unwrap().mouse);
        }
        let buttons: u32 = buttons(&report.mouse);

        // Send a Move InputMessage with the buttons that remained pressed buttons
        let moving_buttons: u32 = previous_buttons & buttons;
        send_mouse_message(
            mouse_report.movement_x.unwrap_or_default(),
            mouse_report.movement_y.unwrap_or_default(),
            fidl_fuchsia_ui_input::PointerEventPhase::Move,
            moving_buttons,
            input_message_sender,
        );

        // Send an Up InputMessage for released buttons
        let released_buttons: u32 = (previous_buttons ^ buttons) & previous_buttons;
        if released_buttons != 0 {
            send_mouse_message(
                mouse_report.movement_x.unwrap_or_default(),
                mouse_report.movement_y.unwrap_or_default(),
                fidl_fuchsia_ui_input::PointerEventPhase::Up,
                released_buttons,
                input_message_sender,
            );
        }

        // Send a Down InputMessage for pressed buttons
        let pressed_buttons: u32 = (buttons ^ previous_buttons) & buttons;
        if pressed_buttons != 0 {
            send_mouse_message(
                mouse_report.movement_x.unwrap_or_default(),
                mouse_report.movement_y.unwrap_or_default(),
                fidl_fuchsia_ui_input::PointerEventPhase::Down,
                pressed_buttons,
                input_message_sender,
            );
        }

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
        match device.get_descriptor().await?.mouse {
            Some(_) => {
                let (message_sender, message_receiver) =
                    futures::channel::mpsc::channel(input_device::INPUT_MESSAGE_BUFFER_SIZE);

                let descriptor: MouseDescriptor = MouseDescriptor {};

                Ok(MouseBinding { message_sender, message_receiver, descriptor })
            }
            descriptor => Err(format_err!("Mouse Descriptor failed to parse: \n {:?}", descriptor)),
        }
    }
}

/// Sends a MouseInputMessage over `sender`.
///
/// # Parameters
/// - `movement_x`: The movement in the x axis.
/// - `movement_y`: The movement in the y axis.
/// - `phase`: The phase of the [`buttons`] associated with the input message.
/// - `buttons`: The buttons relevant to the message represented as flipped bits.
/// - `sender`: The stream to send the MouseInputMessage to.
fn send_mouse_message(
    movement_x: i64,
    movement_y: i64,
    phase: fidl_fuchsia_ui_input::PointerEventPhase,
    buttons: u32,
    sender: &mut Sender<input_device::InputMessage>,
) {
    match sender.try_send(input_device::InputMessage::Mouse({
        MouseInputMessage { movement_x, movement_y, phase, buttons }
    })) {
        Err(e) => fx_log_info!("Failed to send MouseInputMessage with error: {:?}", e),
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
fn get_u32_from_vector(vector: &Vec<u8>) -> u32 {
    let mut bits: u32 = 0;
    for id in vector {
        if *id > 0 && *id <= fidl_fuchsia_input_report::MOUSE_MAX_NUM_BUTTONS as u8 {
            bits = ((1 as u32) << *id - 1) | bits;
        }
    }

    bits
}

/// Returns a u32 representation of the pressed buttons in the MouseReport.
///
/// # Parameters
/// - `mouse_report`: The report to get pressed buttons from.
fn buttons(mouse_report: &Option<fidl_fuchsia_input_report::MouseReport>) -> u32 {
    if mouse_report.is_some() {
        return match &mouse_report.as_ref().unwrap().pressed_buttons {
            Some(buttons) => get_u32_from_vector(&buttons),
            None => 0,
        };
    }

    0
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

/// Returns a stream of InputMessages from all mouse devices.
///
/// # Errors
/// If there was an error binding to any mouse.
pub async fn all_mouse_messages() -> Result<Receiver<input_device::InputMessage>, Error> {
    let bindings = all_mouse_bindings().await?;
    let (message_sender, message_receiver) =
        futures::channel::mpsc::channel(input_device::INPUT_MESSAGE_BUFFER_SIZE);

    for mut mouse in bindings {
        let mut sender = message_sender.clone();
        fasync::spawn(async move {
            while let Some(report) = mouse.input_message_stream().next().await {
                let _ = sender.try_send(report);
            }
        });
    }

    Ok(message_receiver)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn create_input_report(x: i64, y: i64, buttons: Vec<u8>) -> InputReport {
        InputReport {
            event_time: None,
            keyboard: None,
            mouse: Some(fidl_fuchsia_input_report::MouseReport {
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

    // Tests that the right u32 representation is returned from a vector of digits.
    #[test]
    fn get_u32_from_vector_test() {
        let bits = get_u32_from_vector(&vec![1, 3, 5]);
        assert_eq!(bits, 21 /* 0...00010101 */)
    }

    // Tests that the right u32 representation is returned from a vector of digits that includes 0.
    #[test]
    fn get_u32_with_0_in_vector() {
        let bits = get_u32_from_vector(&vec![0, 1, 3]);
        assert_eq!(bits, 5 /* 0...00000101 */)
    }

    // Tests that the right u32 representation is returned from an empty vector.
    #[test]
    fn get_u32_with_empty_vector() {
        let bits = get_u32_from_vector(&vec![]);
        assert_eq!(bits, 0 /* 0...00000000 */)
    }

    // Tests that the right u32 representation is returned from a vector containing std::u8::MAX.
    #[test]
    fn get_u32_with_u8_max_in_vector() {
        let bits = get_u32_from_vector(&vec![1, 3, std::u8::MAX]);
        assert_eq!(bits, 5 /* 0...00000101 */)
    }

    // Tests that the right u32 representation is returned from a vector containing the largest
    // button id possible.
    #[test]
    fn get_u32_with_max_mouse_buttons() {
        let bits = get_u32_from_vector(&vec![
            1,
            3,
            fidl_fuchsia_input_report::MOUSE_MAX_NUM_BUTTONS as u8,
        ]);
        assert_eq!(bits, 2147483653 /* 10...00000101 */)
    }

    // Tests that two InputMessages are sent for a button press
    #[fasync::run_singlethreaded(test)]
    async fn down_input_message() {
        const MOUSE_X: i64 = 0;
        const MOUSE_Y: i64 = 0;

        let previous_report = Some(create_input_report(0, 0, vec![]));
        let report = create_input_report(MOUSE_X, MOUSE_Y, vec![3]);
        let mut descriptor = input_device::InputDescriptor::Mouse(MouseDescriptor {});
        let (message_sender, mut message_receiver) = futures::channel::mpsc::channel(1);
        let _ = MouseBinding::process_reports(
            report,
            previous_report,
            &mut descriptor,
            &mut message_sender.clone(),
        );

        // First message received is a move
        if let Some(input_message) = message_receiver.next().await {
            match input_message {
                input_device::InputMessage::Mouse(mouse_message) => {
                    assert_eq!(mouse_message.movement_x, MOUSE_X);
                    assert_eq!(mouse_message.movement_y, MOUSE_Y);
                    assert_eq!(mouse_message.phase, fidl_fuchsia_ui_input::PointerEventPhase::Move);
                    assert_eq!(mouse_message.buttons, 0);
                }
                _ => assert!(false),
            }
        }

        // Second message received is a down
        if let Some(input_message) = message_receiver.next().await {
            match input_message {
                input_device::InputMessage::Mouse(mouse_message) => {
                    assert_eq!(mouse_message.movement_x, MOUSE_X);
                    assert_eq!(mouse_message.movement_y, MOUSE_Y);
                    assert_eq!(mouse_message.phase, fidl_fuchsia_ui_input::PointerEventPhase::Down);
                    assert_eq!(mouse_message.buttons, 4);
                }
                _ => assert!(false),
            }
        }
    }

    // Tests that two InputMessages are sent for a button release
    #[fasync::run_singlethreaded(test)]
    async fn up_input_message() {
        const MOUSE_X: i64 = 0;
        const MOUSE_Y: i64 = 0;

        let previous_report = Some(create_input_report(0, 0, vec![3]));
        let report = create_input_report(MOUSE_X, MOUSE_Y, vec![]);
        let mut descriptor = input_device::InputDescriptor::Mouse(MouseDescriptor {});
        let (message_sender, mut message_receiver) = futures::channel::mpsc::channel(1);
        let _ = MouseBinding::process_reports(
            report,
            previous_report,
            &mut descriptor,
            &mut message_sender.clone(),
        );

        // First message received is a move
        if let Some(input_message) = message_receiver.next().await {
            match input_message {
                input_device::InputMessage::Mouse(mouse_message) => {
                    assert_eq!(mouse_message.movement_x, MOUSE_X);
                    assert_eq!(mouse_message.movement_y, MOUSE_Y);
                    assert_eq!(mouse_message.phase, fidl_fuchsia_ui_input::PointerEventPhase::Move);
                    assert_eq!(mouse_message.buttons, 0);
                }
                _ => assert!(false),
            }
        }

        // Second message received is an up
        if let Some(input_message) = message_receiver.next().await {
            match input_message {
                input_device::InputMessage::Mouse(mouse_message) => {
                    assert_eq!(mouse_message.movement_x, MOUSE_X);
                    assert_eq!(mouse_message.movement_y, MOUSE_Y);
                    assert_eq!(mouse_message.phase, fidl_fuchsia_ui_input::PointerEventPhase::Up);
                    assert_eq!(mouse_message.buttons, 4);
                }
                _ => assert!(false),
            }
        }
    }

    // Tests an InputMessage representing a move
    #[fasync::run_singlethreaded(test)]
    async fn move_input_message() {
        const MOUSE_X: i64 = 30;
        const MOUSE_Y: i64 = 40;

        let previous_report = Some(create_input_report(10, 20, vec![3]));
        let report = create_input_report(MOUSE_X, MOUSE_Y, vec![3]);
        let mut descriptor = input_device::InputDescriptor::Mouse(MouseDescriptor {});
        let (message_sender, mut message_receiver) = futures::channel::mpsc::channel(1);
        let _ = MouseBinding::process_reports(
            report,
            previous_report,
            &mut descriptor,
            &mut message_sender.clone(),
        );

        if let Some(input_message) = message_receiver.next().await {
            match input_message {
                input_device::InputMessage::Mouse(mouse_message) => {
                    assert_eq!(mouse_message.movement_x, MOUSE_X);
                    assert_eq!(mouse_message.movement_y, MOUSE_Y);
                    assert_eq!(mouse_message.phase, fidl_fuchsia_ui_input::PointerEventPhase::Move);
                    assert_eq!(mouse_message.buttons, 4);
                }
                _ => assert!(false),
            }
        }
    }

    // Tests that three InputMessages are sent when there is a move, a button press, and
    // a button release
    #[fasync::run_singlethreaded(test)]
    async fn move_input_message_with_buttons() {
        const MOUSE_X: i64 = 30;
        const MOUSE_Y: i64 = 40;

        let previous_report = Some(create_input_report(10, 20, vec![1, 3]));
        let report = create_input_report(MOUSE_X, MOUSE_Y, vec![2, 3]);
        let mut descriptor = input_device::InputDescriptor::Mouse(MouseDescriptor {});

        // This channel's buffer needs to hold an extra message
        let (message_sender, mut message_receiver) = futures::channel::mpsc::channel(2);
        let _ = MouseBinding::process_reports(
            report,
            previous_report,
            &mut descriptor,
            &mut message_sender.clone(),
        );

        // First message received is a move
        if let Some(input_message) = message_receiver.next().await {
            match input_message {
                input_device::InputMessage::Mouse(mouse_message) => {
                    assert_eq!(mouse_message.movement_x, MOUSE_X);
                    assert_eq!(mouse_message.movement_y, MOUSE_Y);
                    assert_eq!(mouse_message.phase, fidl_fuchsia_ui_input::PointerEventPhase::Move);
                    assert_eq!(mouse_message.buttons, 4); // Button 3 was held
                }
                _ => assert!(false),
            }
        }

        // Second message received is an up
        if let Some(input_message) = message_receiver.next().await {
            match input_message {
                input_device::InputMessage::Mouse(mouse_message) => {
                    assert_eq!(mouse_message.movement_x, MOUSE_X);
                    assert_eq!(mouse_message.movement_y, MOUSE_Y);
                    assert_eq!(mouse_message.phase, fidl_fuchsia_ui_input::PointerEventPhase::Up);
                    assert_eq!(mouse_message.buttons, 1); // Button 1 was released
                }
                _ => assert!(false),
            }
        }

        // Third message received is a down
        if let Some(input_message) = message_receiver.next().await {
            match input_message {
                input_device::InputMessage::Mouse(mouse_message) => {
                    assert_eq!(mouse_message.movement_x, MOUSE_X);
                    assert_eq!(mouse_message.movement_y, MOUSE_Y);
                    assert_eq!(mouse_message.phase, fidl_fuchsia_ui_input::PointerEventPhase::Down);
                    assert_eq!(mouse_message.buttons, 2); // Button 2 was pressed
                }
                _ => assert!(false),
            }
        }
    }
}
