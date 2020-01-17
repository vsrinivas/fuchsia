// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::input_device,
    crate::input_handler::InputHandler,
    crate::mouse,
    async_trait::async_trait,
    fidl_fuchsia_ui_input as fidl_ui_input, fidl_fuchsia_ui_scenic as fidl_ui_scenic,
    fuchsia_scenic as scenic,
    fuchsia_zircon::{ClockId, Time},
    futures::{channel::mpsc::Sender, SinkExt},
    std::collections::HashSet,
};

/// The location of a mouse cursor.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct CursorLocation {
    /// The x location of the cursor, in pixels.
    pub x: f32,

    /// The y location of the cursor, in pixels.
    pub y: f32,
}

/// A [`MouseCursorHandler`] tracks the mouse position and renders a cursor.
///
/// Clients can customize the starting position for the cursor, and also specify
/// a custom mouse cursor to render.
pub struct MouseHandler {
    /// The current cursor location.
    current_cursor_location: CursorLocation,

    /// The maximum cursor location, used to bound events sent to clients.
    max_cursor_location: CursorLocation,

    /// A [`Sender`] used to communicate the current cursor location.
    cursor_location_sender: Option<Sender<CursorLocation>>,

    /// The Scenic session to send pointer events to.
    scenic_session: scenic::SessionPtr,

    /// The Scenic compositor id to tag pointer events with.
    scenic_compositor_id: u32,
}

#[async_trait]
impl InputHandler for MouseHandler {
    async fn handle_input_event(
        &mut self,
        input_event: input_device::InputEvent,
    ) -> Vec<input_device::InputEvent> {
        match input_event {
            input_device::InputEvent {
                device_event: input_device::InputDeviceEvent::Mouse(mouse_event),
                device_descriptor: input_device::InputDeviceDescriptor::Mouse(mouse_descriptor),
            } => {
                self.update_cursor_position(&mouse_event).await;
                self.send_events_to_scenic(
                    mouse_event.phase,
                    &mouse_event.buttons,
                    &mouse_descriptor,
                )
                .await;
                vec![]
            }
            _ => vec![input_event],
        }
    }
}

impl MouseHandler {
    /// Creates a new [`MouseHandler `] that sends pointer events to Scenic and tracks cursor
    /// position.
    ///
    /// # Parameters
    /// - `max_cursor_location`: The maximum cursor location, used to bound events sent to clients.
    /// - `cursor_location_sender`: A [`Sender`] used to communicate the current cursor location.
    /// - `scenic_session`: The Scenic session to send pointer events to.
    /// - `scenic_compositor_id`: The Scenic compositor id to tag pointer events with.
    pub fn new(
        max_cursor_location: CursorLocation,
        cursor_location_sender: Option<Sender<CursorLocation>>,
        scenic_session: scenic::SessionPtr,
        scenic_compositor_id: u32,
    ) -> MouseHandler {
        MouseHandler {
            cursor_location_sender,
            current_cursor_location: CursorLocation { x: 0.0, y: 0.0 },
            max_cursor_location,
            scenic_session,
            scenic_compositor_id,
        }
    }

    /// Updates the current cursor position according to the received mouse event.
    ///
    /// The updated cursor location is sent to a client via `self.cursor_location_sender`.
    ///
    /// If there is no movement, the location is not sent to clients.
    ///
    /// # Parameters
    /// - `mouse_event`: The mouse event to use to update the cursor location.
    async fn update_cursor_position(&mut self, mouse_event: &mouse::MouseEvent) {
        if mouse_event.movement_x == 0 && mouse_event.movement_y == 0 {
            return;
        }

        if let Some(cursor_location_sender) = &mut self.cursor_location_sender {
            self.current_cursor_location.x += mouse_event.movement_x as f32;
            self.current_cursor_location.y += mouse_event.movement_y as f32;

            if self.current_cursor_location.x > self.max_cursor_location.x {
                self.current_cursor_location.x = self.max_cursor_location.x;
            }
            if self.current_cursor_location.y > self.max_cursor_location.y {
                self.current_cursor_location.y = self.max_cursor_location.y;
            }

            if self.current_cursor_location.x < 0.0 {
                self.current_cursor_location.x = 0.0;
            }
            if self.current_cursor_location.y < 0.0 {
                self.current_cursor_location.y = 0.0;
            }

            let _ = cursor_location_sender.send(self.current_cursor_location).await;
        }
    }

