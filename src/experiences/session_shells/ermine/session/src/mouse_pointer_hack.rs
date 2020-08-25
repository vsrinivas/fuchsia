// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    async_trait::async_trait,
    fidl_fuchsia_ui_input as fidl_ui_input,
    fidl_fuchsia_ui_policy::PointerCaptureListenerHackProxy,
    futures::lock::Mutex,
    input::input_device,
    input::input_handler::InputHandler,
    input::mouse,
    input::{Position, Size},
    std::collections::HashSet,
    std::sync::Arc,
};

/// A [`MousePointerHack`] tracks the mouse position and sends it to observers.
///
/// Ermine cannot receive pointer events from Scenic for subviews (i.e., Flutter
/// applications it embeds), so this handler is used to send all pointer events directly
/// to Ermine.
///
/// TODO(fxbug.dev/58145): Remove this handler once Ermine has a way to observe these
/// events directly from Scenic.
pub struct MousePointerHack {
    /// The current cursor location.
    current_position: Position,

    /// The maximum cursor position, used to bound events sent to clients.
    screen_size: Size,

    /// The scale factor to be applied to cursor events before sending to listeners.
    event_scale: f32,

    /// The listeners which will receive pointer events.
    listeners: Arc<Mutex<Vec<PointerCaptureListenerHackProxy>>>,
}

#[async_trait]
impl InputHandler for MousePointerHack {
    async fn handle_input_event(
        &mut self,
        input_event: input_device::InputEvent,
    ) -> Vec<input_device::InputEvent> {
        match &input_event {
            input_device::InputEvent {
                device_event: input_device::InputDeviceEvent::Mouse(mouse_event),
                device_descriptor: input_device::InputDeviceDescriptor::Mouse(mouse_descriptor),
                event_time,
            } => {
                self.update_cursor_position(&mouse_event).await;
                self.send_event_to_listeners(
                    mouse_event.phase,
                    &mouse_event.buttons,
                    *event_time,
                    &mouse_descriptor,
                )
                .await;
            }
            _ => {}
        }
        vec![input_event]
    }
}

impl MousePointerHack {
    /// Creates a new [`MousePointerHack`] that sends pointer events to Ermine.
    ///
    /// # Parameters
    /// - `screen_size`: The maximum position to cap mouse movement at.
    /// - `event_scale`: The scale to apply to pointer events before sending them to listeners.
    /// - `listeners`: The listeners that will get notified of mouse events.
    pub fn new(
        screen_size: Size,
        event_scale: f32,
        listeners: Arc<Mutex<Vec<PointerCaptureListenerHackProxy>>>,
    ) -> MousePointerHack {
        MousePointerHack {
            current_position: Position { x: 0.0, y: 0.0 },
            screen_size,
            event_scale,
            listeners,
        }
    }

    /// Updates the current cursor position according to the received mouse event.
    ///
    /// # Parameters
    /// - `mouse_event`: The mouse event to use to update the cursor position.
    async fn update_cursor_position(&mut self, mouse_event: &mouse::MouseEvent) {
        self.current_position = match mouse_event.location {
            mouse::MouseLocation::Relative(offset) if offset != Position::zero() => {
                self.current_position + offset
            }
            mouse::MouseLocation::Absolute(position) if position != self.current_position => {
                position
            }
            _ => return,
        };

        Position::clamp_size(&mut self.current_position, Size::zero(), self.screen_size)
    }

