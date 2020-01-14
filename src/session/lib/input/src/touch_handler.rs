// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::input_device,
    crate::input_handler::InputHandler,
    crate::touch,
    anyhow::{format_err, Error},
    async_trait::async_trait,
    fidl_fuchsia_ui_input as fidl_ui_input, fidl_fuchsia_ui_scenic as fidl_ui_scenic,
    fuchsia_scenic as scenic,
    fuchsia_zircon::{ClockId, Time},
};

/// An input handler that parses touch events and forwards them to Scenic.
pub struct TouchHandler {
    /// The Scenic session to send events to.
    scenic_session: scenic::SessionPtr,

    /// The Scenic compositor id to tag input events with.
    scenic_compositor_id: u32,

    /// The width of the display associated with the touch device, used to convert
    /// coordinates from the touch input report to device coordinates (which is what
    /// Scenic expects).
    display_width: i64,

    /// The height of the display associated with the touch device, used to convert
    /// coordinates from the touch input report to device coordinates (which is what
    /// Scenic expects).
    display_height: i64,
}

#[async_trait]
impl InputHandler for TouchHandler {
    async fn handle_input_event(
        &mut self,
        input_event: input_device::InputEvent,
    ) -> Vec<input_device::InputEvent> {
        match input_event {
            input_device::InputEvent {
                device_event: input_device::InputDeviceEvent::Touch(touch_event),
                device_descriptor:
                    input_device::InputDeviceDescriptor::Touch(touch_device_descriptor),
            } => {
                self.handle_touch_event(touch_event, touch_device_descriptor);
                // Consume the event (i.e., don't forward it to the next handler).
                vec![]
            }
            // Don't consume the event (i.e., forward it to the next handler).
            input_event => vec![input_event],
        }
    }
}

impl TouchHandler {
    /// Creates a new touch handler that sends events to the given Scenic session.
    ///
    /// # Parameters
    /// - `scenic_session`: The Scenic session to send events to.
    /// - `scenic_compositor_id`: The compositor id to tag input events with.
    /// - `display_width`: The width of the associated touch display,
    /// used to convert coordinates into device coordinates. Can't be 0.
    /// - `display_height`: The height of the associated touch display,
    /// used to convert coordinates into device coordinates. Can't be 0.
    ///
    /// # Errors
    /// If the display height or width is 0.
    pub async fn new(
        scenic_session: scenic::SessionPtr,
        scenic_compositor_id: u32,
        display_width: i64,
        display_height: i64,
    ) -> Result<Self, Error> {
        match (display_width, display_height) {
            (0, _) | (_, 0) => {
                Err(format_err!("Display height: {} and width: {} are required to be non-zero."))
            }
            _ => Ok(TouchHandler {
                scenic_session,
                scenic_compositor_id,
                display_width,
                display_height,
            }),
        }
    }

    /// Handles the given event and sends it to Scenic.
    ///
    /// # Parameters
    /// - `touch_event`: The touch event to send to Scenic.
    /// - `touch_descriptor`: The descriptor for the device that sent the touch event.
    fn handle_touch_event(
        &self,
        touch_event: touch::TouchEvent,
        touch_descriptor: touch::TouchDeviceDescriptor,
    ) {
        // The order in which events are sent to clients.
        let ordered_phases = vec![
            fidl_ui_input::PointerEventPhase::Add,
            fidl_ui_input::PointerEventPhase::Down,
            fidl_ui_input::PointerEventPhase::Move,
            fidl_ui_input::PointerEventPhase::Up,
            fidl_ui_input::PointerEventPhase::Remove,
        ];

        let mut locked_session = self.scenic_session.lock();
        for phase in ordered_phases {
            let contacts: Vec<touch::TouchContact> =
                touch_event.contacts.get(&phase).map_or(vec![], |contacts| contacts.to_vec());
            for contact in contacts {
                let command = self.create_pointer_input_command(phase, contact, &touch_descriptor);
                locked_session.enqueue(command);
            }
        }
        locked_session.flush();
    }

