// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    anyhow::{format_err, Error},
    core::convert::TryFrom,
    fidl_fuchsia_input as input, fidl_fuchsia_ui_input as ui_input,
    fidl_fuchsia_ui_input3 as ui_input3,
    input_synthesis::{keymaps::QWERTY_MAP, usages::input3_key_to_hid_usage},
    std::collections::HashSet,
};

// Type of a keyboard event.
pub(crate) enum KeyEventType {
    Pressed,
    Released,
}

/// Abstraction wrapper for a key event.
pub(crate) struct KeyEvent {
    // Key that triggered the event.
    pub key: input::Key,

    // Keys pressed at the time of the event.
    pub keys_pressed: HashSet<input::Key>,

    pub type_: KeyEventType,

    inner: Option<ui_input3::KeyEvent>,
}

impl KeyEvent {
    pub fn new(
        event: &ui_input3::KeyEvent,
        keys_pressed: HashSet<input::Key>,
    ) -> Result<Self, Error> {
        let key = event.key.ok_or(format_err!("No key in the event"))?;
        let type_ = match event.type_ {
            Some(ui_input3::KeyEventType::Pressed) => KeyEventType::Pressed,
            Some(ui_input3::KeyEventType::Released) => KeyEventType::Released,
            _ => return Err(format_err!("Unsupported event type")),
        };
        Ok(Self { inner: Some(event.clone()), key, keys_pressed, type_ })
    }
}

impl TryFrom<KeyEvent> for ui_input::KeyboardEvent {
    type Error = Error;

    /// Attempts to convert KeyEvent into fidl_fuchsia_ui_input::KeyboardEvent.
    /// This will produce single event with the best effort using the data available.
    /// Data fields that are not possible to express in a single `KeyboardEvent` are
    /// ignored: e.g. NumLock and ScrollLock modifiers.
    /// USB HID usage codes outside of QWERTY keyboard layout generate errors.
    fn try_from(event: KeyEvent) -> Result<Self, Error> {
        let inner =
            event.inner.ok_or(format_err!("Need underlying input3 event for conversion."))?;
        let phase = match event.type_ {
            KeyEventType::Pressed => ui_input::KeyboardEventPhase::Pressed,
            KeyEventType::Released => ui_input::KeyboardEventPhase::Released,
        };

        let caps_lock = match inner.modifiers {
            Some(modifiers) if modifiers.contains(ui_input3::Modifiers::CapsLock) => {
                ui_input::MODIFIER_CAPS_LOCK
            }
            _ => ui_input::MODIFIER_NONE,
        };
        let left_shift = if event.keys_pressed.contains(&input::Key::LeftShift) {
            ui_input::MODIFIER_LEFT_SHIFT
        } else {
            ui_input::MODIFIER_NONE
        };
        let right_shift = if event.keys_pressed.contains(&input::Key::RightShift) {
            ui_input::MODIFIER_RIGHT_SHIFT
        } else {
            ui_input::MODIFIER_NONE
        };
        let left_control = if event.keys_pressed.contains(&input::Key::LeftCtrl) {
            ui_input::MODIFIER_LEFT_CONTROL
        } else {
            ui_input::MODIFIER_NONE
        };
        let right_control = if event.keys_pressed.contains(&input::Key::RightCtrl) {
            ui_input::MODIFIER_RIGHT_CONTROL
        } else {
            ui_input::MODIFIER_NONE
        };
        let left_alt = if event.keys_pressed.contains(&input::Key::LeftAlt) {
            ui_input::MODIFIER_LEFT_ALT
        } else {
            ui_input::MODIFIER_NONE
        };
        let right_alt = if event.keys_pressed.contains(&input::Key::RightAlt) {
            ui_input::MODIFIER_RIGHT_ALT
        } else {
            ui_input::MODIFIER_NONE
        };
        let left_super = if event.keys_pressed.contains(&input::Key::LeftMeta) {
            ui_input::MODIFIER_LEFT_SUPER
        } else {
            ui_input::MODIFIER_NONE
        };
        let right_super = if event.keys_pressed.contains(&input::Key::RightMeta) {
            ui_input::MODIFIER_RIGHT_SUPER
        } else {
            ui_input::MODIFIER_NONE
        };

        let hid_usage = input3_key_to_hid_usage(event.key) as usize;
        let code_point = if hid_usage < QWERTY_MAP.len() {
            if let Some(map_entry) = QWERTY_MAP[hid_usage] {
                if caps_lock | left_shift | right_shift != 0 {
                    map_entry
                        .1
                        .and_then(|shifted_char| Some(shifted_char as u32))
                        .ok_or(format_err!("Invalid USB HID code: {:?}", hid_usage))
                } else {
                    Ok(map_entry.0 as u32)
                }
            } else {
                Err(format_err!("Invalid USB HID code: {:?}", hid_usage))
            }
        } else {
            Err(format_err!("USB HID code out of bounds: {:?}", hid_usage))
        };

        Ok(ui_input::KeyboardEvent {
            event_time: inner.timestamp.unwrap_or(0) as u64,
            device_id: 0,
            phase: phase,
            hid_usage: hid_usage as u32,
            code_point: code_point?,
            modifiers: caps_lock
                | left_shift
                | right_shift
                | left_control
                | right_control
                | left_alt
                | right_alt
                | left_super
                | right_super,
        })
    }
}