    /// Sends a pointer event with the given phase and buttons to listeners.
    ///
    /// # Parameters
    /// - `phase`: The phase of the buttons associated with the mouse event.
    /// - `buttons`: The buttons associated with the event.
    /// - `event_time`: The time the event was first reported.
    /// - `device_descriptor`: The descriptor for the device that generated the event.
    async fn send_event_to_listeners(
        &self,
        phase: fidl_ui_input::PointerEventPhase,
        buttons: &HashSet<mouse::MouseButton>,
        event_time: input_device::EventTime,
        device_descriptor: &mouse::MouseDeviceDescriptor,
    ) {
        let buttons = mouse::get_u32_from_buttons(buttons);
        let scaled_position = self.current_position * self.event_scale;

        let mut pointer = fidl_ui_input::PointerEvent {
            event_time,
            device_id: device_descriptor.device_id,
            pointer_id: 0,
            type_: fidl_ui_input::PointerEventType::Mouse,
            phase,
            // Since Ermine listens for these events it needs to receive them in Ermine's view's
            // coordinate space. Normally, Scenic would perform this scaling when traversing
            // scale nodes in the Scene graph, but here the scale needs to be done manually.
            x: scaled_position.x,
            y: scaled_position.y,
            radius_major: 0.0,
            radius_minor: 0.0,
            buttons,
        };

        let listeners = self.listeners.lock().await;
        for listener in &mut listeners.iter() {
            let _ = listener.on_pointer_event(&mut pointer);
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*, crate::input_testing_utilities::create_mouse_event,
        fidl_fuchsia_ui_policy as fidl_ui_policy, fuchsia_async as fasync, fuchsia_zircon as zx,
        futures::StreamExt, input::Position,
    };

    const DEVICE_ID: u32 = 1;
    const DISPLAY_SIZE: Size = Size { width: 100.0, height: 150.0 };

    fn mouse_device_descriptor(device_id: u32) -> input_device::InputDeviceDescriptor {
        input_device::InputDeviceDescriptor::Mouse(mouse::MouseDeviceDescriptor {
            device_id,
            absolute_x_range: None,
            absolute_y_range: None,
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
        event_time: input_device::EventTime,
    ) -> fidl_ui_input::PointerEvent {
        fidl_ui_input::PointerEvent {
            event_time,
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

    /// Validates the `event` against `expected_event`.
    ///
    /// # Parameters
    /// - `event`: The event received by the listener.
    /// - `expected_event`: The expected event.
    fn verify_pointer_event(
        event: fidl_ui_input::PointerEvent,
        expected_event: fidl_ui_input::PointerEvent,
    ) {
        assert_eq!(event.event_time, expected_event.event_time);
        assert_eq!(event.device_id, expected_event.device_id);
        assert_eq!(event.pointer_id, expected_event.pointer_id);
        assert_eq!(event.type_, fidl_ui_input::PointerEventType::Mouse);
        assert_eq!(event.phase, expected_event.phase);
        assert_eq!(event.x, expected_event.x);
        assert_eq!(event.y, expected_event.y);
        // event.radius_major is not applicable for mouse events.
        // event.radius_minor is not applicable for mouse events.
        assert_eq!(event.buttons, expected_event.buttons);
    }

    // Tests that two relative mouse move events send PointerEvents to the listener with
    // a position that reflects the final position.
    #[fasync::run_until_stalled(test)]
    async fn relative_move() {
        const EVENT_SCALE: f32 = 1.0;

        let (listener_proxy, mut listener_request_stream) =
            fidl::endpoints::create_proxy_and_stream::<
                fidl_fuchsia_ui_policy::PointerCaptureListenerHackMarker,
            >()
            .expect("Failed to create PointerCaptureListenerHackProxy and stream.");

        let listeners = Arc::new(Mutex::new(vec![listener_proxy]));

        let mut mouse_pointer_hack = MousePointerHack::new(DISPLAY_SIZE, EVENT_SCALE, listeners);

        let first_cursor_position = Position { x: 5.0, y: 20.0 };
        let second_cursor_position = Position { x: 10.0, y: -10.0 };
        let descriptor = mouse_device_descriptor(DEVICE_ID);
        let event_time =
            zx::Time::get(zx::ClockId::Monotonic).into_nanos() as input_device::EventTime;
        let input_events = vec![
            create_mouse_event(
                mouse::MouseLocation::Relative(first_cursor_position),
                fidl_ui_input::PointerEventPhase::Move,
                HashSet::<mouse::MouseButton>::new(),
                event_time,
                &descriptor,
            ),
            create_mouse_event(
                mouse::MouseLocation::Relative(second_cursor_position),
                fidl_ui_input::PointerEventPhase::Move,
                HashSet::<mouse::MouseButton>::new(),
                event_time,
                &descriptor,
            ),
        ];

        let first_cursor_expected_position = first_cursor_position;
        let second_cursor_expected_position = first_cursor_position + second_cursor_position;

        let expected_events = vec![
            create_pointer_event(
                first_cursor_expected_position,
                fidl_ui_input::PointerEventPhase::Move,
                DEVICE_ID,
                event_time,
            ),
            create_pointer_event(
                second_cursor_expected_position,
                fidl_ui_input::PointerEventPhase::Move,
                DEVICE_ID,
                event_time,
            ),
        ];

        assert_input_event_sequence_generates_pointer_hack_events!(
            input_handler: mouse_pointer_hack,
            input_events: input_events,
            expected_events: expected_events,
            listener_request_stream: listener_request_stream,
            assert_event: verify_pointer_event,
        );
    }

    // Tests that a mouse move event sends a PointerEvent to the listener with a scaled position.
    #[fasync::run_until_stalled(test)]
    async fn move_with_scale() {
        const EVENT_SCALE: f32 = 2.0;

        let (listener_proxy, mut listener_request_stream) =
            fidl::endpoints::create_proxy_and_stream::<
                fidl_fuchsia_ui_policy::PointerCaptureListenerHackMarker,
            >()
            .expect("Failed to create PointerCaptureListenerHackProxy and stream.");

        let listeners = Arc::new(Mutex::new(vec![listener_proxy]));

        let mut mouse_pointer_hack = MousePointerHack::new(DISPLAY_SIZE, EVENT_SCALE, listeners);

        let cursor_relative_position = Position { x: 50.0, y: 75.0 };
        let descriptor = mouse_device_descriptor(DEVICE_ID);
        let event_time =
            zx::Time::get(zx::ClockId::Monotonic).into_nanos() as input_device::EventTime;
        let input_events = vec![create_mouse_event(
            mouse::MouseLocation::Relative(cursor_relative_position),
            fidl_ui_input::PointerEventPhase::Move,
            HashSet::<mouse::MouseButton>::new(),
            event_time,
            &descriptor,
        )];

        let cursor_expected_position = Position {
            x: cursor_relative_position.x * EVENT_SCALE,
            y: cursor_relative_position.y * EVENT_SCALE,
        };

        let expected_events = vec![create_pointer_event(
            cursor_expected_position,
            fidl_ui_input::PointerEventPhase::Move,
            DEVICE_ID,
            event_time,
        )];

        assert_input_event_sequence_generates_pointer_hack_events!(
            input_handler: mouse_pointer_hack,
            input_events: input_events,
            expected_events: expected_events,
            listener_request_stream: listener_request_stream,
            assert_event: verify_pointer_event,
        );
    }

    // Tests that absolute mouse move events send PointerEvents to the listener
    // with positions that reflect absolute movement.
    #[fasync::run_until_stalled(test)]
    async fn absolute_move() {
        const EVENT_SCALE: f32 = 1.0;

        let (listener_proxy, mut listener_request_stream) =
            fidl::endpoints::create_proxy_and_stream::<
                fidl_fuchsia_ui_policy::PointerCaptureListenerHackMarker,
            >()
            .expect("Failed to create PointerCaptureListenerHackProxy and stream.");

        let listeners = Arc::new(Mutex::new(vec![listener_proxy]));

        let mut mouse_pointer_hack = MousePointerHack::new(DISPLAY_SIZE, EVENT_SCALE, listeners);

        let first_cursor_position = Position { x: 50.0, y: 75.0 };
        let second_cursor_position = Position { x: 10.0, y: 20.0 };
        let descriptor = mouse_device_descriptor(DEVICE_ID);
        let event_time =
            zx::Time::get(zx::ClockId::Monotonic).into_nanos() as input_device::EventTime;

        let input_events = vec![
            create_mouse_event(
                mouse::MouseLocation::Absolute(first_cursor_position),
                fidl_ui_input::PointerEventPhase::Move,
                HashSet::<mouse::MouseButton>::new(),
                event_time,
                &descriptor,
            ),
            create_mouse_event(
                mouse::MouseLocation::Absolute(second_cursor_position),
                fidl_ui_input::PointerEventPhase::Move,
                HashSet::<mouse::MouseButton>::new(),
                event_time,
                &descriptor,
            ),
        ];

        // The expected cursor positions are the same as the input positions since
        // there is no scaling (EVENT_SCALE is 1.0) and the the movements are absolute.
        let expected_events = vec![
            create_pointer_event(
                first_cursor_position,
                fidl_ui_input::PointerEventPhase::Move,
                DEVICE_ID,
                event_time,
            ),
            create_pointer_event(
                second_cursor_position,
                fidl_ui_input::PointerEventPhase::Move,
                DEVICE_ID,
                event_time,
            ),
        ];

        assert_input_event_sequence_generates_pointer_hack_events!(
            input_handler: mouse_pointer_hack,
            input_events: input_events,
            expected_events: expected_events,
            listener_request_stream: listener_request_stream,
            assert_event: verify_pointer_event,
        );
    }
}
