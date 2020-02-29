// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    async_trait::async_trait, fidl_fuchsia_ui_input as fidl_ui_input,
    fidl_fuchsia_ui_policy::PointerCaptureListenerHackProxy, futures::lock::Mutex,
    input::input_device, input::input_handler::InputHandler, input::touch, std::sync::Arc, input::{Position, Size}
};

/// A [`TouchPointerHack`] observes touch events and sends them to observers.
///
/// Ermine cannot receive pointer events from Scenic for subviews (i.e., Flutter
/// applications it embeds), so this handler is used to send all pointer events directly
/// to Ermine.
///
/// Once Ermine has a way to observe these events directly from Scenic, this handler
/// can be removed.
pub struct TouchPointerHack {
    /// The size of the display, used to compute the sent touch location.
    display_size: Size,

    /// The scale to apply to touch event before sending to listeners.
    event_scale: f32,

    /// The listeners to notify of touch events.
    listeners: Arc<Mutex<Vec<PointerCaptureListenerHackProxy>>>,
}

#[async_trait]
impl InputHandler for TouchPointerHack {
    async fn handle_input_event(
        &mut self,
        input_event: input_device::InputEvent,
    ) -> Vec<input_device::InputEvent> {
        match &input_event {
            input_device::InputEvent {
                device_event: input_device::InputDeviceEvent::Touch(touch_event),
                device_descriptor:
                    input_device::InputDeviceDescriptor::Touch(touch_device_descriptor),
                event_time,
            } => {
                self.handle_touch_event(touch_event, touch_device_descriptor, *event_time).await;
            }
            _ => {}
        }
        vec![input_event]
    }
}

impl TouchPointerHack {
    pub fn new(
        display_size: Size,
        event_scale: f32,
        listeners: Arc<Mutex<Vec<PointerCaptureListenerHackProxy>>>,
    ) -> Self {
        TouchPointerHack { display_size, event_scale, listeners }
    }

    async fn handle_touch_event(
        &self,
        touch_event: &touch::TouchEvent,
        touch_descriptor: &touch::TouchDeviceDescriptor,
        event_time: input_device::EventTime,
    ) {
        // The order in which events are sent to clients.
        let ordered_phases = vec![
            fidl_ui_input::PointerEventPhase::Add,
            fidl_ui_input::PointerEventPhase::Down,
            fidl_ui_input::PointerEventPhase::Move,
            fidl_ui_input::PointerEventPhase::Up,
            fidl_ui_input::PointerEventPhase::Remove,
        ];

        for phase in ordered_phases {
            let contacts: Vec<touch::TouchContact> =
                touch_event.contacts.get(&phase).map_or(vec![], |contacts| contacts.to_vec());
            for contact in contacts {
                let mut command = self.create_pointer_input_command(
                    phase,
                    contact,
                    event_time,
                    &touch_descriptor,
                );
                let listeners = self.listeners.lock().await;
                for listener in &mut listeners.iter() {
                    let listener = listener;
                    let _ = listener.on_pointer_event(&mut command);
                }
            }
        }
    }

    /// Creates a pointer input command to send to listeners.
    fn create_pointer_input_command(
        &self,
        phase: fidl_ui_input::PointerEventPhase,
        contact: touch::TouchContact,
        event_time: input_device::EventTime,
        touch_descriptor: &touch::TouchDeviceDescriptor,
    ) -> fidl_ui_input::PointerEvent {
        let position = self.device_coordinate_from_contact(&contact, &touch_descriptor) * self.event_scale;

        fidl_ui_input::PointerEvent {
            event_time: event_time,
            device_id: touch_descriptor.device_id,
            pointer_id: contact.id,
            type_: fidl_ui_input::PointerEventType::Touch,
            phase,
            x: position.x,
            y: position.y,
            radius_major: 0.0,
            radius_minor: 0.0,
            buttons: 0,
        }
    }

    /// Converts a touch coordinate in device coordinates into a display coordinate.
    ///
    /// For example, if a touch device reports x in a range [0, 100], and the display
    /// is [0, 200] pixels, a touch at [0, 75] would be converted to [0, 150].
    fn device_coordinate_from_contact(
        &self,
        contact: &touch::TouchContact,
        touch_descriptor: &touch::TouchDeviceDescriptor,
    ) -> Position {
        if let Some(contact_descriptor) = touch_descriptor.contacts.first() {
            let range = Position {
                x: (contact_descriptor.x_range.max - contact_descriptor.x_range.min) as f32,
                y: (contact_descriptor.y_range.max - contact_descriptor.y_range.min) as f32,
            };

            if range.x == 0.0 || range.y == 0.0 {
                return contact.position();
            }

            let normalized = contact.position() / range;
            normalized * self.display_size
        } else {
            return contact.position();
        }
    }
}
