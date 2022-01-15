// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    input::{mouse, touch, Button, ButtonSet, DeviceId, Event, EventType},
    IntPoint, IntSize,
};
use euclid::{default::Transform2D, point2};
use fidl_fuchsia_ui_pointer::{EventPhase, MouseEvent, TouchEvent};
use std::collections::HashSet;

#[derive(Default)]
pub(crate) struct FlatlandMouseInputHandler {
    pressed_mouse_buttons: HashSet<u8>,
    cursor_position: IntPoint,
    device_id: u32,
}

impl FlatlandMouseInputHandler {
    pub fn new(device_id: u32) -> Self {
        Self { device_id, ..Default::default() }
    }

    fn events_from_mouse_event(&mut self, mouse_event: &MouseEvent) -> Vec<Event> {
        let transform = Transform2D::identity();
        let event_time = mouse_event.timestamp.expect("timestamp") as u64;
        let pointer_sample = mouse_event.pointer_sample.as_ref().expect("pointer_sample");
        let position_in_viewport =
            pointer_sample.position_in_viewport.expect("position_in_viewport");
        let pressed_buttons: HashSet<u8> =
            if let Some(ref pressed_buttons) = pointer_sample.pressed_buttons {
                let pressed_buttons_set = pressed_buttons.iter().cloned().collect();
                pressed_buttons_set
            } else {
                HashSet::new()
            };
        let button_set = ButtonSet::new(&pressed_buttons);
        let device_id = &DeviceId(format!("mouse-{}", self.device_id));
        let new_cursor_position = point2(position_in_viewport[0], position_in_viewport[1]).to_i32();
        let move_event = if new_cursor_position != self.cursor_position {
            let event = mouse::create_event(
                event_time,
                device_id,
                &button_set,
                new_cursor_position,
                &transform,
                mouse::Phase::Moved,
            );
            Some(event)
        } else {
            None
        };

        self.cursor_position = new_cursor_position;

        let newly_pressed = pressed_buttons.difference(&self.pressed_mouse_buttons).map(|button| {
            mouse::create_event(
                event_time,
                device_id,
                &button_set,
                new_cursor_position,
                &transform,
                mouse::Phase::Down(Button(*button)),
            )
        });

        let released = self.pressed_mouse_buttons.difference(&pressed_buttons).map(|button| {
            mouse::create_event(
                event_time,
                device_id,
                &button_set,
                new_cursor_position,
                &transform,
                mouse::Phase::Up(Button(*button)),
            )
        });
        let events = newly_pressed.chain(move_event).chain(released).collect();
        self.pressed_mouse_buttons = pressed_buttons;
        events
    }

    pub fn handle_mouse_events(&mut self, events: &Vec<MouseEvent>) -> Vec<Event> {
        let mut converted_events = Vec::new();
        for event in events {
            converted_events.extend(self.events_from_mouse_event(event));
        }
        converted_events
    }
}

#[derive(Default)]
pub(crate) struct FlatlandTouchInputHandler {}

impl FlatlandTouchInputHandler {
    fn flatland_phase_to_phase(phase: EventPhase, location: IntPoint) -> touch::Phase {
        match phase {
            EventPhase::Add => touch::Phase::Down(location, IntSize::zero()),
            EventPhase::Change => touch::Phase::Moved(location, IntSize::zero()),
            EventPhase::Remove => touch::Phase::Remove,
            EventPhase::Cancel => touch::Phase::Cancel,
        }
    }

    fn convert_touch_event(&mut self, touch_event: &TouchEvent) -> Vec<Event> {
        let event_time = touch_event.timestamp.expect("timestamp") as u64;
        if let Some(pointer_sample) = touch_event.pointer_sample.as_ref() {
            let mut converted_events = Vec::new();
            let interaction_id = pointer_sample.interaction.expect("interaction");
            let device_id = DeviceId(format!("touch-{}", interaction_id.device_id));
            let flatland_phase = pointer_sample.phase.expect("phase");
            let position_in_viewport =
                pointer_sample.position_in_viewport.expect("position_in_viewport");
            let location = point2(position_in_viewport[0], position_in_viewport[1]).to_i32();
            let phase = Self::flatland_phase_to_phase(flatland_phase, location);
            let contact =
                touch::Contact { contact_id: touch::ContactId(interaction_id.pointer_id), phase };
            let buttons = ButtonSet::default();
            let touch_event = touch::Event { buttons, contacts: vec![contact] };
            let new_event = Event {
                event_type: EventType::Touch(touch_event),
                device_id: device_id.clone(),
                event_time: event_time,
            };
            if flatland_phase == EventPhase::Remove || flatland_phase == EventPhase::Cancel {
                let contact = touch::Contact {
                    contact_id: touch::ContactId(interaction_id.pointer_id),
                    phase: touch::Phase::Up,
                };
                let touch_event =
                    touch::Event { buttons: ButtonSet::default(), contacts: vec![contact] };
                let up_event = Event {
                    event_type: EventType::Touch(touch_event),
                    device_id: device_id,
                    event_time: event_time,
                };
                converted_events.push(up_event);
            }
            converted_events.push(new_event);
            converted_events
        } else {
            Vec::new()
        }
    }

    pub fn handle_events(&mut self, events: &Vec<TouchEvent>) -> Vec<Event> {
        let mut converted_events = Vec::new();
        for event in events {
            converted_events.extend(self.convert_touch_event(event));
        }
        converted_events
    }
}
