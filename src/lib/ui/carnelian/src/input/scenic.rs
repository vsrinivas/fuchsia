// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    input::{
        device_id_for_event, input3_key_to_hid_usage, keyboard, mouse, touch, Button, ButtonSet,
        DeviceId, Event, EventType, Modifiers,
    },
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
    keyboard_device_id: DeviceId,
    pressed_keys: HashSet<fidl_fuchsia_input::Key>,
}

impl ScenicInputHandler {
    pub fn new() -> Self {
        Self {
            keyboard_device_id: DeviceId("scenic-keyboard".to_string()),
            contacts: HashMap::new(),
            pressed_keys: HashSet::new(),
        }
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

    pub fn handle_scenic_input_event(
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

    pub fn handle_scenic_key_event(
        &mut self,
        event: &fidl_fuchsia_ui_input3::KeyEvent,
    ) -> Vec<Event> {
        if event.type_.is_none() || event.timestamp.is_none() || event.key.is_none() {
            println!("Malformed scenic key event {:?}", event);
            return vec![];
        }
        let event_type = event.type_.unwrap();
        let timestamp = event.timestamp.unwrap() as u64;
        let key = event.key.unwrap();
        let phase: Option<keyboard::Phase> = match event_type {
            fidl_fuchsia_ui_input3::KeyEventType::Pressed => Some(keyboard::Phase::Pressed),
            fidl_fuchsia_ui_input3::KeyEventType::Released => Some(keyboard::Phase::Released),
            fidl_fuchsia_ui_input3::KeyEventType::Cancel => Some(keyboard::Phase::Cancelled),
            // The input3 sync feature is not supported
            fidl_fuchsia_ui_input3::KeyEventType::Sync => None,
        };
        if phase.is_none() {
            return vec![];
        }
        let phase = phase.unwrap();

        match phase {
            keyboard::Phase::Pressed => {
                self.pressed_keys.insert(key);
            }
            keyboard::Phase::Released | keyboard::Phase::Cancelled => {
                self.pressed_keys.remove(&key);
            }
            _ => (),
        }

        let device_id = self.keyboard_device_id.clone();
        let hid_usage = input3_key_to_hid_usage(key);
        let modifiers = Modifiers::from_pressed_keys_3(&self.pressed_keys);

        // Apply the resolved code point if one is available.  Fall back to US_QWERTY if no such
        // luck. Note that the fallback currently includes the nonprintable keys, which may need to
        // change in the future.
        let code_point = match event.key_meaning {
            Some(fidl_fuchsia_ui_input3::KeyMeaning::Codepoint(cp)) => Some(cp),
            Some(fidl_fuchsia_ui_input3::KeyMeaning::NonPrintableKey(_)) | _ => keymaps::US_QWERTY
                .hid_usage_to_code_point_for_mods(hid_usage, modifiers.shift, modifiers.caps_lock),
        };

        let keyboard_event = keyboard::Event { code_point, hid_usage, modifiers, phase };

        let event = Event {
            event_time: timestamp,
            event_type: EventType::Keyboard(keyboard_event),
            device_id,
        };

        vec![event]
    }
}
