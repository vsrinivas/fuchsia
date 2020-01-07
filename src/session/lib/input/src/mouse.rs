// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::input_device::{self, InputDeviceBinding, InputDeviceDescriptor},
    anyhow::{format_err, Error},
    async_trait::async_trait,
    fidl_fuchsia_input_report::{InputDeviceProxy, InputReport},
    fuchsia_async as fasync,
    fuchsia_syslog::fx_log_info,
    futures::channel::mpsc::{Receiver, Sender},
    futures::StreamExt,
};

/// A [`MouseEventDescriptor`] represents a pointer event with a specified phase, and the buttons
/// involved in said phase. The supported phases for mice include Up, Down, and Move.
///
/// # Example
/// The following MouseEventDescriptor represents a movement of 40 units in the x axis and 20 units
/// in the y axis while hold the primary button down.
///
/// ```
/// let mouse_event_descriptor = input_device::InputEventDescriptor::Mouse({
///     MouseEventDescriptor {
///         movement_x: 40,
///         movement_y: 20,
///         phase: fidl_fuchsia_ui_input::PointerEventPhase::Move,
///         buttons: 1,
///     }});
/// ```
#[derive(Copy, Clone)]
pub struct MouseEventDescriptor {
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

#[derive(Clone)]
#[allow(dead_code)]
pub struct MouseDeviceDescriptor {
    /// The id of the connected mouse input device.
    device_id: u32,
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
        device_descriptor: input_device::InputDeviceDescriptor,
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

        // Get the previously and currently pressed buttons
        let mut previous_buttons: u32 = 0;
        if previous_report.is_some() {
            previous_buttons = buttons(&previous_report.unwrap().mouse);
        }
        let buttons: u32 = buttons(&report.mouse);

        // Send a Move InputEventDescriptor with the buttons that remained pressed buttons
        let moving_buttons: u32 = previous_buttons & buttons;
        send_mouse_event(
            mouse_report.movement_x.unwrap_or_default(),
            mouse_report.movement_y.unwrap_or_default(),
            fidl_fuchsia_ui_input::PointerEventPhase::Move,
            moving_buttons,
            device_descriptor.clone(),
            input_event_sender,
        );

        // Send an Up InputEventDescriptor for released buttons
        let released_buttons: u32 = (previous_buttons ^ buttons) & previous_buttons;
        if released_buttons != 0 {
            send_mouse_event(
                mouse_report.movement_x.unwrap_or_default(),
                mouse_report.movement_y.unwrap_or_default(),
                fidl_fuchsia_ui_input::PointerEventPhase::Up,
                released_buttons,
                device_descriptor.clone(),
                input_event_sender,
            );
        }

