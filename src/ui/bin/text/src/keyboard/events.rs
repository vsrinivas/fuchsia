// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    anyhow::{format_err, Error, Result},
    core::convert::TryFrom,
    fidl_fuchsia_input as input, fidl_fuchsia_ui_input as ui_input,
    fidl_fuchsia_ui_input3 as ui_input3,
    keymaps::{self, usages::input3_key_to_hid_usage},
    std::collections::HashSet,
};

const EMPTY_TIME: u64 = 0;
const EMPTY_DEVICE_ID: u32 = 0;

const HID_USAGE_NO_EVENT: u32 = 0;

// Type of a keyboard event.
#[derive(Debug, Clone)]
pub(crate) enum KeyEventType {
    Pressed,
    Released,
}

/// A key and/or key meaning inside a `KeyEvent`. At least one of the two must be present.
#[derive(Debug, Clone)]
enum KeyOrMeaning {
    Key(input::Key),
    Meaning(ui_input3::KeyMeaning),
    Both(input::Key, ui_input3::KeyMeaning),
}

impl KeyOrMeaning {
    fn key(&self) -> Option<input::Key> {
        match self {
            KeyOrMeaning::Key(key) => Some(*key),
            KeyOrMeaning::Both(key, _) => Some(*key),
            _ => None,
        }
    }

    fn key_meaning(&self) -> Option<ui_input3::KeyMeaning> {
        match self {
            KeyOrMeaning::Meaning(meaning) => Some(*meaning),
            KeyOrMeaning::Both(_, meaning) => Some(*meaning),
            _ => None,
        }
    }

    fn code_point(&self) -> Option<u32> {
        self.key_meaning().and_then(|key_meaning| match key_meaning {
            ui_input3::KeyMeaning::Codepoint(code_point) => Some(code_point),
            _ => None,
        })
    }
}

/// Abstraction wrapper for a key event.
#[derive(Debug, Clone)]
pub(crate) struct KeyEvent {
    /// Key or key meaning that triggered the event.
    key_or_meaning: KeyOrMeaning,

    /// Keys pressed at the time of the event.
    ///
    /// (Note: This is used for determining active modifiers. Knowing which key meanings are pressed
    /// is not currently needed.)
    pub keys_pressed: HashSet<input::Key>,

    pub type_: KeyEventType,

    inner: Option<ui_input3::KeyEvent>,
}

impl KeyEvent {
    pub fn new(
        event: &ui_input3::KeyEvent,
        keys_pressed: HashSet<input::Key>,
    ) -> Result<Self, Error> {
        let (key, meaning) = (event.key, event.key_meaning);
        let key_or_meaning = match (key, meaning) {
            (None, Some(meaning)) => KeyOrMeaning::Meaning(meaning),
            (Some(key), None) => KeyOrMeaning::Key(key),
            (Some(key), Some(meaning)) => KeyOrMeaning::Both(key, meaning),
            // This branch exists because there is no way to enforce the "at least one of the two"
            // requirement in the current FIDL schema.
            (None, None) => return Err(format_err!("Missing key and key_meaning: {:?}", event)),
        };

        let type_ = match event.type_ {
            Some(ui_input3::KeyEventType::Pressed) => KeyEventType::Pressed,
            Some(ui_input3::KeyEventType::Released) => KeyEventType::Released,
            _ => return Err(format_err!("Unsupported event type")),
        };
        Ok(Self { inner: Some(event.clone()), key_or_meaning, keys_pressed, type_ })
    }

    pub fn key(&self) -> Option<input::Key> {
        self.key_or_meaning.key()
    }