#[cfg(test)]
mod test {
    use {
        super::*, core::convert::TryInto, input_synthesis::usages::Usages, std::iter::FromIterator,
    };

    static TEST_TIMESTAMP: i64 = 123;

    #[test]
    fn event_all_locks() -> Result<(), Error> {
        let event = ui_input3::KeyEvent {
            timestamp: Some(TEST_TIMESTAMP),
            type_: Some(ui_input3::KeyEventType::Pressed),
            key: Some(input::Key::A),
            modifiers: Some(
                ui_input3::Modifiers::CapsLock
                    | ui_input3::Modifiers::NumLock
                    | ui_input3::Modifiers::ScrollLock,
            ),
            ..ui_input3::KeyEvent::EMPTY
        };

        let key_event = KeyEvent::new(&event, HashSet::new())?;

        let keyboard_event: ui_input::KeyboardEvent = key_event.try_into()?;
        assert_eq!(
            keyboard_event,
            ui_input::KeyboardEvent {
                event_time: TEST_TIMESTAMP as u64,
                device_id: 0,
                phase: ui_input::KeyboardEventPhase::Pressed,
                hid_usage: Usages::HidUsageKeyA as u32,
                code_point: 'A' as u32,
                modifiers: ui_input::MODIFIER_CAPS_LOCK,
            }
        );
        Ok(())
    }

    #[test]
    fn event_all_modifiers() -> Result<(), Error> {
        let event = ui_input3::KeyEvent {
            type_: Some(ui_input3::KeyEventType::Pressed),
            key: Some(input::Key::B),
            modifiers: Some(
                ui_input3::Modifiers::CapsLock
                    | ui_input3::Modifiers::NumLock
                    | ui_input3::Modifiers::ScrollLock,
            ),
            ..ui_input3::KeyEvent::EMPTY
        };

        let key_event = KeyEvent::new(
            &event,
            HashSet::from_iter(vec![
                input::Key::LeftShift,
                input::Key::RightShift,
                input::Key::LeftCtrl,
                input::Key::RightCtrl,
                input::Key::LeftAlt,
                input::Key::RightAlt,
                input::Key::LeftMeta,
                input::Key::RightMeta,
            ]),
        )?;

        let keyboard_event: ui_input::KeyboardEvent = key_event.try_into()?;
        assert_eq!(
            keyboard_event,
            ui_input::KeyboardEvent {
                event_time: 0,
                device_id: 0,
                phase: ui_input::KeyboardEventPhase::Pressed,
                hid_usage: Usages::HidUsageKeyB as u32,
                code_point: 'B' as u32,
                modifiers: ui_input::MODIFIER_CAPS_LOCK
                    | ui_input::MODIFIER_LEFT_SHIFT
                    | ui_input::MODIFIER_RIGHT_SHIFT
                    | ui_input::MODIFIER_LEFT_CONTROL
                    | ui_input::MODIFIER_RIGHT_CONTROL
                    | ui_input::MODIFIER_LEFT_ALT
                    | ui_input::MODIFIER_RIGHT_ALT
                    | ui_input::MODIFIER_LEFT_SUPER
                    | ui_input::MODIFIER_RIGHT_SUPER,
            }
        );
        Ok(())
    }

