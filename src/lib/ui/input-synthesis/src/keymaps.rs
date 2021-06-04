// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::usages;
use anyhow::{format_err, Result};
use fidl_fuchsia_input;
use fidl_fuchsia_ui_input3;
use fuchsia_syslog::fx_log_err;
use lazy_static::lazy_static;
use std::collections;
use std::convert::Into;

lazy_static! {
    /// A US QWERTY keymap.
    pub static ref US_QWERTY: Keymap<'static> = Keymap::new(&QWERTY_MAP);

    /// A FR AZERTY keymap.
    pub static ref FR_AZERTY: Keymap<'static> = Keymap::new(&FR_AZERTY_MAP);

    /// Standard [qwerty] keymap.
    ///
    /// The value of this array at index `u`, where `u` is the usage, can be:
    ///
    ///  * `None` if the key maps to no `char` (Esc key)
    ///  * `Some((c, None))` if the key maps to `c`, but does not map to any `char` when shift is pressed
    ///  * `Some((c, Some(cs)))` if the key maps to `c` when shift is not pressed and to `cs` when it is
    ///    pressed
    ///
    /// [qwerty]: https://en.wikipedia.org/wiki/Keyboard_layout#QWERTY-based_Latin-script_keyboard_layouts
    static ref QWERTY_MAP: Vec<Option<KeyLevels>> = vec![
        // 0x00
        None,
        None,
        None,
        None,
        // HID_USAGE_KEY_A
        Some(('a', Some('A'), true).into()),
        Some(('b', Some('B'), true).into()),
        Some(('c', Some('C'), true).into()),
        Some(('d', Some('D'), true).into()),
        // 0x08
        Some(('e', Some('E'), true).into()),
        Some(('f', Some('F'), true).into()),
        Some(('g', Some('G'), true).into()),
        Some(('h', Some('H'), true).into()),
        // 0x0c
        Some(('i', Some('I'), true).into()),
        Some(('j', Some('J'), true).into()),
        Some(('k', Some('K'), true).into()),
        Some(('l', Some('L'), true).into()),
        // 0x10
        Some(('m', Some('M'), true).into()),
        Some(('n', Some('N'), true).into()),
        Some(('o', Some('O'), true).into()),
        Some(('p', Some('P'), true).into()),
        // 0x14
        Some(('q', Some('Q'), true).into()),
        Some(('r', Some('R'), true).into()),
        Some(('s', Some('S'), true).into()),
        Some(('t', Some('T'), true).into()),
        // 0x18
        Some(('u', Some('U'), true).into()),
        Some(('v', Some('V'), true).into()),
        Some(('w', Some('W'), true).into()),
        Some(('x', Some('X'), true).into()),
        // 0x1c
        Some(('y', Some('Y'), true).into()),
        Some(('z', Some('Z'), true).into()),
        Some(('1', Some('!')).into()),
        Some(('2', Some('@')).into()),
        // 0x20
        Some(('3', Some('#')).into()),
        Some(('4', Some('$')).into()),
        Some(('5', Some('%')).into()),
        Some(('6', Some('^')).into()),
        // 0x24
        Some(('7', Some('&')).into()),
        Some(('8', Some('*')).into()),
        Some(('9', Some('(')).into()),
        Some(('0', Some(')')).into()),
        // 0x28
        None,
        None,
        None,
        None,
        // 0x2c
        Some((' ', Some(' ')).into()),
        Some(('-', Some('_')).into()),
        Some(('=', Some('+')).into()),
        Some(('[', Some('{')).into()),
        // 0x30
        Some((']', Some('}')).into()),
        Some(('\\', Some('|')).into()),
        None,
        Some((';', Some(':')).into()),
        // 0x34
        Some(('\'', Some('"')).into()),
        Some(('`', Some('~')).into()),
        Some((',', Some('<')).into()),
        Some(('.', Some('>')).into()),
        // 0x38
        Some(('/', Some('?')).into()),
        None,
        None,
        None,
        // 0x3c
        None,
        None,
        None,
        None,
        // 0x40
        None,
        None,
        None,
        None,
        // 0x44
        None,
        None,
        None,
        None,
        // 0x48
        None,
        None,
        None,
        None,
        // 0x4c
        None,
        None,
        None,
        None,
        // 0x50
        None,
        None,
        None,
        None,
        // 0x54
        Some(('/', None).into()),
        Some(('*', None).into()),
        Some(('-', None).into()),
        Some(('+', None).into()),
        // 0x58
        None,
        Some(('1', None).into()),
        Some(('2', None).into()),
        Some(('3', None).into()),
        // 0x5c
        Some(('4', None).into()),
        Some(('5', None).into()),
        Some(('6', None).into()),
        Some(('7', None).into()),
        // 0x60
        Some(('8', None).into()),
        Some(('9', None).into()),
        Some(('0', None).into()),
        Some(('.', None).into()),
    ];

    /// TODO(75723): This map is incomplete, and is here only temporarily for
    /// kicks.
    static ref FR_AZERTY_MAP: Vec<Option<KeyLevels>> = vec![
        // 0x00
        None,
        None,
        None,
        None,
        // HID_USAGE_KEY_A
        Some(('q', Some('Q'), true).into()),
        Some(('b', Some('B'), true).into()),
        Some(('c', Some('C'), true).into()),
        Some(('d', Some('D'), true).into()),
        // 0x08
        Some(('e', Some('E'), true).into()),
        Some(('f', Some('F'), true).into()),
        Some(('g', Some('G'), true).into()),
        Some(('h', Some('H'), true).into()),
        // 0x0c
        Some(('i', Some('I'), true).into()),
        Some(('j', Some('J'), true).into()),
        Some(('k', Some('K'), true).into()),
        Some(('l', Some('L'), true).into()),
        // 0x10
        Some((',', Some('?'), true).into()),
        Some(('n', Some('N'), true).into()),
        Some(('o', Some('O'), true).into()),
        Some(('p', Some('P'), true).into()),
        // 0x14
        Some(('a', Some('A'), true).into()),
        Some(('r', Some('R'), true).into()),
        Some(('s', Some('S'), true).into()),
        Some(('t', Some('T'), true).into()),
        // 0x18
        Some(('u', Some('U'), true).into()),
        Some(('v', Some('V'), true).into()),
        Some(('z', Some('Z'), true).into()),
        Some(('x', Some('X'), true).into()),
        // 0x1c
        Some(('y', Some('Y'), true).into()),
        Some(('w', Some('W'), true).into()),
        Some(('&', Some('1')).into()),
        Some(('é', Some('2')).into()),
        // 0x20
        Some(('"', Some('3')).into()),
        Some(('\'', Some('4')).into()),
        Some(('(', Some('5')).into()),
        Some(('-', Some('6')).into()),
        // 0x24
        Some(('è', Some('7')).into()),
        Some(('—', Some('8')).into()),
        Some(('ç', Some('9')).into()),
        Some(('à', Some('0')).into()),
        // 0x28
        None,
        None,
        None,
        None,
        // 0x2c
        Some((' ', Some(' ')).into()),
        Some((')', Some('°')).into()),
        Some(('=', Some('+')).into()),
        Some(('\u{0302}', Some('\u{0308}')).into()),  // Unicode combining characters circumflex and dieresis.
        // 0x30
        Some(('$', Some('£')).into()),
        Some(('\\', Some('|')).into()),  // Not present on French Azerty?
        None,
        Some(('m', Some('M')).into()),
        // 0x34
        Some(('\'', Some('"')).into()),
        Some(('²', None).into()),
        Some((';', Some('.')).into()),
        Some((':', Some('/')).into()),
        // 0x38
        Some(('!', Some('§')).into()),
        None,
        None,
        None,
        // 0x3c
        None,
        None,
        None,
        None,
        // 0x40
        None,
        None,
        None,
        None,
        // 0x44
        None,
        None,
        None,
        None,
        // 0x48
        None,
        None,
        None,
        None,
        // 0x4c
        None,
        None,
        None,
        None,
        // 0x50
        None,
        None,
        None,
        None,
        // 0x54
        Some(('/', None).into()),
        Some(('*', None).into()),
        Some(('-', None).into()),
        Some(('+', None).into()),
        // 0x58
        None,
        Some(('1', None).into()),
        Some(('2', None).into()),
        Some(('3', None).into()),
        // 0x5c
        Some(('4', None).into()),
        Some(('5', None).into()),
        Some(('6', None).into()),
        Some(('7', None).into()),
        // 0x60
        Some(('8', None).into()),
        Some(('9', None).into()),
        Some(('0', None).into()),
        Some(('.', None).into()),
    ];
}

