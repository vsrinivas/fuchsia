// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::input::{input3_key_to_hid_usage, keyboard, DeviceId, Event, EventType, Modifiers};
use std::collections::HashSet;

#[derive(Default)]
pub(crate) struct KeyboardInputHandler {
    keyboard_device_id: DeviceId,
    pressed_keys: HashSet<fidl_fuchsia_input::Key>,
}

impl KeyboardInputHandler {
    pub fn new() -> Self {
        Self {
            keyboard_device_id: DeviceId("scenic-keyboard".to_string()),
            pressed_keys: HashSet::new(),
        }
    }

    pub fn handle_key_event(&mut self, event: &fidl_fuchsia_ui_input3::KeyEvent) -> Vec<Event> {
        if event.type_.is_none() || event.timestamp.is_none() || event.key.is_none() {
            println!("Malformed key event {:?}", event);
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
        // change in the future. Filter out code point if 0.
        let code_point = match event.key_meaning {
            Some(fidl_fuchsia_ui_input3::KeyMeaning::Codepoint(cp)) => Some(cp),
            Some(fidl_fuchsia_ui_input3::KeyMeaning::NonPrintableKey(_)) | _ => keymaps::US_QWERTY
                .hid_usage_to_code_point_for_mods(hid_usage, modifiers.shift, modifiers.caps_lock),
        }
        .filter(|cp| *cp != 0);

        let keyboard_event = keyboard::Event { code_point, hid_usage, modifiers, phase };

        let event = Event {
            event_time: timestamp,
            event_type: EventType::Keyboard(keyboard_event),
            device_id,
        };

        vec![event]
    }
}
