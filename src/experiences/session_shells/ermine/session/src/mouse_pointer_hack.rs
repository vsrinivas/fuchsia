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
/// Once Ermine has a way to observe these events directly from Scenic, this handler
/// can be removed.
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
    /// Creates a new [`MousePointerHack `] that sends pointer events to Ermine.
    ///
    /// # Parameters
    /// - `max_cursor_position`: The maximum position to cap mouse movement at.
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
    /// - `mouse_event`: The mouse event to use to update the cursor location.
    async fn update_cursor_position(&mut self, mouse_event: &mouse::MouseEvent) {
        if mouse_event.movement == Position::zero() {
            return;
        }

        self.current_position += mouse_event.movement;
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
            event_time: event_time,
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
