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
    input::touch,
    std::sync::Arc,
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
    /// The width of the display, used to compute the sent touch location.
    display_width: f32,

    /// The height of the display, used to compute the sent touch location.
    display_height: f32,

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
            } => {
                self.handle_touch_event(touch_event, touch_device_descriptor).await;
            }
            _ => {}
        }
        vec![input_event]
    }
}

impl TouchPointerHack {
    pub fn new(
        display_width: f32,
        display_height: f32,
        event_scale: f32,
        listeners: Arc<Mutex<Vec<PointerCaptureListenerHackProxy>>>,
    ) -> Self {
        TouchPointerHack { display_width, display_height, event_scale, listeners }
    }

    async fn handle_touch_event(
        &self,
        touch_event: &touch::TouchEvent,
        touch_descriptor: &touch::TouchDeviceDescriptor,
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
                let mut command =
                    self.create_pointer_input_command(phase, contact, &touch_descriptor);
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
        touch_descriptor: &touch::TouchDeviceDescriptor,
    ) -> fidl_ui_input::PointerEvent {
        let (x, y) = self.device_coordinate_from_contact(&contact, &touch_descriptor);

        fidl_ui_input::PointerEvent {
            event_time: Time::get(ClockId::Monotonic).into_nanos() as u64,
            device_id: touch_descriptor.device_id,
            pointer_id: contact.id,
            type_: fidl_ui_input::PointerEventType::Touch,
            phase,
            x: x * self.event_scale,
            y: y * self.event_scale,
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
