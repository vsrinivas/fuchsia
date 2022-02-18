// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::input_device,
    crate::input_handler::InputHandler,
    crate::mouse_binding,
    crate::utils::{CursorMessage, Position},
    async_trait::async_trait,
    fidl_fuchsia_ui_input as fidl_ui_input, fidl_fuchsia_ui_scenic as fidl_ui_scenic,
    fuchsia_scenic as scenic,
    fuchsia_syslog::fx_log_err,
    fuchsia_zircon as zx,
    futures::{channel::mpsc::Sender, SinkExt},
    std::cell::RefCell,
    std::collections::HashSet,
    std::rc::Rc,
};

impl From<mouse_binding::MousePhase> for fidl_ui_input::PointerEventPhase {
    fn from(phase: mouse_binding::MousePhase) -> fidl_ui_input::PointerEventPhase {
        match phase {
            mouse_binding::MousePhase::Down => fidl_ui_input::PointerEventPhase::Down,
            mouse_binding::MousePhase::Move => fidl_ui_input::PointerEventPhase::Move,
            mouse_binding::MousePhase::Up => fidl_ui_input::PointerEventPhase::Up,
        }
    }
}

/// A [`GfxMouseHandler`] tracks the mouse position and sends updates to clients.
pub struct GfxMouseHandler {
    /// The current position.
    current_position: RefCell<Position>,

    /// The maximum position sent to clients, used to bound relative movements
    /// and scale absolute positions from device coordinates.
    max_position: Position,

    /// A [`Sender`] used to communicate the current cursor state.
    cursor_sender: RefCell<Sender<CursorMessage>>,

    /// The Scenic session to send pointer events to.
    scenic_session: scenic::SessionPtr,

    /// The Scenic compositor id to tag pointer events with.
    scenic_compositor_id: u32,

    /// Set to true when in immersive mode.
    immersive_mode: RefCell<bool>,

    /// The current visibility for the cursor.
    is_cursor_visible: RefCell<bool>,
}

#[async_trait(?Send)]
impl InputHandler for GfxMouseHandler {
    async fn handle_input_event(
        self: Rc<Self>,
        mut input_event: input_device::InputEvent,
    ) -> Vec<input_device::InputEvent> {
        match input_event {
            input_device::InputEvent {
                device_event: input_device::InputDeviceEvent::Mouse(ref mouse_event),
                device_descriptor: input_device::InputDeviceDescriptor::Mouse(ref mouse_descriptor),
                event_time,
                handled: input_device::Handled::No,
            } => {
                self.update_cursor_position(&mouse_event, &mouse_descriptor).await;
                self.update_cursor_visibility(!*self.immersive_mode.borrow()).await;
                self.send_events_to_scenic(
                    fidl_ui_input::PointerEventPhase::from(mouse_event.phase),
                    &mouse_event.affected_buttons,
                    &mouse_descriptor,
                    event_time,
                )
                .await;

                // Consume the input event.
                input_event.handled = input_device::Handled::Yes
            }
            input_device::InputEvent {
                device_event: input_device::InputDeviceEvent::Touch(ref _touch_event),
                device_descriptor:
                    input_device::InputDeviceDescriptor::Touch(ref _touch_device_descriptor),
                event_time: _,
                handled: _,
            } => {
                // Hide the cursor on touch input.
                // TODO(fxbug.dev/90290): Remove this workaround when we have a
                // proper cursor API.
                let visible = false;
                self.update_cursor_visibility(visible).await;
            }
            input_device::InputEvent {
                device_event: input_device::InputDeviceEvent::Keyboard(ref keyboard_device_event),
                device_descriptor: input_device::InputDeviceDescriptor::Keyboard(_),
                event_time: _,
                handled: _,
            } => {
                // Alt+Shift+I toggles immersive mode.
                //
                // Immersive mode hides the cursor and is a temporary workaround
                // until we have a cursor API that makes it possible for UI
                // components to control the appearance of the cursor.
                //
                // TODO(fxbug.dev/90290): Remove this workaround when we have a
                // proper cursor API.
                if keyboard_device_event.get_event_type()
                    == fidl_fuchsia_ui_input3::KeyEventType::Pressed
                    && keyboard_device_event.get_key() == fidl_fuchsia_input::Key::I
                    && keyboard_device_event
                        .get_modifiers()
                        .filter(|m| {
                            m.contains(
                                fidl_fuchsia_ui_input3::Modifiers::ALT
                                    | fidl_fuchsia_ui_input3::Modifiers::SHIFT,
                            )
                        })
                        .is_some()
                {
                    let mut immersive_mode = self.immersive_mode.borrow_mut();
                    *immersive_mode = !*immersive_mode;

                    self.update_cursor_visibility(!*immersive_mode).await;

                    // Consume the input event.
                    input_event.handled = input_device::Handled::Yes
                }
            }
            _ => {}
        }
        vec![input_event]
    }
}