    /// Sends a pointer event with the given phase and buttons to Scenic.
    ///
    /// # Parameters
    /// - `phase`: The phase of the buttons associated with the mouse event.
    /// - `buttons`: The buttons associated with the event.
    async fn send_events_to_scenic(
        &self,
        phase: fidl_ui_input::PointerEventPhase,
        buttons: &HashSet<mouse::MouseButton>,
        device_descriptor: &mouse::MouseDeviceDescriptor,
    ) {
        let buttons = mouse::get_u32_from_buttons(buttons);

        let pointer_event = fidl_ui_input::PointerEvent {
            event_time: Time::get(ClockId::Monotonic).into_nanos() as u64,
            device_id: device_descriptor.device_id,
            pointer_id: 0,
            type_: fidl_ui_input::PointerEventType::Mouse,
            phase,
            x: self.current_cursor_location.x,
            y: self.current_cursor_location.y,
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
}

#[cfg(test)]
mod tests {
    use {
        super::*, crate::testing_utilities::create_mouse_event,
        fidl_fuchsia_ui_scenic as fidl_ui_scenic, fuchsia_async as fasync, futures::StreamExt,
    };

    const SCENIC_COMPOSITOR_ID: u32 = 1;
    const SCENIC_DISPLAY_WIDTH: f32 = 100.0;
    const SCENIC_DISPLAY_HEIGHT: f32 = 150.0;

    fn mouse_device_descriptor(device_id: u32) -> input_device::InputDeviceDescriptor {
        input_device::InputDeviceDescriptor::Mouse(mouse::MouseDeviceDescriptor { device_id })
    }

    /// Creates a PointerEvent with the given parameters.
    ///
    /// # Parameters
    /// - `x`: The x location of the event.
    /// - `y`: The y location of the event.
    /// - `phase`: The phase of the event.
    fn create_pointer_event(
        x: f32,
        y: f32,
        phase: fidl_ui_input::PointerEventPhase,
        device_id: u32,
    ) -> fidl_ui_input::PointerEvent {
        fidl_ui_input::PointerEvent {
            event_time: Time::get(ClockId::Monotonic).into_nanos() as u64,
            device_id,
            pointer_id: 0,
            type_: fidl_ui_input::PointerEventType::Mouse,
            phase,
            x: x as f32,
            y: y as f32,
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
                            event_time: _,
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

    // Tests that a mouse move event both sends an update to scenic and sends the current cursor
    // location via the cursor location sender.
    #[fasync::run_singlethreaded(test)]
    async fn move_event() {
        const DEVICE_ID: u32 = 1;

        let (session_proxy, mut session_request_stream) =
            fidl::endpoints::create_proxy_and_stream::<fidl_ui_scenic::SessionMarker>()
                .expect("Failed to create ScenicProxy and stream.");
        let scenic_session: scenic::SessionPtr = scenic::Session::new(session_proxy);

        let (sender, mut receiver) = futures::channel::mpsc::channel(1);

        let mut mouse_handler = MouseHandler::new(
            CursorLocation { x: SCENIC_DISPLAY_WIDTH, y: SCENIC_DISPLAY_HEIGHT },
            Some(sender),
            scenic_session.clone(),
            SCENIC_COMPOSITOR_ID,
        );

        const CURSOR_X_MOVEMENT: i64 = 50;
        const CURSOR_Y_MOVEMENT: i64 = 75;
        let descriptor = mouse_device_descriptor(DEVICE_ID);
        let input_events = vec![create_mouse_event(
            CURSOR_X_MOVEMENT,
            CURSOR_Y_MOVEMENT,
            fidl_ui_input::PointerEventPhase::Move,
            HashSet::<mouse::MouseButton>::new(),
            &descriptor,
        )];

        let expected_commands = vec![create_pointer_event(
            CURSOR_X_MOVEMENT as f32,
            CURSOR_Y_MOVEMENT as f32,
            fidl_ui_input::PointerEventPhase::Move,
            DEVICE_ID,
        )];

        assert_input_event_sequence_generates_scenic_events!(
            input_handler: mouse_handler,
            input_events: input_events,
            expected_commands: expected_commands,
            scenic_session_request_stream: session_request_stream,
            assert_command: verify_pointer_event,
        );

        let expected_cursor_location =
            CursorLocation { x: CURSOR_X_MOVEMENT as f32, y: CURSOR_Y_MOVEMENT as f32 };
        match receiver.next().await {
            Some(cursor_location) => assert_eq!(cursor_location, expected_cursor_location),
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

        let mut mouse_handler = MouseHandler::new(
            CursorLocation { x: SCENIC_DISPLAY_WIDTH, y: SCENIC_DISPLAY_HEIGHT },
            Some(sender),
            scenic_session.clone(),
            SCENIC_COMPOSITOR_ID,
        );

        const CURSOR_X_MOVEMENT: f32 = SCENIC_DISPLAY_WIDTH + 2.0;
        const CURSOR_Y_MOVEMENT: f32 = SCENIC_DISPLAY_HEIGHT + 2.0;
        let descriptor = mouse_device_descriptor(DEVICE_ID);
        let input_events = vec![create_mouse_event(
            CURSOR_X_MOVEMENT as i64,
            CURSOR_Y_MOVEMENT as i64,
            fidl_ui_input::PointerEventPhase::Move,
            HashSet::<mouse::MouseButton>::new(),
            &descriptor,
        )];

        let expected_commands = vec![create_pointer_event(
            SCENIC_DISPLAY_WIDTH,
            SCENIC_DISPLAY_HEIGHT,
            fidl_ui_input::PointerEventPhase::Move,
            DEVICE_ID,
        )];

        assert_input_event_sequence_generates_scenic_events!(
            input_handler: mouse_handler,
            input_events: input_events,
            expected_commands: expected_commands,
            scenic_session_request_stream: session_request_stream,
            assert_command: verify_pointer_event,
        );

        let expected_cursor_location =
            CursorLocation { x: SCENIC_DISPLAY_WIDTH, y: SCENIC_DISPLAY_HEIGHT };
        match receiver.next().await {
            Some(cursor_location) => assert_eq!(cursor_location, expected_cursor_location),
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

        let mut mouse_handler = MouseHandler::new(
            CursorLocation { x: SCENIC_DISPLAY_WIDTH, y: SCENIC_DISPLAY_HEIGHT },
            Some(sender),
            scenic_session.clone(),
            SCENIC_COMPOSITOR_ID,
        );

        const CURSOR_X_MOVEMENT: i64 = -20;
        const CURSOR_Y_MOVEMENT: i64 = -15;
        let descriptor = mouse_device_descriptor(DEVICE_ID);
        let input_events = vec![create_mouse_event(
            CURSOR_X_MOVEMENT,
            CURSOR_Y_MOVEMENT,
            fidl_ui_input::PointerEventPhase::Move,
            HashSet::<mouse::MouseButton>::new(),
            &descriptor,
        )];

        let expected_commands =
            vec![create_pointer_event(0.0, 0.0, fidl_ui_input::PointerEventPhase::Move, DEVICE_ID)];

        assert_input_event_sequence_generates_scenic_events!(
            input_handler: mouse_handler,
            input_events: input_events,
            expected_commands: expected_commands,
            scenic_session_request_stream: session_request_stream,
            assert_command: verify_pointer_event,
        );

        let expected_cursor_location = CursorLocation { x: 0.0, y: 0.0 };
        match receiver.next().await {
            Some(cursor_location) => assert_eq!(cursor_location, expected_cursor_location),
            _ => assert!(false),
        };
    }
}
