// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{format_err, Result};
use fidl_fuchsia_input;
use fuchsia_syslog::fx_log_err;
use lazy_static::lazy_static;
use std::collections;

mod defs;

pub mod config;
pub mod inverse_keymap;
pub mod usages;

lazy_static! {
    /// A US QWERTY keymap.
    pub static ref US_QWERTY: Keymap<'static> = Keymap::new(&defs::QWERTY_MAP);

    /// A US DVORAK keymap.
    pub static ref US_DVORAK: Keymap<'static> = Keymap::new(&defs::DVORAK_MAP);

    /// A FR AZERTY keymap.
    pub static ref FR_AZERTY: Keymap<'static> = Keymap::new(&defs::FR_AZERTY_MAP);
}

/// Gets a keymap based on the supplied `keymap` selector.  If no keymap is
/// found the fallback is always US QWERTY.
pub fn select_keymap<'a>(keymap: &Option<String>) -> &'a Keymap<'a> {
    match keymap {
        Some(ref k) if k == "FR_AZERTY" => &FR_AZERTY,
        Some(ref k) if k == "US_DVORAK" => &US_DVORAK,
        _ => &US_QWERTY,
    }
}

/// A codepoint returned by [hid_usage_to_code_point] for HID usages that do
/// not have an associated code point, e.g. Alt.
pub(crate) const EMPTY_CODEPOINT: u32 = 0;

/// A Us Qwerty keymap
pub struct Keymap<'a> {
    map: &'a [Option<defs::KeyLevels>],
}

impl AsRef<[Option<defs::KeyLevels>]> for Keymap<'_> {
    fn as_ref(&self) -> &[Option<defs::KeyLevels>] {
        self.map
    }
}

impl<'a> Keymap<'a> {
    /// Creates a new keymap.
    fn new(map: &'a [Option<defs::KeyLevels>]) -> Self {
        Keymap { map }
    }

    /// Applies the keymap to the given key.
    pub fn apply(
        &self,
        key: fidl_fuchsia_input::Key,
        modifier_state: &ModifierState,
    ) -> Option<fidl_fuchsia_ui_input3::KeyMeaning> {
        let hid_usage = usages::input3_key_to_hid_usage(key);

        use fidl_fuchsia_input::Key;
        use fidl_fuchsia_ui_input3::{KeyMeaning, NonPrintableKey};
        match key {
            // Nonprintable keys get their own key meaning.
            Key::Enter => Some(KeyMeaning::NonPrintableKey(
                NonPrintableKey::Enter,
            )),
            Key::Tab => Some(KeyMeaning::NonPrintableKey(
                NonPrintableKey::Tab,
            )),
            Key::Backspace => Some(KeyMeaning::NonPrintableKey(
                NonPrintableKey::Backspace,
            )),
            // Printable keys get code points as key meanings.
            _ => {
                self.hid_usage_to_code_point(hid_usage, modifier_state)
                .map(KeyMeaning::Codepoint)
                .map_err(|e| {
                    fx_log_err!(
                        "keymaps::Keymap::apply: Could not convert HID usage to code point: {:?}, modifiers: {:?}",
                        &hid_usage,
                        modifier_state,
                    );
                    e
                })
                .ok()
            }
        }
    }

    /// Converts a HID usage for a key to a Unicode code point where such a code point exists, based on
    /// a US QWERTY keyboard layout.  Returns EMPTY_CODEPOINT if a code point does not exist (e.g. Alt),
    /// and an error in case the mapping somehow fails.
    pub fn hid_usage_to_code_point(
        &self,
        hid_usage: u32,
        modifier_state: &ModifierState,
    ) -> Result<u32> {
        if (hid_usage as usize) < self.map.len() {
            if let Some(ref map_entry) = self.map[hid_usage as usize] {
                map_entry
                    .get_key(modifier_state)
                    .map(|c| c as u32)
                    .ok_or(format_err!("Invalid USB HID code: {:?}", hid_usage))
            } else {
                Ok(EMPTY_CODEPOINT) // No code point provided by a keymap, e.g. Enter.
            }
        } else {
            Ok(EMPTY_CODEPOINT) // No code point available, e.g. Shift, Alt, etc.
        }
    }