impl GfxMouseHandler {
    /// Creates a new [`GfxMouseHandler`] that sends pointer events to Scenic and tracks cursor
    /// position.
    ///
    /// # Parameters
    /// - `initial_position`: The initial position.
    /// - `max_position`: The maximum position, used to bound events sent to clients.
    /// - `position_sender`: A [`Sender`] used to communicate the current position.
    /// - `scenic_session`: The Scenic session to send pointer events to.
    /// - `scenic_compositor_id`: The Scenic compositor id to tag pointer events with.
    pub fn new(
        initial_position: Position,
        max_position: Position,
        cursor_sender: Sender<CursorMessage>,
        scenic_session: scenic::SessionPtr,
        scenic_compositor_id: u32,
    ) -> Rc<GfxMouseHandler> {
        Rc::new(GfxMouseHandler {
            max_position,
            cursor_sender: RefCell::new(cursor_sender),
            scenic_session,
            scenic_compositor_id,
            current_position: RefCell::new(initial_position),
            immersive_mode: RefCell::new(false),
            is_cursor_visible: RefCell::new(true),
        })
    }

    /// Updates the current cursor position according to the received mouse event.
    ///
    /// The updated position is sent to a client via `self.cursor_sender`.
    ///
    /// If there is no movement, the location is not sent to clients.
    ///
    /// # Parameters
    /// - `mouse_event`: The mouse event to use to update the cursor location.
    /// - `device_descriptor`: The descriptor for the input device generating the input reports.
    async fn update_cursor_position(
        self: &Rc<Self>,
        mouse_event: &mouse_binding::MouseEvent,
        device_descriptor: &mouse_binding::MouseDeviceDescriptor,
    ) {
        let new_position = match mouse_event.location {
            mouse_binding::MouseLocation::Relative(offset) if offset != Position::zero() => {
                *self.current_position.borrow() + offset
            }
            mouse_binding::MouseLocation::Absolute(position)
                if position != *self.current_position.borrow() =>
            {
                self.scale_absolute_position(&position, &device_descriptor)
            }
            _ => return,
        };

        let pos = {
            let mut current_position = self.current_position.borrow_mut();
            *current_position = new_position;

            Position::clamp(&mut *current_position, Position::zero(), self.max_position);

            *current_position
        };
        match self.cursor_sender.borrow_mut().send(CursorMessage::SetPosition(pos)).await {
            Err(e) => {
                fx_log_err!("Failed to send current mouse position with error {:?}", e);
            }
            _ => {}
        }
    }