/// Gets a keymap based on the supplied `keymap` selector.  If no keymap is
/// found the fallback is always US QWERTY.
pub fn select_keymap(keymap: &Option<String>) -> &Keymap<'_> {
    match keymap {
        Some(ref k) if k == "FR_AZERTY" => &FR_AZERTY,
        _ => &US_QWERTY,
    }
}

/// A codepoint returned by [hid_usage_to_code_point] for HID usages that do
/// not have an associated code point, e.g. Alt.
pub(crate) const EMPTY_CODEPOINT: u32 = 0;

/// Levels corresponding to each defined key.
pub struct KeyLevels {
    /// The base, unshifted character.
    pub(crate) ch: char,
    /// The shifted character, if present
    pub(crate) shift_ch: Option<char>,
    /// Is it a letter?  Letters are affected by Caps Lock, while other keys
    /// are not.
    is_letter: bool,
}

impl From<(char, Option<char>)> for KeyLevels {
    fn from(s: (char, Option<char>)) -> Self {
        KeyLevels { ch: s.0, shift_ch: s.1, is_letter: false }
    }
}

impl From<(char, Option<char>, bool)> for KeyLevels {
    fn from(s: (char, Option<char>, bool)) -> Self {
        KeyLevels { ch: s.0, shift_ch: s.1, is_letter: s.2 }
    }
}

impl KeyLevels {
    fn get_key(&self, m: &ModifierState) -> Option<char> {
        if m.is_caps_lock_active() && self.is_letter || m.is_shift_active() {
            return self.shift_ch;
        }
        Some(self.ch)
    }
}

/// A Us Qwerty keymap
pub struct Keymap<'a> {
    map: &'a [Option<KeyLevels>],
}

impl AsRef<[Option<KeyLevels>]> for Keymap<'_> {
    fn as_ref(&self) -> &[Option<KeyLevels>] {
        self.map
    }
}

impl<'a> Keymap<'a> {
    /// Creates a new keymap.
    fn new(map: &'a [Option<KeyLevels>]) -> Self {
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
