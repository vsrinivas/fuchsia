// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::input_device::{self, Handled, InputDeviceBinding},
    crate::utils::Position,
    anyhow::{format_err, Error},
    async_trait::async_trait,
    fidl_fuchsia_input_report as fidl_input_report,
    fidl_fuchsia_input_report::{InputDeviceProxy, InputReport},
    fuchsia_syslog::fx_log_err,
    fuchsia_zircon as zx,
    futures::channel::mpsc::Sender,
    std::collections::HashSet,
    std::iter::FromIterator,
};

pub type MouseButton = u8;

/// A [`MouseLocation`] represents the mouse pointer location at the time of a pointer event.
#[derive(Copy, Clone, Debug, PartialEq)]
pub enum MouseLocation {
    /// A mouse movement relative to its current position.
    Relative(Position),

    /// An absolute position, in device coordinates.
    Absolute(Position),
}

#[derive(Copy, Clone, Debug, PartialEq)]
pub enum MousePhase {
    Down, // One or more buttons were newly pressed.
    Move, // The mouse moved with no change in button state.
    Up,   // One or more buttons were newly released.
}

/// A [`MouseEvent`] represents a pointer event with a specified phase, and the buttons
/// involved in said phase. The supported phases for mice include Up, Down, and Move.
///
/// # Example
/// The following MouseEvent represents a relative movement of 40 units in the x axis
/// and 20 units in the y axis while holding the primary button (1) down.
///
/// ```
/// let mouse_device_event = input_device::InputDeviceEvent::Mouse(MouseEvent::new(
///     MouseLocation::Relative(Position { x: 40.0, y: 20.0 }),
///     MousePhase::Move,
///     HashSet::from_iter(vec![1]).into_iter()),
///     HashSet::from_iter(vec![1]).into_iter()),,
/// ));
/// ```
#[derive(Clone, Debug, PartialEq)]
pub struct MouseEvent {
    /// The mouse location.
    pub location: MouseLocation,

    /// The phase of the [`buttons`] associated with this input event.
    pub phase: MousePhase,

    /// The buttons relevant to this event.
    pub affected_buttons: HashSet<MouseButton>,

    /// The complete button state including this event.
    pub pressed_buttons: HashSet<MouseButton>,
}

impl MouseEvent {
    /// Creates a new [`MouseEvent`].
    ///
    /// # Parameters
    /// - `location`: The mouse location.
    /// - `phase`: The phase of the [`buttons`] associated with this input event.
    /// - `buttons`: The buttons relevant to this event.
    pub fn new(
        location: MouseLocation,
        phase: MousePhase,
        affected_buttons: HashSet<MouseButton>,
        pressed_buttons: HashSet<MouseButton>,
    ) -> MouseEvent {
        MouseEvent { location, phase, affected_buttons, pressed_buttons }
    }
}

/// A [`MouseBinding`] represents a connection to a mouse input device.
///
/// The [`MouseBinding`] parses and exposes mouse descriptor properties (e.g., the range of
/// possible x values) for the device it is associated with. It also parses [`InputReport`]s
/// from the device, and sends them to the device binding owner over `event_sender`.
pub struct MouseBinding {
    /// The channel to stream InputEvents to.
    event_sender: Sender<input_device::InputEvent>,

    /// Holds information about this device.
    device_descriptor: MouseDeviceDescriptor,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct MouseDeviceDescriptor {
    /// The id of the connected mouse input device.
    pub device_id: u32,

    /// The range of possible x values of absolute mouse positions reported by this device.
    pub absolute_x_range: Option<fidl_input_report::Range>,

    /// The range of possible y values of absolute mouse positions reported by this device.
    pub absolute_y_range: Option<fidl_input_report::Range>,

    /// This is a vector of ids for the mouse buttons.
    pub buttons: Option<Vec<MouseButton>>,
}

#[async_trait]
impl input_device::InputDeviceBinding for MouseBinding {
    fn input_event_sender(&self) -> Sender<input_device::InputEvent> {
        self.event_sender.clone()
    }

