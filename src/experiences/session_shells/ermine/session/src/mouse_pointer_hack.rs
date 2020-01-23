// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    async_trait::async_trait,
    fidl_fuchsia_ui_input as fidl_ui_input,
    fidl_fuchsia_ui_policy::PointerCaptureListenerHackProxy,
    fuchsia_zircon::{ClockId, Time},
    futures::lock::Mutex,
    input::input_device,
    input::input_handler::InputHandler,
    input::mouse,
    std::collections::HashSet,
    std::sync::Arc,
};

/// The location of a mouse cursor.
#[derive(Clone, Copy, Debug, PartialEq)]
struct CursorLocation {
    /// The x location of the cursor, in pixels.
    pub x: f32,

    /// The y location of the cursor, in pixels.
    pub y: f32,
}

/// A [`MousePointerHack`] tracks the mouse position and sends it to observers.
///
/// Ermine cannot receive pointer events from Scenic for subviews (i.e., Flutter
/// applications it embeds), so this handler is used to send all pointer events directly
/// to Ermine.
///
/// Once Ermine has a way to observe these events directly from Scenic, this handler
/// can be removed.
pub struct MousePointerHack {
    /// The current cursor location.
    current_cursor_location: CursorLocation,

    /// The maximum cursor location, used to bound events sent to clients.
    max_cursor_location: CursorLocation,

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
            } => {
                self.update_cursor_position(&mouse_event).await;
                self.send_event_to_listeners(
                    mouse_event.phase,
                    &mouse_event.buttons,
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
    /// Creates a new [`MousePointerHack `] that sends pointer events to Ermine.
    ///
    /// # Parameters
    /// - `max_x`: The maximum x-value to cap mouse movement at.
    /// - `max_y`: The maximum y-value to cap mouse movement at.
    /// - `event_scale`: The scale to apply to pointer events before sending them to listeners.
    /// - `listeners`: The listeners that will get notified of mouse events.
    pub fn new(
        max_x: f32,
        max_y: f32,
        event_scale: f32,
        listeners: Arc<Mutex<Vec<PointerCaptureListenerHackProxy>>>,
    ) -> MousePointerHack {
        MousePointerHack {
            current_cursor_location: CursorLocation { x: 0.0, y: 0.0 },
            max_cursor_location: CursorLocation { x: max_x, y: max_y },
            event_scale,
            listeners,
        }
    }

    /// Updates the current cursor position according to the received mouse event.
    ///
    /// # Parameters
    /// - `mouse_event`: The mouse event to use to update the cursor location.
    async fn update_cursor_position(&mut self, mouse_event: &mouse::MouseEvent) {
        if mouse_event.movement_x == 0 && mouse_event.movement_y == 0 {
            return;
        }

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
    }

    /// Sends a pointer event with the given phase and buttons to listeners.
    ///
    /// # Parameters
    /// - `phase`: The phase of the buttons associated with the mouse event.
    /// - `buttons`: The buttons associated with the event.
    /// - `device_descriptor`: The descriptor for the device that generated the event.
    async fn send_event_to_listeners(
        &self,
        phase: fidl_ui_input::PointerEventPhase,
        buttons: &HashSet<mouse::MouseButton>,
        device_descriptor: &mouse::MouseDeviceDescriptor,
    ) {
        let buttons = mouse::get_u32_from_buttons(buttons);

        let mut pointer = fidl_ui_input::PointerEvent {
            event_time: Time::get(ClockId::Monotonic).into_nanos() as u64,
            device_id: device_descriptor.device_id,
            pointer_id: 0,
            type_: fidl_ui_input::PointerEventType::Mouse,
            phase,
            // Since Ermine listens for these events it needs to receive them in Ermine's view's
            // coordinate space. Normally, Scenic would perform this scaling when traversing
            // scale nodes in the Scene graph, but here the scale needs to be done manually.
            x: self.current_cursor_location.x * self.event_scale,
            y: self.current_cursor_location.y * self.event_scale,
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