    pub fn hid_usage_to_code_point_for_mods(
        &self,
        hid_usage: u32,
        shift: bool,
        caps_lock: bool,
    ) -> Option<u32> {
        let modifier_state = ModifierState { caps_lock, left_shift: shift, right_shift: shift };
        let code_point = self.hid_usage_to_code_point(hid_usage, &modifier_state);
        match code_point {
            Ok(EMPTY_CODEPOINT) => None,
            Ok(c) => Some(c),
            Err(_) => None,
        }
    }
}

/// Tracks the current state of "significant" modifier keys.
///
/// Currently, a modifier key is "significant" if it affects the mapping of a
/// Fuchsia key to a key meaning.
#[derive(Default, Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct ModifierState {
    /// Whether the Caps Lock level modifier is active.  Caps Lock level modifier
    /// may be active even if the key itself is not actuated.
    pub caps_lock: bool,
    /// Whether the left shift modifier key is active.  Shift keys are normally
    /// active only while they are actuated (held pressed).
    pub left_shift: bool,
    /// Same as `left_shift`, but for the right shift key.
    pub right_shift: bool,
}

impl ModifierState {
    /// Update the modifier tracker state with this event.
    /// An error is returned in the case the input is completely unexpectedly broken.
    pub fn update(
        &mut self,
        event: fidl_fuchsia_ui_input3::KeyEventType,
        key: fidl_fuchsia_input::Key,
    ) {
        match event {
            fidl_fuchsia_ui_input3::KeyEventType::Pressed => match key {
                fidl_fuchsia_input::Key::LeftShift => self.left_shift = true,
                fidl_fuchsia_input::Key::RightShift => self.right_shift = true,
                fidl_fuchsia_input::Key::CapsLock => self.caps_lock = !self.caps_lock,
                _ => {}
            },
            fidl_fuchsia_ui_input3::KeyEventType::Released => match key {
                fidl_fuchsia_input::Key::LeftShift => self.left_shift = false,
                fidl_fuchsia_input::Key::RightShift => self.right_shift = false,
                _ => {}
            },
            _ => {
                panic!(
                    "ModifierState::update: unexpected event: {:?} - this is a programmer error",
                    event
                );
            }
        }
    }

    /// Returns true if the "shift" level modifier is active.  This corresponds
    /// to left or right shift keys, but not caps lock.
    pub fn is_shift_active(&self) -> bool {
        self.left_shift | self.right_shift
    }

    /// Returns true if Caps Lock is active.
    pub fn is_caps_lock_active(&self) -> bool {
        self.caps_lock
    }
}

/// Tracks the current state of all the keyboard keys.
///
/// This is repetitive code, so perhaps better handle it here.
#[derive(Debug, Clone, Default)]
pub struct KeyState {
    pressed_keys: collections::HashSet<fidl_fuchsia_input::Key>,
}

impl KeyState {
    /// Creates a new [KeyState]
    pub fn new() -> Self {
        KeyState { pressed_keys: collections::HashSet::new() }
    }

    /// Updates the key tracking state with the given key event pair.
    pub fn update(
        &mut self,
        event: fidl_fuchsia_ui_input3::KeyEventType,
        key: fidl_fuchsia_input::Key,
    ) {
        match event {
            fidl_fuchsia_ui_input3::KeyEventType::Pressed
            | fidl_fuchsia_ui_input3::KeyEventType::Sync => {
                self.pressed_keys.insert(key);
            }
            fidl_fuchsia_ui_input3::KeyEventType::Released
            | fidl_fuchsia_ui_input3::KeyEventType::Cancel => {
                self.pressed_keys.remove(&key);
            }
        }
    }

    /// Returns true if `key` is noted as pressed.
    pub fn is_pressed(&self, key: &fidl_fuchsia_input::Key) -> bool {
        self.pressed_keys.contains(key)
    }