    fn get_device_descriptor(&self) -> input_device::InputDeviceDescriptor {
        input_device::InputDeviceDescriptor::Mouse(self.device_descriptor.clone())
    }
}

impl MouseBinding {
    /// Creates a new [`InputDeviceBinding`] from the `device_proxy`.
    ///
    /// The binding will start listening for input reports immediately and send new InputEvents
    /// to the device binding owner over `input_event_sender`.
    ///
    /// # Parameters
    /// - `device_proxy`: The proxy to bind the new [`InputDeviceBinding`] to.
    /// - `device_id`: The id of the connected mouse device.
    /// - `input_event_sender`: The channel to send new InputEvents to.
    ///
    /// # Errors
    /// If there was an error binding to the proxy.
    pub async fn new(
        device_proxy: InputDeviceProxy,
        device_id: u32,
        input_event_sender: Sender<input_device::InputEvent>,
    ) -> Result<Self, Error> {
        let device_binding =
            Self::bind_device(&device_proxy, device_id, input_event_sender).await?;
        input_device::initialize_report_stream(
            device_proxy,
            device_binding.get_device_descriptor(),
            device_binding.input_event_sender(),
            Self::process_reports,
        );

        Ok(device_binding)
    }

    /// Binds the provided input device to a new instance of `Self`.
    ///
    /// # Parameters
    /// - `device`: The device to use to initialize the binding.
    /// - `device_id`: The id of the connected mouse device.
    /// - `input_event_sender`: The channel to send new InputEvents to.
    ///
    /// # Errors
    /// If the device descriptor could not be retrieved, or the descriptor could
    /// not be parsed correctly.
    async fn bind_device(
        device: &InputDeviceProxy,
        device_id: u32,
        input_event_sender: Sender<input_device::InputEvent>,
    ) -> Result<Self, Error> {
        let device_descriptor: fidl_input_report::DeviceDescriptor =
            device.get_descriptor().await?;

        let mouse_descriptor = device_descriptor
            .mouse
            .ok_or_else(|| format_err!("DeviceDescriptor does not have a MouseDescriptor"))?;

        let mouse_input_descriptor = mouse_descriptor
            .input
            .ok_or_else(|| format_err!("MouseDescriptor does not have a MouseInputDescriptor"))?;

        let device_descriptor: MouseDeviceDescriptor = MouseDeviceDescriptor {
            device_id,
            absolute_x_range: mouse_input_descriptor.position_x.map(|axis| axis.range),
            absolute_y_range: mouse_input_descriptor.position_y.map(|axis| axis.range),
            buttons: mouse_input_descriptor.buttons,
        };

        Ok(MouseBinding { event_sender: input_event_sender, device_descriptor })
    }