    #[test]
    fn event_no_modifiers() -> Result<(), Error> {
        let event = ui_input3::KeyEvent {
            key: Some(input::Key::C),
            type_: Some(ui_input3::KeyEventType::Pressed),
            ..ui_input3::KeyEvent::EMPTY
        };

        let key_event = KeyEvent::new(&event, HashSet::new())?;

        let keyboard_event: ui_input::KeyboardEvent = key_event.try_into()?;
        assert_eq!(
            keyboard_event,
            ui_input::KeyboardEvent {
                event_time: 0,
                device_id: 0,
                phase: ui_input::KeyboardEventPhase::Pressed,
                hid_usage: Usages::HidUsageKeyC as u32,
                code_point: 'c' as u32,
                modifiers: ui_input::MODIFIER_NONE,
            }
        );
        Ok(())
    }

    #[test]
    fn event_key_released() -> Result<(), Error> {
        let event = ui_input3::KeyEvent {
            key: Some(input::Key::D),
            type_: Some(ui_input3::KeyEventType::Released),
            ..ui_input3::KeyEvent::EMPTY
        };

        let key_event = KeyEvent::new(&event, HashSet::new())?;

        let keyboard_event: ui_input::KeyboardEvent = key_event.try_into()?;
        assert_eq!(
            keyboard_event,
            ui_input::KeyboardEvent {
                event_time: 0,
                device_id: 0,
                phase: ui_input::KeyboardEventPhase::Released,
                hid_usage: Usages::HidUsageKeyD as u32,
                code_point: 'd' as u32,
                modifiers: ui_input::MODIFIER_NONE,
            }
        );
        Ok(())
    }

    #[test]
    fn key_required() -> Result<(), Error> {
        let event = ui_input3::KeyEvent {
            type_: Some(ui_input3::KeyEventType::Pressed),
            ..ui_input3::KeyEvent::EMPTY
        };

        let key_event = KeyEvent::new(&event, HashSet::new());
        assert!(key_event.is_err());
        Ok(())
    }

    #[test]
    fn type_required() -> Result<(), Error> {
        let event = ui_input3::KeyEvent { key: Some(input::Key::E), ..ui_input3::KeyEvent::EMPTY };

        let key_event = KeyEvent::new(&event, HashSet::new());
        assert!(key_event.is_err());
        Ok(())
    }

    #[test]
    fn sync_not_supported() -> Result<(), Error> {
        let event = ui_input3::KeyEvent {
            type_: Some(ui_input3::KeyEventType::Sync),
            key: Some(input::Key::E),
            ..ui_input3::KeyEvent::EMPTY
        };

        let key_event = KeyEvent::new(&event, HashSet::new());
        assert!(key_event.is_err());
        Ok(())
    }

    #[test]
    fn cancel_not_supported() -> Result<(), Error> {
        let event = ui_input3::KeyEvent {
            type_: Some(ui_input3::KeyEventType::Cancel),
            key: Some(input::Key::E),
            ..ui_input3::KeyEvent::EMPTY
        };
        let key_event = KeyEvent::new(&event, HashSet::new());
        assert!(key_event.is_err());
        Ok(())
    }

    #[test]
    fn non_modifier_keys_pressed_ignored() -> Result<(), Error> {
        let event = ui_input3::KeyEvent {
            key: Some(input::Key::F),
            type_: Some(ui_input3::KeyEventType::Pressed),
            ..ui_input3::KeyEvent::EMPTY
        };

        let key_event =
            KeyEvent::new(&event, HashSet::from_iter(vec![input::Key::G, input::Key::H]))?;

        let keyboard_event: ui_input::KeyboardEvent = key_event.try_into()?;
        assert_eq!(
            keyboard_event,
            ui_input::KeyboardEvent {
                event_time: 0,
                device_id: 0,
                phase: ui_input::KeyboardEventPhase::Pressed,
                hid_usage: Usages::HidUsageKeyF as u32,
                code_point: 'f' as u32,
                modifiers: ui_input::MODIFIER_NONE,
            }
        );
        Ok(())
    }
}