    /// Creates a [`fidl_ui_scenic::Command`] representing the given touch contact.
    ///
    /// # Parameters
    /// - `phase`: The phase of the touch contact.
    /// - `contact`: The touch contact to create the event for.
    /// - `touch_descriptor`: The device descriptor for the device that generated the event.
    fn create_pointer_input_command(
        &self,
        phase: fidl_ui_input::PointerEventPhase,
        contact: touch::TouchContact,
        touch_descriptor: &touch::TouchDeviceDescriptor,
    ) -> fidl_ui_scenic::Command {
        let (x, y) = self.device_coordinate_from_contact(&contact, &touch_descriptor);

        let pointer_event = fidl_ui_input::PointerEvent {
            event_time: Time::get(ClockId::Monotonic).into_nanos() as u64,
            device_id: touch_descriptor.device_id,
            pointer_id: contact.id,
            type_: fidl_ui_input::PointerEventType::Touch,
            phase,
            x,
            y,
            radius_major: 0.0,
            radius_minor: 0.0,
            buttons: 0,
        };

        let pointer_command = fidl_ui_input::SendPointerInputCmd {
            compositor_id: self.scenic_compositor_id,
            pointer_event,
        };
        let send_pointer_command = fidl_ui_input::Command::SendPointerInput(pointer_command);
        fidl_ui_scenic::Command::Input(send_pointer_command)
    }