    /// Sends a pointer event with the given phase and buttons to Scenic.
    ///
    /// # Parameters
    /// - `phase`: The phase of the buttons associated with the mouse event.
    /// - `buttons`: The buttons associated with the event.
    /// - `device_descriptor`: The descriptor for the input device generating the input reports.
    /// - `event_time`: The time in nanoseconds when the event was first recorded.
    async fn send_events_to_scenic(
        self: &Rc<Self>,
        phase: fidl_ui_input::PointerEventPhase,
        buttons: &HashSet<mouse_binding::MouseButton>,
        device_descriptor: &mouse_binding::MouseDeviceDescriptor,
        event_time: zx::Time,
    ) {
        let buttons = mouse_binding::get_u32_from_buttons(buttons);

        let pointer_event = fidl_ui_input::PointerEvent {
            event_time: event_time.into_nanos() as u64,
            device_id: device_descriptor.device_id,
            pointer_id: 0,
            type_: fidl_ui_input::PointerEventType::Mouse,
            phase,
            x: self.current_position.borrow().x,
            y: self.current_position.borrow().y,
            radius_major: 0.0,
            radius_minor: 0.0,
            buttons,
        };

        let pointer_command = fidl_ui_input::SendPointerInputCmd {
            compositor_id: self.scenic_compositor_id,
            pointer_event,
        };

        let send_pointer_command = fidl_ui_input::Command::SendPointerInput(pointer_command);
        let mut session = self.scenic_session.lock();
        session.enqueue(fidl_ui_scenic::Command::Input(send_pointer_command));
        session.flush();
    }

    /// Returns an absolute cursor position scaled from device coordinates to the handler's
    /// max position.
    ///
    /// Returns the original position if the device descriptor does not specify ranges
    /// for absolute coordinates.
    ///
    /// # Parameters
    /// - `position`: Absolute cursor position in device coordinates.
    /// - `device_descriptor`: The descriptor for the input device generating the input reports.
    fn scale_absolute_position(
        self: &Rc<Self>,
        position: &Position,
        device_descriptor: &mouse_binding::MouseDeviceDescriptor,
    ) -> Position {
        match (device_descriptor.absolute_x_range, device_descriptor.absolute_y_range) {
            (Some(x_range), Some(y_range)) => {
                let range_min = Position { x: x_range.min as f32, y: y_range.min as f32 };
                let range_max = Position { x: x_range.max as f32, y: y_range.max as f32 };
                self.max_position * ((*position - range_min) / (range_max - range_min))
            }
            _ => *position,
        }
    }

