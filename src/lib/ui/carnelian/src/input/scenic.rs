// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    input::{device_id_for_event, mouse, touch, Button, ButtonSet, Event, EventType},
    IntPoint, IntSize, Size,
};
use euclid::point2;
use std::collections::{HashMap, HashSet};

// Scenic has a logical/physical coordinate system scheme that Carnelian does not currently
// want to expose.
fn to_physical_point(x: f32, y: f32, metrics: &Size) -> IntPoint {
    let x = x * metrics.width;
    let y = y * metrics.height;
    point2(x as i32, y as i32)
}

#[derive(Default)]
pub(crate) struct ScenicInputHandler {
    contacts: HashMap<u32, touch::Contact>,
}

impl ScenicInputHandler {
    pub fn new() -> Self {
        Self { contacts: HashMap::new() }
    }

    fn convert_scenic_mouse_phase(
        &self,
        event: &fidl_fuchsia_ui_input::PointerEvent,
    ) -> Option<mouse::Phase> {
        let buttons = event.buttons;
        let pressed_buttons: HashSet<u8> = (0..3)
            .filter_map(|index| if buttons & 1 << index != 0 { Some(index + 1) } else { None })
            .collect();
        match event.phase {
            fidl_fuchsia_ui_input::PointerEventPhase::Down => pressed_buttons
                .iter()
                .nth(0)
                .and_then(|button| Some(mouse::Phase::Down(Button(*button)))),
            fidl_fuchsia_ui_input::PointerEventPhase::Move => Some(mouse::Phase::Moved),
            fidl_fuchsia_ui_input::PointerEventPhase::Up => pressed_buttons
                .iter()
                .nth(0)
                .and_then(|button| Some(mouse::Phase::Up(Button(*button)))),
            _ => None,
        }
    }

    fn handle_touch_event(
        &mut self,
        metrics: &Size,
        event: &fidl_fuchsia_ui_input::PointerEvent,
    ) -> Option<Event> {
        let device_id = device_id_for_event(event);
        let location = to_physical_point(event.x, event.y, metrics);
        let phase = match event.phase {
            fidl_fuchsia_ui_input::PointerEventPhase::Down => {
                Some(touch::Phase::Down(location, IntSize::zero()))
            }
            fidl_fuchsia_ui_input::PointerEventPhase::Move => {
                Some(touch::Phase::Moved(location, IntSize::zero()))
            }
            fidl_fuchsia_ui_input::PointerEventPhase::Up => Some(touch::Phase::Up),
            fidl_fuchsia_ui_input::PointerEventPhase::Remove => Some(touch::Phase::Remove),
            fidl_fuchsia_ui_input::PointerEventPhase::Cancel => Some(touch::Phase::Cancel),
            _ => None,
        };

        if let Some(phase) = phase {
            let contact = touch::Contact { contact_id: touch::ContactId(event.pointer_id), phase };
            self.contacts.insert(event.pointer_id, contact);
            let contacts: Vec<touch::Contact> = self.contacts.values().map(|v| v.clone()).collect();
            self.contacts.retain(|_, contact| match contact.phase {
                touch::Phase::Remove => false,
                touch::Phase::Cancel => false,
                _ => true,
            });
            let buttons = ButtonSet::new_from_flags(event.buttons);
            let touch_event = touch::Event { buttons, contacts };
            let new_event = Event {
                event_type: EventType::Touch(touch_event),
                device_id: device_id,
                event_time: event.event_time,
            };
            Some(new_event)
        } else {
            None
        }
    }

    fn handle_scenic_pointer_event(
        &mut self,
        metrics: &Size,
        event: &fidl_fuchsia_ui_input::PointerEvent,
    ) -> Vec<Event> {
        let mut events = Vec::new();
        let location = to_physical_point(event.x, event.y, metrics);
        match event.type_ {
            fidl_fuchsia_ui_input::PointerEventType::Touch => {
                let new_event = self.handle_touch_event(metrics, event);
                events.extend(new_event);
            }
            fidl_fuchsia_ui_input::PointerEventType::Mouse => {
                if let Some(phase) = self.convert_scenic_mouse_phase(&event) {
                    let device_id = device_id_for_event(event);
                    let mouse_input = mouse::Event {
                        location,
                        buttons: ButtonSet::new_from_flags(event.buttons),
                        phase: phase,
                    };
                    let new_event = Event {
                        event_type: EventType::Mouse(mouse_input),
                        device_id: device_id,
                        event_time: event.event_time,
                    };
                    events.push(new_event);
                }
            }
            _ => (),
        }
        events
    }

    pub fn handle_input_event(
        &mut self,
        metrics: &Size,
        event: &fidl_fuchsia_ui_input::InputEvent,
    ) -> Vec<Event> {
        let mut events = Vec::new();
        match event {
            fidl_fuchsia_ui_input::InputEvent::Pointer(pointer_event) => {
                events.extend(self.handle_scenic_pointer_event(metrics, &pointer_event));
            }
            _ => (),
        }
        events
    }
}