    pub fn code_point(&self) -> Option<u32> {
        self.key_or_meaning.code_point()
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
        let inner = event
            .inner
            .as_ref()
            .ok_or(format_err!("Need underlying input3 event for conversion."))?;
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

        // TODO(fxbug.dev/76806): Should `NonPrintableKey`s be converted to hid_usage when key is
        // missing?
        let hid_usage = match event.key() {
            Some(key) => input3_key_to_hid_usage(key),
            None => HID_USAGE_NO_EVENT,
        };

        let code_point = match event.code_point() {
            Some(code_point) => code_point,
            None => keymaps::US_QWERTY.hid_usage_to_code_point(
                hid_usage,
                &keymaps::ModifierState {
                    caps_lock: caps_lock == ui_input::MODIFIER_CAPS_LOCK,
                    left_shift: left_shift == ui_input::MODIFIER_LEFT_SHIFT,
                    right_shift: right_shift == ui_input::MODIFIER_RIGHT_SHIFT,
                },
            )?,
        };
        Ok(ui_input::KeyboardEvent {
            event_time: inner.timestamp.unwrap_or(EMPTY_TIME as i64) as u64,
            device_id: EMPTY_DEVICE_ID,
            phase,
            hid_usage,
            code_point,
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

impl TryFrom<KeyEvent> for ui_input3::KeyEvent {
    type Error = Error;

    /// Attempts to convert KeyEvent into `fidl_fuchsia_ui_input3::KeyEvent`.
    fn try_from(event: KeyEvent) -> Result<Self, Error> {
        event.inner.ok_or(format_err!("Need underlying input3 event for conversion."))
    }
}

#[cfg(test)]
mod test {
    use {
        super::*, core::convert::TryInto, fuchsia_zircon as zx, keymaps::usages::Usages,
        maplit::hashset, std::iter::FromIterator,
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
                device_id: EMPTY_DEVICE_ID,
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
                event_time: EMPTY_TIME,
                device_id: EMPTY_DEVICE_ID,
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
                event_time: EMPTY_TIME,
                device_id: EMPTY_DEVICE_ID,
                phase: ui_input::KeyboardEventPhase::Pressed,
                hid_usage: Usages::HidUsageKeyC as u32,
                code_point: 'c' as u32,
                modifiers: ui_input::MODIFIER_NONE,
            }
        );
        Ok(())
    }

    #[test]
    fn event_code_point_only() -> Result<(), Error> {
        let event = test_helpers::create_key_event(
            zx::Time::ZERO,
            ui_input3::KeyEventType::Pressed,
            None,
            None,
            'й',
        );

        let key_event = KeyEvent::new(&event, HashSet::new())?;

        let keyboard_event: ui_input::KeyboardEvent = key_event.try_into()?;
        assert_eq!(
            keyboard_event,
            ui_input::KeyboardEvent {
                event_time: EMPTY_TIME,
                device_id: EMPTY_DEVICE_ID,
                phase: ui_input::KeyboardEventPhase::Pressed,
                hid_usage: HID_USAGE_NO_EVENT as u32,
                code_point: 'й' as u32,
                modifiers: ui_input::MODIFIER_NONE,
            }
        );
        Ok(())
    }

    #[test]
    fn event_code_point_only_unaffected_by_modifier_keys() -> Result<(), Error> {
        let event = test_helpers::create_key_event(
            zx::Time::ZERO,
            ui_input3::KeyEventType::Pressed,
            None,
            None,
            'й',
        );

        let key_event = KeyEvent::new(&event, hashset!(input::Key::RightShift))?;

        let keyboard_event: ui_input::KeyboardEvent = key_event.try_into()?;
        assert_eq!(
            keyboard_event,
            ui_input::KeyboardEvent {
                event_time: EMPTY_TIME,
                device_id: EMPTY_DEVICE_ID,
                phase: ui_input::KeyboardEventPhase::Pressed,
                hid_usage: HID_USAGE_NO_EVENT as u32,
                code_point: 'й' as u32,
                modifiers: ui_input::MODIFIER_RIGHT_SHIFT,
            }
        );
        Ok(())
    }

    #[test]
    fn event_non_printable_key_meaning_only() -> Result<(), Error> {
        let event = test_helpers::create_key_event(
            zx::Time::ZERO,
            ui_input3::KeyEventType::Pressed,
            None,
            None,
            ui_input3::NonPrintableKey::Backspace,
        );

        let key_event = KeyEvent::new(&event, HashSet::new())?;

        let keyboard_event: ui_input::KeyboardEvent = key_event.try_into()?;
        assert_eq!(
            keyboard_event,
            ui_input::KeyboardEvent {
                event_time: EMPTY_TIME,
                device_id: EMPTY_DEVICE_ID,
                phase: ui_input::KeyboardEventPhase::Pressed,
                hid_usage: HID_USAGE_NO_EVENT as u32,
                code_point: 0,
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
                event_time: EMPTY_TIME,
                device_id: EMPTY_DEVICE_ID,
                phase: ui_input::KeyboardEventPhase::Released,
                hid_usage: Usages::HidUsageKeyD as u32,
                code_point: 'd' as u32,
                modifiers: ui_input::MODIFIER_NONE,
            }
        );
        Ok(())
    }

    #[test]
    fn key_or_meaning_required() -> Result<(), Error> {
        let event = ui_input3::KeyEvent {
            type_: Some(ui_input3::KeyEventType::Pressed),
            ..ui_input3::KeyEvent::EMPTY
        };

        let key_event = KeyEvent::new(&event, HashSet::new());
        assert!(key_event.is_err());
        Ok(())
    }

    #[test]
    fn key_meaning_alone_is_sufficient() -> Result<(), Error> {
        let event = test_helpers::create_key_event(
            zx::Time::ZERO,
            ui_input3::KeyEventType::Pressed,
            None,
            None,
            'й',
        );

        let key_event = KeyEvent::new(&event, HashSet::new());
        assert!(key_event.is_ok());
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
                event_time: EMPTY_TIME,
                device_id: EMPTY_DEVICE_ID,
                phase: ui_input::KeyboardEventPhase::Pressed,
                hid_usage: Usages::HidUsageKeyF as u32,
                code_point: 'f' as u32,
                modifiers: ui_input::MODIFIER_NONE,
            }
        );
        Ok(())
    }