    /// Parses an [`InputReport`] into one or more [`InputEvent`]s.
    ///
    /// The [`InputEvent`]s are sent to the device binding owner via [`input_event_sender`].
    ///
    /// # Parameters
    /// `report`: The incoming [`InputReport`].
    /// `previous_report`: The previous [`InputReport`] seen for the same device. This can be
    ///                    used to determine, for example, which keys are no longer present in
    ///                    a keyboard report to generate key released events. If `None`, no
    ///                    previous report was found.
    /// `device_descriptor`: The descriptor for the input device generating the input reports.
    /// `input_event_sender`: The sender for the device binding's input event stream.
    ///
    /// # Returns
    /// An [`InputReport`] which will be passed to the next call to [`process_reports`], as
    /// [`previous_report`]. If `None`, the next call's [`previous_report`] will be `None`.
    fn process_reports(
        report: InputReport,
        previous_report: Option<InputReport>,
        device_descriptor: &input_device::InputDeviceDescriptor,
        input_event_sender: &mut Sender<input_device::InputEvent>,
    ) -> Option<InputReport> {
        // Input devices can have multiple types so ensure `report` is a MouseInputReport.
        let mouse_report: &fidl_input_report::MouseInputReport = match &report.mouse {
            Some(mouse) => mouse,
            None => {
                return previous_report;
            }
        };

        let previous_buttons: HashSet<MouseButton> =
            buttons_from_optional_report(&previous_report.as_ref());
        let current_buttons: HashSet<MouseButton> = buttons_from_report(&report);

        let event_time: zx::Time = input_device::event_time_or_now(report.event_time);

        // Send a Down event with:
        // * affected_buttons: the buttons that were pressed since the previous report,
        //   i.e. that are in the current report, but were not in the previous report.
        // * pressed_buttons: the full set of currently pressed buttons, including the
        //   recently pressed ones (affected_buttons).
        send_mouse_event(
            MouseLocation::Relative(Position::zero()),
            MousePhase::Down,
            current_buttons.difference(&previous_buttons).cloned().collect(),
            current_buttons.clone(),
            device_descriptor,
            event_time,
            input_event_sender,
        );

        // Create a location for the move event. Use the absolute position if available.
        let location = if let (Some(position_x), Some(position_y)) =
            (mouse_report.position_x, mouse_report.position_y)
        {
            MouseLocation::Absolute(Position { x: position_x as f32, y: position_y as f32 })
        } else {
            MouseLocation::Relative(Position {
                x: mouse_report.movement_x.unwrap_or_default() as f32,
                y: mouse_report.movement_y.unwrap_or_default() as f32,
            })
        };

        // Send a Move event with buttons from both the current report and the previous report.
        // * affected_buttons and pressed_buttons are identical in this case, since the full
        //   set of currently pressed buttons are the same set affected by the event.
        send_mouse_event(
            location,
            MousePhase::Move,
            current_buttons.union(&previous_buttons).cloned().collect(),
            current_buttons.union(&previous_buttons).cloned().collect(),
            device_descriptor,
            event_time,
            input_event_sender,
        );

        // Send an Up event with:
        // * affected_buttons: the buttons that were released since the previous report,
        //   i.e. that were in the previous report, but are not in the current report.
        // * pressed_buttons: the full set of currently pressed buttons, excluding the
        //   recently released ones (affected_buttons).
        send_mouse_event(
            MouseLocation::Relative(Position::zero()),
            MousePhase::Up,
            previous_buttons.difference(&current_buttons).cloned().collect(),
            current_buttons.clone(),
            device_descriptor,
            event_time,
            input_event_sender,
        );

        Some(report)
    }
}

/// Sends an InputEvent over `sender`.
///
/// When no buttons are present, only [`MousePhase::Move`] events will
/// be sent.
///
/// # Parameters
/// - `location`: The mouse location.
/// - `phase`: The phase of the [`buttons`] associated with the input event.
/// - `buttons`: The buttons relevant to the event.
/// - `device_descriptor`: The descriptor for the input device generating the input reports.
/// - `event_time`: The time in nanoseconds when the event was first recorded.
/// - `sender`: The stream to send the MouseEvent to.
fn send_mouse_event(
    location: MouseLocation,
    phase: MousePhase,
    affected_buttons: HashSet<MouseButton>,
    pressed_buttons: HashSet<MouseButton>,
    device_descriptor: &input_device::InputDeviceDescriptor,
    event_time: zx::Time,
    sender: &mut Sender<input_device::InputEvent>,
) {
    // Only send Down/Up events when there are buttons affected.
    if phase != MousePhase::Move && affected_buttons.is_empty() {
        return;
    }

    // Don't send Move events when there is no relative movement.
    // However, absolute movement is always reported.
    if phase == MousePhase::Move && location == MouseLocation::Relative(Position::zero()) {
        return;
    }

    match sender.try_send(input_device::InputEvent {
        device_event: input_device::InputDeviceEvent::Mouse(MouseEvent::new(
            location,
            phase,
            affected_buttons,
            pressed_buttons,
        )),
        device_descriptor: device_descriptor.clone(),
        event_time,
        handled: Handled::No,
    }) {
        Err(e) => fx_log_err!("Failed to send MouseEvent with error: {:?}", e),
        _ => {}
    }
}

/// Returns a u32 representation of `buttons`, where each u8 of `buttons` is an id of a button and
/// indicates the position of a bit to set.
///
/// This supports hashsets with numbers from 1 to fidl_input_report::MOUSE_MAX_NUM_BUTTONS.
///
/// # Parameters
/// - `buttons`: The hashset containing the position of bits to be set.
///
/// # Example
/// ```
/// let bits = get_u32_from_buttons(&HashSet::from_iter(vec![1, 3, 5]).into_iter());
/// assert_eq!(bits, 21 /* ...00010101 */)
/// ```
pub fn get_u32_from_buttons(buttons: &HashSet<MouseButton>) -> u32 {
    let mut bits: u32 = 0;
    for button in buttons {
        if *button > 0 && *button <= fidl_input_report::MOUSE_MAX_NUM_BUTTONS as u8 {
            bits = ((1 as u32) << *button - 1) | bits;
        }
    }

    bits
}

/// Returns the set of pressed buttons present in the given input report.
///
/// # Parameters
/// - `report`: The input report to parse the mouse buttons from.
fn buttons_from_report(input_report: &fidl_input_report::InputReport) -> HashSet<MouseButton> {
    buttons_from_optional_report(&Some(input_report))
}

/// Returns the set of pressed buttons present in the given input report.
///
/// # Parameters
/// - `report`: The input report to parse the mouse buttons from.
fn buttons_from_optional_report(
    input_report: &Option<&fidl_input_report::InputReport>,
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

#[cfg(test)]
mod tests {
    use {
        super::*, crate::testing_utilities, fuchsia_async as fasync, futures::StreamExt,
        pretty_assertions::assert_eq,
    };

    const DEVICE_ID: u32 = 1;

    fn mouse_device_descriptor(device_id: u32) -> input_device::InputDeviceDescriptor {
        input_device::InputDeviceDescriptor::Mouse(MouseDeviceDescriptor {
            device_id,
            absolute_x_range: None,
            absolute_y_range: None,
            buttons: None,
        })
    }

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
            vec![1, 3, fidl_input_report::MOUSE_MAX_NUM_BUTTONS as MouseButton].into_iter(),
        ));
        assert_eq!(bits, 2147483653 /* 10...00000101 */)
    }

    /// Tests that a report containing no buttons but with movement generates a move event.
    #[fasync::run_singlethreaded(test)]
    async fn movement_without_button() {
        let location = MouseLocation::Relative(Position { x: 10.0, y: 16.0 });
        let (event_time_i64, event_time_u64) = testing_utilities::event_times();
        let first_report = testing_utilities::create_mouse_input_report(
            location,
            None, /* scroll_v */
            None, /* scroll_h */
            vec![],
            event_time_i64,
        );
        let descriptor = mouse_device_descriptor(DEVICE_ID);

        let input_reports = vec![first_report];
        let expected_events = vec![testing_utilities::create_mouse_event(
            location,
            None, /* wheel_delta_v */
            None, /* wheel_delta_h */
            MousePhase::Move,
            HashSet::new(),
            HashSet::new(),
            event_time_u64,
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
        let (event_time_i64, event_time_u64) = testing_utilities::event_times();
        let first_report = testing_utilities::create_mouse_input_report(
            MouseLocation::Relative(Position::zero()),
            None, /* scroll_v */
            None, /* scroll_h */
            vec![mouse_button],
            event_time_i64,
        );
        let descriptor = mouse_device_descriptor(DEVICE_ID);

        let input_reports = vec![first_report];
        let expected_events = vec![testing_utilities::create_mouse_event(
            MouseLocation::Relative(Position::zero()),
            None, /* wheel_delta_v */
            None, /* wheel_delta_h */
            MousePhase::Down,
            HashSet::from_iter(vec![mouse_button].into_iter()),
            HashSet::from_iter(vec![mouse_button].into_iter()),
            event_time_u64,
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
        let location = MouseLocation::Relative(Position { x: 10.0, y: 16.0 });
        let mouse_button: MouseButton = 3;
        let (event_time_i64, event_time_u64) = testing_utilities::event_times();
        let first_report = testing_utilities::create_mouse_input_report(
            location,
            None, /* scroll_v */
            None, /* scroll_h */
            vec![mouse_button],
            event_time_i64,
        );
        let descriptor = mouse_device_descriptor(DEVICE_ID);

        let input_reports = vec![first_report];
        let expected_events = vec![
            testing_utilities::create_mouse_event(
                MouseLocation::Relative(Position::zero()),
                None, /* wheel_delta_v */
                None, /* wheel_delta_h */
                MousePhase::Down,
                HashSet::from_iter(vec![mouse_button].into_iter()),
                HashSet::from_iter(vec![mouse_button].into_iter()),
                event_time_u64,
                &descriptor,
            ),
            testing_utilities::create_mouse_event(
                location,
                None, /* wheel_delta_v */
                None, /* wheel_delta_h */
                MousePhase::Move,
                HashSet::from_iter(vec![mouse_button].into_iter()),
                HashSet::from_iter(vec![mouse_button].into_iter()),
                event_time_u64,
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
        let (event_time_i64, event_time_u64) = testing_utilities::event_times();
        let first_report = testing_utilities::create_mouse_input_report(
            MouseLocation::Relative(Position::zero()),
            None, /* scroll_v */
            None, /* scroll_h */
            vec![button],
            event_time_i64,
        );
        let second_report = testing_utilities::create_mouse_input_report(
            MouseLocation::Relative(Position::zero()),
            None, /* scroll_v */
            None, /* scroll_h */
            vec![],
            event_time_i64,
        );
        let descriptor = mouse_device_descriptor(DEVICE_ID);

        let input_reports = vec![first_report, second_report];
        let expected_events = vec![
            testing_utilities::create_mouse_event(
                MouseLocation::Relative(Position::zero()),
                None, /* wheel_delta_v */
                None, /* wheel_delta_h */
                MousePhase::Down,
                HashSet::from_iter(vec![button].into_iter()),
                HashSet::from_iter(vec![button].into_iter()),
                event_time_u64,
                &descriptor,
            ),
            testing_utilities::create_mouse_event(
                MouseLocation::Relative(Position::zero()),
                None, /* wheel_delta_v */
                None, /* wheel_delta_h */
                MousePhase::Up,
                HashSet::from_iter(vec![button].into_iter()),
                HashSet::new(),
                event_time_u64,
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
        let location = MouseLocation::Relative(Position { x: 10.0, y: 16.0 });
        let button = 1;

        let (event_time_i64, event_time_u64) = testing_utilities::event_times();
        let first_report = testing_utilities::create_mouse_input_report(
            MouseLocation::Relative(Position::zero()),
            None, /* scroll_v */
            None, /* scroll_h */
            vec![button],
            event_time_i64,
        );
        let second_report = testing_utilities::create_mouse_input_report(
            location,
            None, /* scroll_v */
            None, /* scroll_h */
            vec![],
            event_time_i64,
        );
        let descriptor = mouse_device_descriptor(DEVICE_ID);

        let input_reports = vec![first_report, second_report];
        let expected_events = vec![
            testing_utilities::create_mouse_event(
                MouseLocation::Relative(Position::zero()),
                None, /* wheel_delta_v */
                None, /* wheel_delta_h */
                MousePhase::Down,
                HashSet::from_iter(vec![button].into_iter()),
                HashSet::from_iter(vec![button].into_iter()),
                event_time_u64,
                &descriptor,
            ),
            testing_utilities::create_mouse_event(
                location,
                None, /* wheel_delta_v */
                None, /* wheel_delta_h */
                MousePhase::Move,
                HashSet::from_iter(vec![button].into_iter()),
                HashSet::from_iter(vec![button].into_iter()),
                event_time_u64,
                &descriptor,
            ),
            testing_utilities::create_mouse_event(
                MouseLocation::Relative(Position::zero()),
                None, /* wheel_delta_v */
                None, /* wheel_delta_h */
                MousePhase::Up,
                HashSet::from_iter(vec![button].into_iter()),
                HashSet::new(),
                event_time_u64,
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
        let location = MouseLocation::Relative(Position { x: 10.0, y: 16.0 });
        let button = 1;

        let (event_time_i64, event_time_u64) = testing_utilities::event_times();
        let first_report = testing_utilities::create_mouse_input_report(
            MouseLocation::Relative(Position::zero()),
            None, /* scroll_v */
            None, /* scroll_h */
            vec![button],
            event_time_i64,
        );
        let second_report = testing_utilities::create_mouse_input_report(
            location,
            None, /* scroll_v */
            None, /* scroll_h */
            vec![button],
            event_time_i64,
        );
        let third_report = testing_utilities::create_mouse_input_report(
            MouseLocation::Relative(Position::zero()),
            None, /* scroll_v */
            None, /* scroll_h */
            vec![],
            event_time_i64,
        );
        let descriptor = mouse_device_descriptor(DEVICE_ID);

        let input_reports = vec![first_report, second_report, third_report];
        let expected_events = vec![
            testing_utilities::create_mouse_event(
                MouseLocation::Relative(Position::zero()),
                None, /* wheel_delta_v */
                None, /* wheel_delta_h */
                MousePhase::Down,
                HashSet::from_iter(vec![button].into_iter()),
                HashSet::from_iter(vec![button].into_iter()),
                event_time_u64,
                &descriptor,
            ),
            testing_utilities::create_mouse_event(
                location,
                None, /* wheel_delta_v */
                None, /* wheel_delta_h */
                MousePhase::Move,
                HashSet::from_iter(vec![button].into_iter()),
                HashSet::from_iter(vec![button].into_iter()),
                event_time_u64,
                &descriptor,
            ),
            testing_utilities::create_mouse_event(
                MouseLocation::Relative(Position::zero()),
                None, /* wheel_delta_v */
                None, /* wheel_delta_h */
                MousePhase::Up,
                HashSet::from_iter(vec![button].into_iter()),
                HashSet::new(),
                event_time_u64,
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

    /// Tests that a report with absolute movement to {0, 0} generates a move event.
    #[fasync::run_until_stalled(test)]
    async fn absolute_movement_to_origin() {
        let location = MouseLocation::Absolute(Position { x: 0.0, y: 0.0 });
        let (event_time_i64, event_time_u64) = testing_utilities::event_times();
        let descriptor = mouse_device_descriptor(DEVICE_ID);

        let input_reports = vec![testing_utilities::create_mouse_input_report(
            location,
            None, /* wheel_delta_v */
            None, /* wheel_delta_h */
            vec![],
            event_time_i64,
        )];
        let expected_events = vec![testing_utilities::create_mouse_event(
            location,
            None, /* wheel_delta_v */
            None, /* wheel_delta_h */
            MousePhase::Move,
            HashSet::new(),
            HashSet::new(),
            event_time_u64,
            &descriptor,
        )];

        assert_input_report_sequence_generates_events!(
            input_reports: input_reports,
            expected_events: expected_events,
            device_descriptor: descriptor,
            device_type: MouseBinding,
        );
    }

    /// Tests that a report that contains both a relative movement and absolute position
    /// generates a move event to the absolute position.
    #[fasync::run_until_stalled(test)]
    async fn report_with_both_movement_and_position() {
        let relative_movement = Position { x: 5.0, y: 5.0 };
        let absolute_position = Position { x: 10.0, y: 10.0 };
        let expected_location = MouseLocation::Absolute(absolute_position);

        let (event_time_i64, event_time_u64) = testing_utilities::event_times();
        let descriptor = mouse_device_descriptor(DEVICE_ID);

        let input_reports = vec![fidl_input_report::InputReport {
            event_time: Some(event_time_i64),
            keyboard: None,
            mouse: Some(fidl_input_report::MouseInputReport {
                movement_x: Some(relative_movement.x as i64),
                movement_y: Some(relative_movement.y as i64),
                position_x: Some(absolute_position.x as i64),
                position_y: Some(absolute_position.y as i64),
                scroll_h: None,
                scroll_v: None,
                pressed_buttons: None,
                ..fidl_input_report::MouseInputReport::EMPTY
            }),
            touch: None,
            sensor: None,
            consumer_control: None,
            trace_id: None,
            ..fidl_input_report::InputReport::EMPTY
        }];
        let expected_events = vec![testing_utilities::create_mouse_event(
            expected_location,
            None, /* wheel_delta_v */
            None, /* wheel_delta_h */
            MousePhase::Move,
            HashSet::new(),
            HashSet::new(),
            event_time_u64,
            &descriptor,
        )];

        assert_input_report_sequence_generates_events!(
            input_reports: input_reports,
            expected_events: expected_events,
            device_descriptor: descriptor,
            device_type: MouseBinding,
        );
    }

    /// Tests that two separate button presses generate two separate down events with differing
    /// sets of `affected_buttons` and `pressed_buttons`.
    #[fasync::run_singlethreaded(test)]
    async fn down_down() {
        let primary_button = 1;
        let secondary_button = 2;

        let (event_time_i64, event_time_u64) = testing_utilities::event_times();
        let first_report = testing_utilities::create_mouse_input_report(
            MouseLocation::Relative(Position::zero()),
            None, /* scroll_v */
            None, /* scroll_h */
            vec![primary_button],
            event_time_i64,
        );
        let second_report = testing_utilities::create_mouse_input_report(
            MouseLocation::Relative(Position::zero()),
            None, /* scroll_v */
            None, /* scroll_h */
            vec![primary_button, secondary_button],
            event_time_i64,
        );
        let descriptor = mouse_device_descriptor(DEVICE_ID);

        let input_reports = vec![first_report, second_report];
        let expected_events = vec![
            testing_utilities::create_mouse_event(
                MouseLocation::Relative(Position::zero()),
                None, /* wheel_delta_v */
                None, /* wheel_delta_h */
                MousePhase::Down,
                HashSet::from_iter(vec![primary_button].into_iter()),
                HashSet::from_iter(vec![primary_button].into_iter()),
                event_time_u64,
                &descriptor,
            ),
            testing_utilities::create_mouse_event(
                MouseLocation::Relative(Position::zero()),
                None, /* wheel_delta_v */
                None, /* wheel_delta_h */
                MousePhase::Down,
                HashSet::from_iter(vec![secondary_button].into_iter()),
                HashSet::from_iter(vec![primary_button, secondary_button].into_iter()),
                event_time_u64,
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

    /// Tests that two staggered button presses followed by stagged releases generate four mouse
    /// events with distinct `affected_buttons` and `pressed_buttons`.
    /// Specifically, we test and expect the following in order:
    /// | Action           | MousePhase | `affected_buttons` | `pressed_buttons` |
    /// | ---------------- | ---------- | ------------------ | ----------------- |
    /// | Press button 1   | Down       | [1]                | [1]               |
    /// | Press button 2   | Down       | [2]                | [1, 2]            |
    /// | Release button 1 | Up         | [1]                | [2]               |
    /// | Release button 2 | Up         | [2]                | []                |
    #[fasync::run_singlethreaded(test)]
    async fn down_down_up_up() {
        let primary_button = 1;
        let secondary_button = 2;

        let (event_time_i64, event_time_u64) = testing_utilities::event_times();
        let first_report = testing_utilities::create_mouse_input_report(
            MouseLocation::Relative(Position::zero()),
            None, /* scroll_v */
            None, /* scroll_h */
            vec![primary_button],
            event_time_i64,
        );
        let second_report = testing_utilities::create_mouse_input_report(
            MouseLocation::Relative(Position::zero()),
            None, /* scroll_v */
            None, /* scroll_h */
            vec![primary_button, secondary_button],
            event_time_i64,
        );
        let third_report = testing_utilities::create_mouse_input_report(
            MouseLocation::Relative(Position::zero()),
            None, /* scroll_v */
            None, /* scroll_h */
            vec![secondary_button],
            event_time_i64,
        );
        let fourth_report = testing_utilities::create_mouse_input_report(
            MouseLocation::Relative(Position::zero()),
            None, /* scroll_v */
            None, /* scroll_h */
            vec![],
            event_time_i64,
        );
        let descriptor = mouse_device_descriptor(DEVICE_ID);

        let input_reports = vec![first_report, second_report, third_report, fourth_report];
        let expected_events = vec![
            testing_utilities::create_mouse_event(
                MouseLocation::Relative(Position::zero()),
                None, /* wheel_delta_v */
                None, /* wheel_delta_h */
                MousePhase::Down,
                HashSet::from_iter(vec![primary_button].into_iter()),
                HashSet::from_iter(vec![primary_button].into_iter()),
                event_time_u64,
                &descriptor,
            ),
            testing_utilities::create_mouse_event(
                MouseLocation::Relative(Position::zero()),
                None, /* wheel_delta_v */
                None, /* wheel_delta_h */
                MousePhase::Down,
                HashSet::from_iter(vec![secondary_button].into_iter()),
                HashSet::from_iter(vec![primary_button, secondary_button].into_iter()),
                event_time_u64,
                &descriptor,
            ),
            testing_utilities::create_mouse_event(
                MouseLocation::Relative(Position::zero()),
                None, /* wheel_delta_v */
                None, /* wheel_delta_h */
                MousePhase::Up,
                HashSet::from_iter(vec![primary_button].into_iter()),
                HashSet::from_iter(vec![secondary_button].into_iter()),
                event_time_u64,
                &descriptor,
            ),
            testing_utilities::create_mouse_event(
                MouseLocation::Relative(Position::zero()),
                None, /* wheel_delta_v */
                None, /* wheel_delta_h */
                MousePhase::Up,
                HashSet::from_iter(vec![secondary_button].into_iter()),
                HashSet::new(),
                event_time_u64,
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