    /// Converts an input event touch to a display coordinate, which is the coordinate space in
    /// which Scenic handles events.
    ///
    /// # Parameters
    /// - `contact`: The contact to get the display coordinate from.
    /// - `touch_descriptor`: The device descriptor for the device that generated the event.
    /// This is used to compute the device coordinate.
    ///
    /// # Returns
    /// (x, y) coordinates.
    fn device_coordinate_from_contact(
        &self,
        contact: &touch::TouchContact,
        touch_descriptor: &touch::TouchDeviceDescriptor,
    ) -> (f32, f32) {
        let default = (contact.position_x as f32, contact.position_y as f32);
        if let Some(contact_descriptor) = touch_descriptor.contacts.first() {
            let x_range = (contact_descriptor.x_range.max - contact_descriptor.x_range.min) as f32;
            let y_range = (contact_descriptor.y_range.max - contact_descriptor.y_range.min) as f32;

            if x_range == 0.0 || y_range == 0.0 {
                return default;
            }

            let normalized_x = contact.position_x as f32 / x_range;
            let normalized_y = contact.position_y as f32 / y_range;
            (normalized_x * self.display_width as f32, normalized_y * self.display_height as f32)
        } else {
            return default;
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::testing_utilities::{create_touch_contact, create_touch_event},
        fidl_fuchsia_input_report as fidl_input_report, fidl_fuchsia_ui_scenic as fidl_ui_scenic,
        fuchsia_async as fasync,
        futures::StreamExt,
        maplit::hashmap,
    };

    const SCENIC_COMPOSITOR_ID: u32 = 1;
    const SCENIC_DISPLAY_WIDTH: i64 = 100;
    const SCENIC_DISPLAY_HEIGHT: i64 = 100;

    /// Returns an TouchDescriptor.
    fn get_touch_device_descriptor() -> input_device::InputDeviceDescriptor {
        input_device::InputDeviceDescriptor::Touch(touch::TouchDeviceDescriptor {
            device_id: 1,
            contacts: vec![touch::ContactDeviceDescriptor {
                x_range: fidl_input_report::Range { min: 0, max: 100 },
                y_range: fidl_input_report::Range { min: 0, max: 100 },
                pressure_range: None,
                width_range: None,
                height_range: None,
            }],
        })
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
    ) -> fidl_ui_input::PointerEvent {
        fidl_ui_input::PointerEvent {
            event_time: Time::get(ClockId::Monotonic).into_nanos() as u64,
            device_id: 1,
            pointer_id: 1,
            type_: fidl_ui_input::PointerEventType::Touch,
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
                            type_: _,
                            phase,
                            x,
                            y,
                            radius_major: _,
                            radius_minor: _,
                            buttons,
                        },
                },
            )) => {
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

    // Tests that add and down events are handled in order.
    #[fasync::run_singlethreaded(test)]
    async fn add_and_down() {
        const TOUCH_ID: u32 = 1;
        let (session_proxy, mut session_request_stream) =
            fidl::endpoints::create_proxy_and_stream::<fidl_ui_scenic::SessionMarker>()
                .expect("Failed to create ScenicProxy and stream.");
        let scenic_session: scenic::SessionPtr = scenic::Session::new(session_proxy);
        let mut touch_handler = TouchHandler::new(
            scenic_session.clone(),
            SCENIC_COMPOSITOR_ID,
            SCENIC_DISPLAY_WIDTH,
            SCENIC_DISPLAY_HEIGHT,
        )
        .await
        .expect("Failed to create TouchHandler.");

        let descriptor = get_touch_device_descriptor();
        let input_events = vec![create_touch_event(
            hashmap! {
                fidl_ui_input::PointerEventPhase::Add
                    => vec![create_touch_contact(TOUCH_ID, 20, 40)],
                fidl_ui_input::PointerEventPhase::Down
                    => vec![create_touch_contact(TOUCH_ID, 20, 40)],
            },
            &descriptor,
        )];

        let expected_commands = vec![
            create_pointer_event(20.0, 40.0, fidl_ui_input::PointerEventPhase::Add),
            create_pointer_event(20.0, 40.0, fidl_ui_input::PointerEventPhase::Down),
        ];

        assert_input_event_sequence_generates_scenic_events!(
            input_handler: touch_handler,
            input_events: input_events,
            expected_commands: expected_commands,
            scenic_session_request_stream: session_request_stream,
            assert_command: verify_pointer_event,
        );
    }

    // Tests that up and remove events are handled in order.
    #[fasync::run_singlethreaded(test)]
    async fn up_and_remove() {
        const TOUCH_ID: u32 = 1;
        let (session_proxy, mut session_request_stream) =
            fidl::endpoints::create_proxy_and_stream::<fidl_ui_scenic::SessionMarker>()
                .expect("Failed to create ScenicProxy and stream.");
        let scenic_session: scenic::SessionPtr = scenic::Session::new(session_proxy);
        let mut touch_handler = TouchHandler::new(
            scenic_session.clone(),
            SCENIC_COMPOSITOR_ID,
            SCENIC_DISPLAY_WIDTH,
            SCENIC_DISPLAY_HEIGHT,
        )
        .await
        .expect("Failed to create TouchHandler.");

        let descriptor = get_touch_device_descriptor();
        let input_events = vec![create_touch_event(
            hashmap! {
                fidl_ui_input::PointerEventPhase::Up
                    => vec![create_touch_contact(TOUCH_ID, 20, 40)],
                fidl_ui_input::PointerEventPhase::Remove
                    => vec![create_touch_contact(TOUCH_ID, 20, 40)],
            },
            &descriptor,
        )];

        let expected_commands = vec![
            create_pointer_event(20.0, 40.0, fidl_ui_input::PointerEventPhase::Up),
            create_pointer_event(20.0, 40.0, fidl_ui_input::PointerEventPhase::Remove),
        ];

        assert_input_event_sequence_generates_scenic_events!(
            input_handler: touch_handler,
            input_events: input_events,
            expected_commands: expected_commands,
            scenic_session_request_stream: session_request_stream,
            assert_command: verify_pointer_event,
        );
    }

    // Tests that add, down, and move events are handled in order.
    #[fasync::run_singlethreaded(test)]
    async fn add_down_move() {
        const TOUCH_ID: u32 = 1;
        const X: i64 = 20;
        const Y: i64 = 40;
        let (session_proxy, mut session_request_stream) =
            fidl::endpoints::create_proxy_and_stream::<fidl_ui_scenic::SessionMarker>()
                .expect("Failed to create ScenicProxy and stream.");
        let scenic_session: scenic::SessionPtr = scenic::Session::new(session_proxy);
        let mut touch_handler = TouchHandler::new(
            scenic_session.clone(),
            SCENIC_COMPOSITOR_ID,
            SCENIC_DISPLAY_WIDTH,
            SCENIC_DISPLAY_HEIGHT,
        )
        .await
        .expect("Failed to create TouchHandler.");

        let descriptor = get_touch_device_descriptor();
        let input_events = vec![create_touch_event(
            hashmap! {
                fidl_ui_input::PointerEventPhase::Add
                    => vec![create_touch_contact(TOUCH_ID, X, Y)],
                fidl_ui_input::PointerEventPhase::Down
                    => vec![create_touch_contact(TOUCH_ID, X, Y)],
                fidl_ui_input::PointerEventPhase::Move
                    => vec![create_touch_contact(TOUCH_ID, X*2, Y*2)]
            },
            &descriptor,
        )];

        let expected_commands = vec![
            create_pointer_event(X as f32, Y as f32, fidl_ui_input::PointerEventPhase::Add),
            create_pointer_event(X as f32, Y as f32, fidl_ui_input::PointerEventPhase::Down),
            create_pointer_event(
                (X * 2) as f32,
                (Y * 2) as f32,
                fidl_ui_input::PointerEventPhase::Move,
            ),
        ];

        assert_input_event_sequence_generates_scenic_events!(
            input_handler: touch_handler,
            input_events: input_events,
            expected_commands: expected_commands,
            scenic_session_request_stream: session_request_stream,
            assert_command: verify_pointer_event,
        );
    }
}