    /// Returns `true` if at least one key from `keys` is pressed.
    pub fn pressed_any(&self, keys: &[fidl_fuchsia_input::Key]) -> bool {
        keys.iter().any(|k| self.is_pressed(k))
    }

    /// Returns `true` if all keys from `keys` are pressed.
    pub fn pressed_all(&self, keys: &[fidl_fuchsia_input::Key]) -> bool {
        keys.iter().all(|k| self.is_pressed(k))
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use test_case::test_case;

    const HID_USAGE_KEY_A: u32 = 0x04;
    const HID_USAGE_KEY_B: u32 = 0x05;
    const HID_USAGE_KEY_L: u32 = 0x0f;
    const HID_USAGE_KEY_1: u32 = 0x1e;

    // The effects of Shift and CapsLock on keys are different for non-letters.
    #[test]
    fn caps_lock_effect() -> Result<()> {
        assert_eq!(
            '1' as u32,
            US_QWERTY.hid_usage_to_code_point(
                HID_USAGE_KEY_1,
                &ModifierState { caps_lock: true, ..Default::default() }
            )?
        );
        assert_eq!(
            '!' as u32,
            US_QWERTY.hid_usage_to_code_point(
                HID_USAGE_KEY_1,
                &ModifierState { left_shift: true, ..Default::default() }
            )?
        );
        Ok(())
    }

    #[test]
    fn spotcheck_keymap() -> Result<()> {
        assert_eq!(
            'a' as u32,
            US_QWERTY.hid_usage_to_code_point(
                HID_USAGE_KEY_A,
                &ModifierState { ..Default::default() }
            )?
        );
        assert_eq!(
            'A' as u32,
            US_QWERTY.hid_usage_to_code_point(
                HID_USAGE_KEY_A,
                &ModifierState { caps_lock: true, ..Default::default() }
            )?
        );
        assert_eq!(
            'A' as u32,
            US_QWERTY.hid_usage_to_code_point(
                HID_USAGE_KEY_A,
                &ModifierState { right_shift: true, ..Default::default() }
            )?
        );
        assert_eq!(
            'A' as u32,
            US_QWERTY.hid_usage_to_code_point(
                HID_USAGE_KEY_A,
                &ModifierState { left_shift: true, ..Default::default() }
            )?
        );
        Ok(())
    }

    #[test]
    fn spotcheck_dvorak_keymap() -> Result<()> {
        assert_eq!(
            'a' as u32,
            US_DVORAK.hid_usage_to_code_point(
                HID_USAGE_KEY_A,
                &ModifierState { ..Default::default() }
            )?
        );
        assert_eq!(
            'A' as u32,
            US_DVORAK.hid_usage_to_code_point(
                HID_USAGE_KEY_A,
                &ModifierState { caps_lock: true, ..Default::default() }
            )?
        );
        assert_eq!(
            'x' as u32,
            US_DVORAK.hid_usage_to_code_point(
                HID_USAGE_KEY_B,
                &ModifierState { ..Default::default() }
            )?
        );
        assert_eq!(
            'X' as u32,
            US_DVORAK.hid_usage_to_code_point(
                HID_USAGE_KEY_B,
                &ModifierState { caps_lock: true, ..Default::default() }
            )?
        );
        assert_eq!(
            'n' as u32,
            US_DVORAK.hid_usage_to_code_point(
                HID_USAGE_KEY_L,
                &ModifierState { ..Default::default() }
            )?
        );
        assert_eq!(
            'N' as u32,
            US_DVORAK.hid_usage_to_code_point(
                HID_USAGE_KEY_L,
                &ModifierState { caps_lock: true, ..Default::default() }
            )?
        );
        Ok(())
    }

    // CapsLock ______/""""""""\_______/"""""""""\_______
    // Active   ______/""""""""""""""""\________________
    #[test]
    fn test_caps_lock() {
        let mut modifier_state: ModifierState = Default::default();
        assert!(!modifier_state.is_caps_lock_active());

        modifier_state.update(
            fidl_fuchsia_ui_input3::KeyEventType::Pressed,
            fidl_fuchsia_input::Key::CapsLock,
        );
        assert!(modifier_state.is_caps_lock_active());
        modifier_state.update(
            fidl_fuchsia_ui_input3::KeyEventType::Released,
            fidl_fuchsia_input::Key::CapsLock,
        );
        assert!(modifier_state.is_caps_lock_active());

        modifier_state.update(
            fidl_fuchsia_ui_input3::KeyEventType::Pressed,
            fidl_fuchsia_input::Key::CapsLock,
        );
        assert!(!modifier_state.is_caps_lock_active());
        modifier_state.update(
            fidl_fuchsia_ui_input3::KeyEventType::Released,
            fidl_fuchsia_input::Key::CapsLock,
        );
        assert!(!modifier_state.is_caps_lock_active());
    }

    #[test]
    fn test_modifier_tracker() {
        let mut modifier_state: ModifierState = Default::default();
        assert!(!modifier_state.is_shift_active());

        modifier_state.update(
            fidl_fuchsia_ui_input3::KeyEventType::Pressed,
            fidl_fuchsia_input::Key::LeftShift,
        );
        assert!(modifier_state.is_shift_active());
        modifier_state.update(
            fidl_fuchsia_ui_input3::KeyEventType::Released,
            fidl_fuchsia_input::Key::LeftShift,
        );
        assert!(!modifier_state.is_shift_active());

        modifier_state.update(
            fidl_fuchsia_ui_input3::KeyEventType::Pressed,
            fidl_fuchsia_input::Key::RightShift,
        );
        assert!(modifier_state.is_shift_active());
        modifier_state.update(
            fidl_fuchsia_ui_input3::KeyEventType::Released,
            fidl_fuchsia_input::Key::RightShift,
        );
        assert!(!modifier_state.is_shift_active());

        modifier_state.update(
            fidl_fuchsia_ui_input3::KeyEventType::Pressed,
            fidl_fuchsia_input::Key::CapsLock,
        );
        assert!(!modifier_state.is_shift_active());
        assert!(modifier_state.is_caps_lock_active());
        modifier_state.update(
            fidl_fuchsia_ui_input3::KeyEventType::Released,
            fidl_fuchsia_input::Key::CapsLock,
        );
        assert!(!modifier_state.is_shift_active());
        assert!(modifier_state.is_caps_lock_active());
        modifier_state.update(
            fidl_fuchsia_ui_input3::KeyEventType::Pressed,
            fidl_fuchsia_input::Key::CapsLock,
        );
        assert!(!modifier_state.is_shift_active());
        assert!(!modifier_state.is_caps_lock_active());
        modifier_state.update(
            fidl_fuchsia_ui_input3::KeyEventType::Released,
            fidl_fuchsia_input::Key::CapsLock,
        );
        assert!(!modifier_state.is_shift_active());
        assert!(!modifier_state.is_caps_lock_active());
    }

    // CapsLock            ________/""""""""""\_______/"""""\_____
    // LeftShift           ____________/"""""""""""\______________
    // is_shift_active     ____________/"""""""""""\______________
    // is_caps_lock_active ________/""""""""""""""""""\___________
    #[test]
    fn test_interleaved_caps_lock_and_shift() {
        let mut modifier_state: ModifierState = Default::default();
        assert!(!modifier_state.is_shift_active());

        modifier_state.update(
            fidl_fuchsia_ui_input3::KeyEventType::Pressed,
            fidl_fuchsia_input::Key::CapsLock,
        );
        assert!(!modifier_state.is_shift_active());
        assert!(modifier_state.is_caps_lock_active());

        modifier_state.update(
            fidl_fuchsia_ui_input3::KeyEventType::Pressed,
            fidl_fuchsia_input::Key::LeftShift,
        );
        assert!(modifier_state.is_shift_active());
        assert!(modifier_state.is_caps_lock_active());

        modifier_state.update(
            fidl_fuchsia_ui_input3::KeyEventType::Released,
            fidl_fuchsia_input::Key::CapsLock,
        );
        assert!(modifier_state.is_shift_active());
        assert!(modifier_state.is_caps_lock_active());

        modifier_state.update(
            fidl_fuchsia_ui_input3::KeyEventType::Released,
            fidl_fuchsia_input::Key::LeftShift,
        );
        // Caps Lock is still active...
        assert!(!modifier_state.is_shift_active());
        assert!(modifier_state.is_caps_lock_active());

        // Press and release Caps Lock again.
        modifier_state.update(
            fidl_fuchsia_ui_input3::KeyEventType::Pressed,
            fidl_fuchsia_input::Key::CapsLock,
        );
        assert!(!modifier_state.is_shift_active());
        assert!(!modifier_state.is_caps_lock_active());

        modifier_state.update(
            fidl_fuchsia_ui_input3::KeyEventType::Released,
            fidl_fuchsia_input::Key::CapsLock,
        );
        assert!(!modifier_state.is_shift_active());
        assert!(!modifier_state.is_caps_lock_active());
    }

    #[test]
    fn key_state_tracker() {
        let mut t = KeyState::new();
        assert_eq!(false, t.is_pressed(&fidl_fuchsia_input::Key::Space));
        t.update(fidl_fuchsia_ui_input3::KeyEventType::Pressed, fidl_fuchsia_input::Key::Space);
        assert_eq!(true, t.is_pressed(&fidl_fuchsia_input::Key::Space));

        t.update(fidl_fuchsia_ui_input3::KeyEventType::Released, fidl_fuchsia_input::Key::Space);

        assert_eq!(false, t.is_pressed(&fidl_fuchsia_input::Key::Space));

        t.update(fidl_fuchsia_ui_input3::KeyEventType::Sync, fidl_fuchsia_input::Key::Space);
        assert_eq!(true, t.is_pressed(&fidl_fuchsia_input::Key::Space));

        t.update(fidl_fuchsia_ui_input3::KeyEventType::Cancel, fidl_fuchsia_input::Key::Space);
        assert_eq!(false, t.is_pressed(&fidl_fuchsia_input::Key::Space));
    }

    #[test]
    fn key_state_tracker_any_and_all() {
        let mut t = KeyState::new();

        assert_eq!(false, t.pressed_any(&vec![]));
        assert_eq!(true, t.pressed_all(&vec![]));

        t.update(fidl_fuchsia_ui_input3::KeyEventType::Pressed, fidl_fuchsia_input::Key::Space);
        t.update(fidl_fuchsia_ui_input3::KeyEventType::Pressed, fidl_fuchsia_input::Key::Tab);

        assert_eq!(
            true,
            t.pressed_any(&vec![
                fidl_fuchsia_input::Key::LeftShift,
                fidl_fuchsia_input::Key::Space,
            ])
        );
        assert_eq!(
            false,
            t.pressed_any(&vec![
                fidl_fuchsia_input::Key::RightShift,
                fidl_fuchsia_input::Key::LeftShift
            ])
        );
        assert_eq!(true, t.pressed_all(&vec![fidl_fuchsia_input::Key::Space,]));
        assert_eq!(
            true,
            t.pressed_all(&vec![fidl_fuchsia_input::Key::Space, fidl_fuchsia_input::Key::Tab,])
        );
    }

    #[test_case(
        fidl_fuchsia_input::Key::A,
        ModifierState{..Default::default()},
        Some(fidl_fuchsia_ui_input3::KeyMeaning::Codepoint(97));
        "test basic mapping")
    ]
    #[test_case(
        fidl_fuchsia_input::Key::A,
        ModifierState{left_shift:true,..Default::default()},
        Some(fidl_fuchsia_ui_input3::KeyMeaning::Codepoint(65));
        "test basic mapping - capital letter")
    ]
    fn test_keymap_apply(
        key: fidl_fuchsia_input::Key,
        modifier_state: ModifierState,
        expected: Option<fidl_fuchsia_ui_input3::KeyMeaning>,
    ) {
        let actual = US_QWERTY.apply(key, &modifier_state);
        assert_eq!(expected, actual, "expected: {:?}, actual: {:?}", expected, actual);
    }
}