    #[test]
    fn explicit_code_point_overrides_calculated_code_point() -> Result<(), Error> {
        let event = test_helpers::create_key_event(
            zx::Time::ZERO,
            ui_input3::KeyEventType::Pressed,
            input::Key::Q,
            None,
            'й',
        );
        let key_event = KeyEvent::new(&event, HashSet::new())?;
        let keyboard_event: ui_input::KeyboardEvent = key_event.try_into()?;
        assert_eq!(keyboard_event.code_point, 'й' as u32);

        Ok(())
    }

    #[test]
    fn calculated_code_point_is_preserved_with_non_printable_key_meaning() -> Result<(), Error> {
        let event = test_helpers::create_key_event(
            zx::Time::ZERO,
            ui_input3::KeyEventType::Pressed,
            input::Key::Q,
            ui_input3::Modifiers::CapsLock,
            ui_input3::NonPrintableKey::Tab,
        );
        let key_event = KeyEvent::new(&event, HashSet::new())?;
        let keyboard_event: ui_input::KeyboardEvent = key_event.try_into()?;
        assert_eq!(keyboard_event.code_point, 'Q' as u32);

        Ok(())
    }

    #[test]
    fn empty_codepoint() -> Result<(), Error> {
        let keys = &[
            (input::Key::LeftShift, Usages::HidUsageKeyLeftShift as u32),
            (input::Key::Enter, Usages::HidUsageKeyEnter as u32),
        ];
        for &(key1, key2) in keys {
            let event = ui_input3::KeyEvent {
                key: Some(key1),
                type_: Some(ui_input3::KeyEventType::Pressed),
                ..ui_input3::KeyEvent::EMPTY
            };

            let key_event = KeyEvent::new(&event, HashSet::new())?;

            let keyboard_event: ui_input::KeyboardEvent = key_event.try_into()?;
            assert_eq!(
                keyboard_event,
                ui_input::KeyboardEvent {
                    event_time: EMPTY_TIME,
                    device_id: EMPTY_DEVICE_ID,
                    phase: ui_input::KeyboardEventPhase::Pressed,
                    hid_usage: key2,
                    code_point: 0,
                    modifiers: ui_input::MODIFIER_NONE,
                }
            );
        }
        Ok(())
    }
}