    /// Updates the current cursor's visibility.
    ///
    /// The updated visibility is sent to a client via `self.cursor_sender`.
    ///
    /// If there is no change to visibility, the state is not sent to clients.
    ///
    /// # Parameters
    /// - `visible`: The new visibility of the cursor.
    async fn update_cursor_visibility(self: &Rc<Self>, visible: bool) {
        let mut is_cursor_visible = self.is_cursor_visible.borrow_mut();

        // No change to visibility needed.
        if visible == *is_cursor_visible {
            return;
        }
        *is_cursor_visible = visible;

        match self.cursor_sender.borrow_mut().send(CursorMessage::SetVisibility(visible)).await {
            Err(e) => {
                fx_log_err!("Failed to send current cursor visibilty with error {:?}", e);
            }
            _ => {}
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::testing_utilities::{
            assert_handler_ignores_input_event_sequence, create_mouse_event,
            create_mouse_event_with_handled, create_touch_contact, create_touch_event_with_handled,
        },
        crate::touch_binding,
        crate::utils::Position,
        assert_matches::assert_matches,
        fidl_fuchsia_input_report as fidl_input_report, fidl_fuchsia_ui_scenic as fidl_ui_scenic,
        fuchsia_async as fasync, fuchsia_zircon as zx,
        futures::StreamExt,
        maplit::hashmap,
        pretty_assertions::assert_eq,
    };

    const SCENIC_COMPOSITOR_ID: u32 = 1;
    const SCENIC_DISPLAY_WIDTH: f32 = 100.0;
    const SCENIC_DISPLAY_HEIGHT: f32 = 150.0;

    fn mouse_device_descriptor(device_id: u32) -> input_device::InputDeviceDescriptor {
        input_device::InputDeviceDescriptor::Mouse(mouse_binding::MouseDeviceDescriptor {
            device_id,
            absolute_x_range: None,
            absolute_y_range: None,
            buttons: None,
        })
    }

    /// Creates a PointerEvent with the given parameters.
    ///
    /// # Parameters
    /// - `position`: The location of the event.
    /// - `phase`: The phase of the event.
    /// - `device_id`: The id of the device where this event originated.
    /// - `event_time: The time of the event.
    fn create_pointer_event(
        position: Position,
        phase: fidl_ui_input::PointerEventPhase,
        device_id: u32,
        event_time: zx::Time,
    ) -> fidl_ui_input::PointerEvent {
        fidl_ui_input::PointerEvent {
            event_time: event_time.into_nanos() as u64,
            device_id,
            pointer_id: 0,
            type_: fidl_ui_input::PointerEventType::Mouse,
            phase,
            x: position.x,
            y: position.y,
            radius_major: 0.0,
            radius_minor: 0.0,
            buttons: 0,
        }
    }

    /// Validates the event `command` against `expected_event`.
    ///
    /// # Parameters
    /// - `command`: The command received by the Scenic session.
    /// - `expected_event`: The expected event.
    fn verify_pointer_event(
        command: fidl_ui_scenic::Command,
        expected_event: fidl_ui_input::PointerEvent,
    ) {
        match command {
            fidl_ui_scenic::Command::Input(fidl_ui_input::Command::SendPointerInput(
                fidl_ui_input::SendPointerInputCmd {
                    compositor_id: _,
                    pointer_event:
                        fidl_ui_input::PointerEvent {
                            event_time,
                            device_id,
                            pointer_id,
                            type_,
                            phase,
                            x,
                            y,
                            radius_major: _,
                            radius_minor: _,
                            buttons,
                        },
                },
            )) => {
                assert_eq!(event_time, expected_event.event_time);
                assert_eq!(type_, fidl_ui_input::PointerEventType::Mouse);
                assert_eq!(device_id, expected_event.device_id);
                assert_eq!(pointer_id, expected_event.pointer_id);
                assert_eq!(phase, expected_event.phase);
                assert_eq!(x, expected_event.x);
                assert_eq!(y, expected_event.y);
                assert_eq!(buttons, expected_event.buttons);
            }
            _ => {
                assert!(false);
            }
        }
    }

    /// Returns an TouchDescriptor.
    fn get_touch_device_descriptor() -> input_device::InputDeviceDescriptor {
        input_device::InputDeviceDescriptor::Touch(touch_binding::TouchDeviceDescriptor {
            device_id: 1,
            contacts: vec![touch_binding::ContactDeviceDescriptor {
                x_range: fidl_input_report::Range { min: 0, max: 100 },
                y_range: fidl_input_report::Range { min: 0, max: 100 },
                pressure_range: None,
                width_range: None,
                height_range: None,
            }],
        })
    }

    // Tests that a mouse move event both sends an update to scenic and sends the current cursor
    // location via the cursor sender.
    #[fasync::run_singlethreaded(test)]
    async fn move_event() {
        const DEVICE_ID: u32 = 1;

        let (session_proxy, mut session_request_stream) =
            fidl::endpoints::create_proxy_and_stream::<fidl_ui_scenic::SessionMarker>()
                .expect("Failed to create ScenicProxy and stream.");
        let scenic_session: scenic::SessionPtr = scenic::Session::new(session_proxy);

        let (sender, mut receiver) = futures::channel::mpsc::channel(1);

        let mouse_handler = GfxMouseHandler::new(
            Position { x: 0.0, y: 0.0 },
            Position { x: SCENIC_DISPLAY_WIDTH, y: SCENIC_DISPLAY_HEIGHT },
            sender,
            scenic_session.clone(),
            SCENIC_COMPOSITOR_ID,
        );

        let cursor_relative_position = Position { x: 50.0, y: 75.0 };
        let cursor_location = mouse_binding::MouseLocation::Relative(cursor_relative_position);
        let descriptor = mouse_device_descriptor(DEVICE_ID);
        let event_time = zx::Time::get_monotonic();
        let input_events = vec![create_mouse_event(
            cursor_location,
            None, /* wheel_delta_v */
            None, /* wheel_delta_h */
            mouse_binding::MousePhase::Move,
            HashSet::<mouse_binding::MouseButton>::new(),
            HashSet::<mouse_binding::MouseButton>::new(),
            event_time,
            &descriptor,
        )];

        let expected_commands = vec![create_pointer_event(
            cursor_relative_position,
            fidl_ui_input::PointerEventPhase::Move,
            DEVICE_ID,
            event_time,
        )];

        assert_input_event_sequence_generates_scenic_events!(
            input_handler: mouse_handler,
            input_events: input_events,
            expected_commands: expected_commands,
            scenic_session_request_stream: session_request_stream,
            assert_command: verify_pointer_event,
        );

        let expected_cursor_location = cursor_relative_position;
        match receiver.next().await {
            Some(CursorMessage::SetPosition(cursor_location)) => {
                assert_eq!(cursor_location, expected_cursor_location)
            }
            _ => assert!(false),
        };
    }

    // Tests that a mouse move event that moves outside the width and height of the display
    // is bounded to the display.
    #[fasync::run_singlethreaded(test)]
    async fn move_outside_max_bounds_event() {
        const DEVICE_ID: u32 = 1;

        let (session_proxy, mut session_request_stream) =
            fidl::endpoints::create_proxy_and_stream::<fidl_ui_scenic::SessionMarker>()
                .expect("Failed to create ScenicProxy and stream.");
        let scenic_session: scenic::SessionPtr = scenic::Session::new(session_proxy);

        let (sender, mut receiver) = futures::channel::mpsc::channel(1);

        let mouse_handler = GfxMouseHandler::new(
            Position { x: 0.0, y: 0.0 },
            Position { x: SCENIC_DISPLAY_WIDTH, y: SCENIC_DISPLAY_HEIGHT },
            sender,
            scenic_session.clone(),
            SCENIC_COMPOSITOR_ID,
        );

        let start = Position { x: SCENIC_DISPLAY_WIDTH, y: SCENIC_DISPLAY_HEIGHT };
        let cursor_location = mouse_binding::MouseLocation::Relative(Position {
            x: SCENIC_DISPLAY_WIDTH + 2.0,
            y: SCENIC_DISPLAY_HEIGHT + 2.0,
        });
        let descriptor = mouse_device_descriptor(DEVICE_ID);
        let event_time = zx::Time::get_monotonic();
        let input_events = vec![create_mouse_event(
            cursor_location,
            None, /* wheel_delta_v */
            None, /* wheel_delta_h */
            mouse_binding::MousePhase::Move,
            HashSet::<mouse_binding::MouseButton>::new(),
            HashSet::<mouse_binding::MouseButton>::new(),
            event_time,
            &descriptor,
        )];

        let expected_commands = vec![create_pointer_event(
            start,
            fidl_ui_input::PointerEventPhase::Move,
            DEVICE_ID,
            event_time,
        )];

        assert_input_event_sequence_generates_scenic_events!(
            input_handler: mouse_handler,
            input_events: input_events,
            expected_commands: expected_commands,
            scenic_session_request_stream: session_request_stream,
            assert_command: verify_pointer_event,
        );

        let expected_cursor_location =
            Position { x: SCENIC_DISPLAY_WIDTH, y: SCENIC_DISPLAY_HEIGHT };
        match receiver.next().await {
            Some(CursorMessage::SetPosition(cursor_location)) => {
                assert_eq!(cursor_location, expected_cursor_location)
            }
            _ => assert!(false),
        };
    }

    // Tests that a mouse move event that moves outside the width and height of the display
    // is bounded to the display.
    #[fasync::run_singlethreaded(test)]
    async fn move_outside_min_bounds_event() {
        const DEVICE_ID: u32 = 1;

        let (session_proxy, mut session_request_stream) =
            fidl::endpoints::create_proxy_and_stream::<fidl_ui_scenic::SessionMarker>()
                .expect("Failed to create ScenicProxy and stream.");
        let scenic_session: scenic::SessionPtr = scenic::Session::new(session_proxy);

        let (sender, mut receiver) = futures::channel::mpsc::channel(1);

        let mouse_handler = GfxMouseHandler::new(
            Position { x: 0.0, y: 0.0 },
            Position { x: SCENIC_DISPLAY_WIDTH, y: SCENIC_DISPLAY_HEIGHT },
            sender,
            scenic_session.clone(),
            SCENIC_COMPOSITOR_ID,
        );

        let cursor_location =
            mouse_binding::MouseLocation::Relative(Position { x: -20.0, y: -15.0 });
        let descriptor = mouse_device_descriptor(DEVICE_ID);
        let event_time = zx::Time::get_monotonic();
        let input_events = vec![create_mouse_event(
            cursor_location,
            None, /* wheel_delta_v */
            None, /* wheel_delta_h */
            mouse_binding::MousePhase::Move,
            HashSet::<mouse_binding::MouseButton>::new(),
            HashSet::<mouse_binding::MouseButton>::new(),
            event_time,
            &descriptor,
        )];

        let expected_commands = vec![create_pointer_event(
            Position { x: 0.0, y: 0.0 },
            fidl_ui_input::PointerEventPhase::Move,
            DEVICE_ID,
            event_time,
        )];

        assert_input_event_sequence_generates_scenic_events!(
            input_handler: mouse_handler,
            input_events: input_events,
            expected_commands: expected_commands,
            scenic_session_request_stream: session_request_stream,
            assert_command: verify_pointer_event,
        );

        let expected_cursor_location = Position { x: 0.0, y: 0.0 };
        match receiver.next().await {
            Some(CursorMessage::SetPosition(cursor_location)) => {
                assert_eq!(cursor_location, expected_cursor_location)
            }
            _ => assert!(false),
        };
    }

    // Tests that an absolute mouse move event scales the location from device coordinates to
    // between {0, 0} and the handler's maximum position.
    #[fasync::run_until_stalled(test)]
    async fn move_absolute_event() {
        const DEVICE_ID: u32 = 1;

        let (session_proxy, mut session_request_stream) =
            fidl::endpoints::create_proxy_and_stream::<fidl_ui_scenic::SessionMarker>()
                .expect("Failed to create ScenicProxy and stream.");
        let scenic_session: scenic::SessionPtr = scenic::Session::new(session_proxy);

        let (sender, mut receiver) = futures::channel::mpsc::channel(1);

        let mouse_handler = GfxMouseHandler::new(
            Position { x: 0.0, y: 0.0 },
            Position { x: SCENIC_DISPLAY_WIDTH, y: SCENIC_DISPLAY_HEIGHT },
            sender,
            scenic_session.clone(),
            SCENIC_COMPOSITOR_ID,
        );

        // The location is rescaled from the device coordinate system defined
        // by `absolute_x_range` and `absolute_y_range`, to the display coordinate
        // system defined by `max_position`.
        //
        //          -50 y              0 +------------------ w
        //            |                  |         .
        //            |                  |         .
        //            |                  |         .
        // -50 x -----o----- 50   ->     | . . . . . . . . .
        //            |                  |         .
        //         * { x: -25, y: 25 }   |    * { x: w * 0.25, y: h * 0.75 }
        //            |                  |         .
        //           50                h |         .
        //
        // Where w = SCENIC_DISPLAY_WIDTH, h = SCENIC_DISPLAY_HEIGHT
        let cursor_location =
            mouse_binding::MouseLocation::Absolute(Position { x: -25.0, y: 25.0 });
        let descriptor =
            input_device::InputDeviceDescriptor::Mouse(mouse_binding::MouseDeviceDescriptor {
                device_id: DEVICE_ID,
                absolute_x_range: Some(fidl_input_report::Range { min: -50, max: 50 }),
                absolute_y_range: Some(fidl_input_report::Range { min: -50, max: 50 }),
                buttons: None,
            });
        let event_time = zx::Time::get_monotonic();
        let input_events = vec![create_mouse_event(
            cursor_location,
            None, /* wheel_delta_v */
            None, /* wheel_delta_h */
            mouse_binding::MousePhase::Move,
            HashSet::<mouse_binding::MouseButton>::new(),
            HashSet::<mouse_binding::MouseButton>::new(),
            event_time,
            &descriptor,
        )];

        let expected_position =
            Position { x: SCENIC_DISPLAY_WIDTH * 0.25, y: SCENIC_DISPLAY_HEIGHT * 0.75 };

        let expected_commands = vec![create_pointer_event(
            expected_position,
            fidl_ui_input::PointerEventPhase::Move,
            DEVICE_ID,
            event_time,
        )];

        assert_input_event_sequence_generates_scenic_events!(
            input_handler: mouse_handler,
            input_events: input_events,
            expected_commands: expected_commands,
            scenic_session_request_stream: session_request_stream,
            assert_command: verify_pointer_event,
        );

        let expected_cursor_location = expected_position;
        match receiver.next().await {
            Some(CursorMessage::SetPosition(cursor_location)) => {
                assert_eq!(cursor_location, expected_cursor_location)
            }
            _ => assert!(false),
        };
    }

    // Tests that a mouse move event that has already been handled is not forwarded to scenic.
    #[fasync::run_singlethreaded(test)]
    async fn handler_ignores_handled_events() {
        const DEVICE_ID: u32 = 1;

        let (session_proxy, session_request_stream) =
            fidl::endpoints::create_proxy_and_stream::<fidl_ui_scenic::SessionMarker>()
                .expect("Failed to create ScenicProxy and stream.");
        let scenic_session: scenic::SessionPtr = scenic::Session::new(session_proxy);

        let (sender, _) = futures::channel::mpsc::channel(1);
        let mouse_handler = GfxMouseHandler::new(
            Position { x: 0.0, y: 0.0 },
            Position { x: SCENIC_DISPLAY_WIDTH, y: SCENIC_DISPLAY_HEIGHT },
            sender,
            scenic_session.clone(),
            SCENIC_COMPOSITOR_ID,
        );

        let cursor_relative_position = Position { x: 50.0, y: 75.0 };
        let cursor_location = mouse_binding::MouseLocation::Relative(cursor_relative_position);
        let descriptor = mouse_device_descriptor(DEVICE_ID);
        let event_time = zx::Time::get_monotonic();
        let input_events = vec![create_mouse_event_with_handled(
            cursor_location,
            None, /* wheel_delta_v */
            None, /* wheel_delta_h */
            mouse_binding::MousePhase::Move,
            HashSet::<mouse_binding::MouseButton>::new(),
            HashSet::<mouse_binding::MouseButton>::new(),
            event_time,
            &descriptor,
            input_device::Handled::Yes,
        )];

        assert_handler_ignores_input_event_sequence(
            mouse_handler,
            input_events,
            session_request_stream,
        )
        .await;
    }

    // Tests that initial location is handled correctly.
    #[fasync::run_singlethreaded(test)]
    async fn initial_location() {
        const DEVICE_ID: u32 = 1;

        let (session_proxy, mut session_request_stream) =
            fidl::endpoints::create_proxy_and_stream::<fidl_ui_scenic::SessionMarker>()
                .expect("Failed to create ScenicProxy and stream.");
        let scenic_session: scenic::SessionPtr = scenic::Session::new(session_proxy);

        let (sender, mut receiver) = futures::channel::mpsc::channel(1);

        let initial_location = Position { x: 10.0, y: 10.0 };
        let mouse_handler = GfxMouseHandler::new(
            initial_location,
            Position { x: SCENIC_DISPLAY_WIDTH, y: SCENIC_DISPLAY_HEIGHT },
            sender,
            scenic_session.clone(),
            SCENIC_COMPOSITOR_ID,
        );

        let cursor_relative_position = Position { x: 50.0, y: 75.0 };
        let cursor_location = mouse_binding::MouseLocation::Relative(cursor_relative_position);
        let descriptor = mouse_device_descriptor(DEVICE_ID);
        let event_time = zx::Time::get_monotonic();
        let input_events = vec![create_mouse_event(
            cursor_location,
            None, /* wheel_delta_v */
            None, /* wheel_delta_h */
            mouse_binding::MousePhase::Move,
            HashSet::<mouse_binding::MouseButton>::new(),
            HashSet::<mouse_binding::MouseButton>::new(),
            event_time,
            &descriptor,
        )];

        let expected_commands = vec![create_pointer_event(
            initial_location + cursor_relative_position,
            fidl_ui_input::PointerEventPhase::Move,
            DEVICE_ID,
            event_time,
        )];

        assert_input_event_sequence_generates_scenic_events!(
            input_handler: mouse_handler,
            input_events: input_events,
            expected_commands: expected_commands,
            scenic_session_request_stream: session_request_stream,
            assert_command: verify_pointer_event,
        );

        let expected_cursor_location = initial_location + cursor_relative_position;
        match receiver.next().await {
            Some(CursorMessage::SetPosition(cursor_location)) => {
                assert_eq!(cursor_location, expected_cursor_location)
            }
            _ => assert!(false),
        };
    }

    // Tests that touch events hide the cursor.
    #[fasync::run_singlethreaded(test)]
    async fn touch_hides_cursor() {
        let (session_proxy, session_request_stream) =
            fidl::endpoints::create_proxy_and_stream::<fidl_ui_scenic::SessionMarker>()
                .expect("Failed to create ScenicProxy and stream.");
        let scenic_session: scenic::SessionPtr = scenic::Session::new(session_proxy);

        let (sender, mut receiver) = futures::channel::mpsc::channel(1);

        let mouse_handler = GfxMouseHandler::new(
            Position { x: 0.0, y: 0.0 },
            Position { x: SCENIC_DISPLAY_WIDTH, y: SCENIC_DISPLAY_HEIGHT },
            sender,
            scenic_session.clone(),
            SCENIC_COMPOSITOR_ID,
        );

        const TOUCH_ID: u32 = 1;
        let touch_descriptor = get_touch_device_descriptor();
        let event_time = zx::Time::get_monotonic();
        let input_events = vec![create_touch_event_with_handled(
            hashmap! {
                fidl_ui_input::PointerEventPhase::Add
                    => vec![create_touch_contact(TOUCH_ID, Position{ x: 20.0, y: 40.0 })],
                fidl_ui_input::PointerEventPhase::Down
                    => vec![create_touch_contact(TOUCH_ID, Position{ x: 20.0, y: 40.0 })],
            },
            event_time,
            &touch_descriptor,
            input_device::Handled::Yes,
        )];

        assert_handler_ignores_input_event_sequence(
            mouse_handler,
            input_events,
            session_request_stream,
        )
        .await;

        // Touch event should hide the cursor.
        match receiver.next().await {
            Some(CursorMessage::SetVisibility(visible)) => {
                assert_eq!(visible, false)
            }
            _ => assert!(false),
        };
    }
}