        // Send a Down InputEventDescriptor for pressed buttons
        let pressed_buttons: u32 = (buttons ^ previous_buttons) & buttons;
        if pressed_buttons != 0 {
            send_mouse_event(
                mouse_report.movement_x.unwrap_or_default(),
                mouse_report.movement_y.unwrap_or_default(),
                fidl_fuchsia_ui_input::PointerEventPhase::Down,
                pressed_buttons,
                device_descriptor.clone(),
                input_event_sender,
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
/// # Parameters
/// - `movement_x`: The movement in the x axis.
/// - `movement_y`: The movement in the y axis.
/// - `phase`: The phase of the [`buttons`] associated with the input event.
/// - `buttons`: The buttons relevant to the event represented as flipped bits.
/// - `sender`: The stream to send the MouseInputEvent to.
fn send_mouse_event(
    movement_x: i64,
    movement_y: i64,
    phase: fidl_fuchsia_ui_input::PointerEventPhase,
    buttons: u32,
    device_descriptor: InputDeviceDescriptor,
    sender: &mut Sender<input_device::InputEvent>,
) {
    match sender.try_send(input_device::InputEvent {
        event_descriptor: input_device::InputEventDescriptor::Mouse({
            MouseEventDescriptor { movement_x, movement_y, phase, buttons }
        }),
        device_descriptor,
    }) {
        Err(e) => fx_log_info!("Failed to send InputEvent for mouse with error: {:?}", e),
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

/// Returns a u32 representation of the pressed buttons in the MouseInputReport.
///
/// # Parameters
/// - `mouse_report`: The report to get pressed buttons from.
fn buttons(mouse_report: &Option<fidl_fuchsia_input_report::MouseInputReport>) -> u32 {
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
    use {super::*, futures::TryStreamExt};

    fn create_input_device_proxy_and_stream(
        device_descriptor: fidl_fuchsia_input_report::DeviceDescriptor,
    ) -> InputDeviceProxy {
        let (input_device_proxy, mut stream) = fidl::endpoints::create_proxy_and_stream::<
            fidl_fuchsia_input_report::InputDeviceMarker,
        >()
        .expect("Failed to create InputDeviceProxy and stream.");
        fasync::spawn(async move {
            let request = stream.try_next().await.expect("Failed to read request.");
            match request {
                Some(fidl_fuchsia_input_report::InputDeviceRequest::GetDescriptor {
                    responder,
                }) => {
                    let _ = responder.send(device_descriptor);
                }
                _ => assert!(false),
            }
        });

        input_device_proxy
    }

    fn create_input_report(x: i64, y: i64, buttons: Vec<u8>) -> InputReport {
        InputReport {
            event_time: None,
            keyboard: None,
            mouse: Some(fidl_fuchsia_input_report::MouseInputReport {
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

    #[fasync::run_singlethreaded(test)]
    async fn bind_device_success() {
        let input_device_proxy: InputDeviceProxy =
            create_input_device_proxy_and_stream(fidl_fuchsia_input_report::DeviceDescriptor {
                device_info: Some(fidl_fuchsia_input_report::DeviceInfo {
                    vendor_id: 1,
                    product_id: 2,
                    version: 3,
                    name: "mouse".to_string(),
                }),
                mouse: Some(fidl_fuchsia_input_report::MouseDescriptor { input: None }),
                sensor: None,
                touch: None,
                keyboard: None,
            });

        let mouse_binding_result: Result<MouseBinding, Error> =
            MouseBinding::bind_device(&input_device_proxy).await;
        match mouse_binding_result {
            Ok(binding) => match binding.get_device_descriptor() {
                input_device::InputDeviceDescriptor::Mouse(mouse_device_descriptor) => {
                    assert_eq!(mouse_device_descriptor.device_id, 2)
                }
                _ => assert!(false),
            },
            Err(_) => assert!(false),
        };
    }

    #[fasync::run_singlethreaded(test)]
    async fn bind_device_without_device_info() {
        let input_device_proxy: InputDeviceProxy =
            create_input_device_proxy_and_stream(fidl_fuchsia_input_report::DeviceDescriptor {
                device_info: None,
                mouse: Some(fidl_fuchsia_input_report::MouseDescriptor { input: None }),
                sensor: None,
                touch: None,
                keyboard: None,
            });

        match MouseBinding::bind_device(&input_device_proxy).await {
            Ok(_) => assert!(false),
            Err(_) => {}
        };
    }

    // Tests that two InputEvents are sent for a button press
    #[fasync::run_singlethreaded(test)]
    async fn down_input_event() {
        const MOUSE_X: i64 = 0;
        const MOUSE_Y: i64 = 0;

        let previous_report = Some(create_input_report(0, 0, vec![]));
        let report = create_input_report(MOUSE_X, MOUSE_Y, vec![3]);
        let descriptor =
            input_device::InputDeviceDescriptor::Mouse(MouseDeviceDescriptor { device_id: 0 });
        let (event_sender, mut event_receiver) = futures::channel::mpsc::channel(1);
        let _ = MouseBinding::process_reports(
            report,
            previous_report,
            descriptor,
            &mut event_sender.clone(),
        );

        // First event received is a move
        if let Some(input_event) = event_receiver.next().await {
            match input_event {
                input_device::InputEvent {
                    event_descriptor:
                        input_device::InputEventDescriptor::Mouse(mouse_event_descriptor),
                    device_descriptor:
                        input_device::InputDeviceDescriptor::Mouse(_mouse_device_descriptor),
                } => {
                    assert_eq!(mouse_event_descriptor.movement_x, MOUSE_X);
                    assert_eq!(mouse_event_descriptor.movement_y, MOUSE_Y);
                    assert_eq!(
                        mouse_event_descriptor.phase,
                        fidl_fuchsia_ui_input::PointerEventPhase::Move
                    );
                    assert_eq!(mouse_event_descriptor.buttons, 0);
                }
                _ => assert!(false),
            }
        }

        // Second event received is a down
        if let Some(input_event) = event_receiver.next().await {
            match input_event {
                input_device::InputEvent {
                    event_descriptor:
                        input_device::InputEventDescriptor::Mouse(mouse_event_descriptor),
                    device_descriptor:
                        input_device::InputDeviceDescriptor::Mouse(_mouse_device_descriptor),
                } => {
                    assert_eq!(mouse_event_descriptor.movement_x, MOUSE_X);
                    assert_eq!(mouse_event_descriptor.movement_y, MOUSE_Y);
                    assert_eq!(
                        mouse_event_descriptor.phase,
                        fidl_fuchsia_ui_input::PointerEventPhase::Down
                    );
                    assert_eq!(mouse_event_descriptor.buttons, 4);
                }
                _ => assert!(false),
            }
        }
    }

    // Tests that two InputEvents are sent for a button release
    #[fasync::run_singlethreaded(test)]
    async fn up_input_event() {
        const MOUSE_X: i64 = 0;
        const MOUSE_Y: i64 = 0;

        let previous_report = Some(create_input_report(0, 0, vec![3]));
        let report = create_input_report(MOUSE_X, MOUSE_Y, vec![]);
        let descriptor =
            input_device::InputDeviceDescriptor::Mouse(MouseDeviceDescriptor { device_id: 0 });
        let (event_sender, mut event_receiver) = futures::channel::mpsc::channel(1);
        let _ = MouseBinding::process_reports(
            report,
            previous_report,
            descriptor,
            &mut event_sender.clone(),
        );

        // First event received is a move
        if let Some(input_event) = event_receiver.next().await {
            match input_event {
                input_device::InputEvent {
                    event_descriptor:
                        input_device::InputEventDescriptor::Mouse(mouse_event_descriptor),
                    device_descriptor:
                        input_device::InputDeviceDescriptor::Mouse(_mouse_device_descriptor),
                } => {
                    assert_eq!(mouse_event_descriptor.movement_x, MOUSE_X);
                    assert_eq!(mouse_event_descriptor.movement_y, MOUSE_Y);
                    assert_eq!(
                        mouse_event_descriptor.phase,
                        fidl_fuchsia_ui_input::PointerEventPhase::Move
                    );
                    assert_eq!(mouse_event_descriptor.buttons, 0);
                }
                _ => assert!(false),
            }
        }

        // Second event received is an up
        if let Some(input_event) = event_receiver.next().await {
            match input_event {
                input_device::InputEvent {
                    event_descriptor:
                        input_device::InputEventDescriptor::Mouse(mouse_event_descriptor),
                    device_descriptor:
                        input_device::InputDeviceDescriptor::Mouse(_mouse_device_descriptor),
                } => {
                    assert_eq!(mouse_event_descriptor.movement_x, MOUSE_X);
                    assert_eq!(mouse_event_descriptor.movement_y, MOUSE_Y);
                    assert_eq!(
                        mouse_event_descriptor.phase,
                        fidl_fuchsia_ui_input::PointerEventPhase::Up
                    );
                    assert_eq!(mouse_event_descriptor.buttons, 4);
                }
                _ => assert!(false),
            }
        }
    }

    // Tests an InputEvent representing a move
    #[fasync::run_singlethreaded(test)]
    async fn move_input_event() {
        const MOUSE_X: i64 = 30;
        const MOUSE_Y: i64 = 40;

        let previous_report = Some(create_input_report(10, 20, vec![3]));
        let report = create_input_report(MOUSE_X, MOUSE_Y, vec![3]);
        let descriptor =
            input_device::InputDeviceDescriptor::Mouse(MouseDeviceDescriptor { device_id: 0 });
        let (event_sender, mut event_receiver) = futures::channel::mpsc::channel(1);
        let _ = MouseBinding::process_reports(
            report,
            previous_report,
            descriptor,
            &mut event_sender.clone(),
        );

        if let Some(input_event) = event_receiver.next().await {
            match input_event {
                input_device::InputEvent {
                    event_descriptor:
                        input_device::InputEventDescriptor::Mouse(mouse_event_descriptor),
                    device_descriptor:
                        input_device::InputDeviceDescriptor::Mouse(_mouse_device_descriptor),
                } => {
                    assert_eq!(mouse_event_descriptor.movement_x, MOUSE_X);
                    assert_eq!(mouse_event_descriptor.movement_y, MOUSE_Y);
                    assert_eq!(
                        mouse_event_descriptor.phase,
                        fidl_fuchsia_ui_input::PointerEventPhase::Move
                    );
                    assert_eq!(mouse_event_descriptor.buttons, 4);
                }
                _ => assert!(false),
            }
        }
    }

    // Tests that three InputEvents are sent when there is a move, a button press, and
    // a button release
    #[fasync::run_singlethreaded(test)]
    async fn move_input_event_with_buttons() {
        const MOUSE_X: i64 = 30;
        const MOUSE_Y: i64 = 40;

        let previous_report = Some(create_input_report(10, 20, vec![1, 3]));
        let report = create_input_report(MOUSE_X, MOUSE_Y, vec![2, 3]);
        let descriptor =
            input_device::InputDeviceDescriptor::Mouse(MouseDeviceDescriptor { device_id: 0 });

        // This channel's buffer needs to hold an extra event
        let (event_sender, mut event_receiver) = futures::channel::mpsc::channel(2);
        let _ = MouseBinding::process_reports(
            report,
            previous_report,
            descriptor,
            &mut event_sender.clone(),
        );

        // First event received is a move
        if let Some(input_event) = event_receiver.next().await {
            match input_event {
                input_device::InputEvent {
                    event_descriptor:
                        input_device::InputEventDescriptor::Mouse(mouse_event_descriptor),
                    device_descriptor:
                        input_device::InputDeviceDescriptor::Mouse(_mouse_device_descriptor),
                } => {
                    assert_eq!(mouse_event_descriptor.movement_x, MOUSE_X);
                    assert_eq!(mouse_event_descriptor.movement_y, MOUSE_Y);
                    assert_eq!(
                        mouse_event_descriptor.phase,
                        fidl_fuchsia_ui_input::PointerEventPhase::Move
                    );
                    assert_eq!(mouse_event_descriptor.buttons, 4); // Button 3 was held
                }
                _ => assert!(false),
            }
        }

        // Second event received is an up
        if let Some(input_event) = event_receiver.next().await {
            match input_event {
                input_device::InputEvent {
                    event_descriptor:
                        input_device::InputEventDescriptor::Mouse(mouse_event_descriptor),
                    device_descriptor:
                        input_device::InputDeviceDescriptor::Mouse(_mouse_device_descriptor),
                } => {
                    assert_eq!(mouse_event_descriptor.movement_x, MOUSE_X);
                    assert_eq!(mouse_event_descriptor.movement_y, MOUSE_Y);
                    assert_eq!(
                        mouse_event_descriptor.phase,
                        fidl_fuchsia_ui_input::PointerEventPhase::Up
                    );
                    assert_eq!(mouse_event_descriptor.buttons, 1); // Button 1 was released
                }
                _ => assert!(false),
            }
        }

        // Third event received is a down
        if let Some(input_event) = event_receiver.next().await {
            match input_event {
                input_device::InputEvent {
                    event_descriptor:
                        input_device::InputEventDescriptor::Mouse(mouse_event_descriptor),
                    device_descriptor:
                        input_device::InputDeviceDescriptor::Mouse(_mouse_device_descriptor),
                } => {
                    assert_eq!(mouse_event_descriptor.movement_x, MOUSE_X);
                    assert_eq!(mouse_event_descriptor.movement_y, MOUSE_Y);
                    assert_eq!(
                        mouse_event_descriptor.phase,
                        fidl_fuchsia_ui_input::PointerEventPhase::Down
                    );
                    assert_eq!(mouse_event_descriptor.buttons, 2); // Button 2 was pressed
                }
                _ => assert!(false),
            }
        }
    }
}
