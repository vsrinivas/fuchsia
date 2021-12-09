// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{format_err, Result};
use fidl_fuchsia_input::{self, Key};
use fidl_fuchsia_ui_input3::{KeyEventType, KeyMeaning, LockState, Modifiers, NonPrintableKey};
use fuchsia_syslog::fx_log_err;
use lazy_static::lazy_static;
use std::collections::{self, HashMap};

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
        key: Key,
        modifier_state: &impl ModifierChecker,
        lock_state: &impl LockStateChecker,
    ) -> Option<KeyMeaning> {
        let hid_usage = usages::input3_key_to_hid_usage(key);

        match key {
            // Nonprintable keys get their own key meaning.
            Key::Enter => Some(KeyMeaning::NonPrintableKey(NonPrintableKey::Enter)),
            Key::Tab => Some(KeyMeaning::NonPrintableKey(NonPrintableKey::Tab)),
            Key::Backspace => Some(KeyMeaning::NonPrintableKey(NonPrintableKey::Backspace)),
            // Printable keys get code points as key meanings.
            _ => self
                .hid_usage_to_code_point(hid_usage, modifier_state, lock_state)
                .map(KeyMeaning::Codepoint)
                .map_err(|e| {
                    fx_log_err!(
                        concat!(
                            "keymaps::Keymap::apply: ",
                            "Could not convert HID usage to code point: {:?}, ",
                            "modifiers: {:?} ",
                            "lock_state: {:?}",
                        ),
                        &hid_usage,
                        modifier_state,
                        lock_state,
                    );
                    e
                })
                .ok(),
        }
    }

    /// Converts a HID usage for a key to a Unicode code point where such a code point exists, based on
    /// a US QWERTY keyboard layout.  Returns EMPTY_CODEPOINT if a code point does not exist (e.g. Alt),
    /// and an error in case the mapping somehow fails.
    pub fn hid_usage_to_code_point(
        &self,
        hid_usage: u32,
        modifier_state: &impl ModifierChecker,
        lock_state: &impl LockStateChecker,
    ) -> Result<u32> {
        if (hid_usage as usize) < self.map.len() {
            if let Some(ref map_entry) = self.map[hid_usage as usize] {
                map_entry
                    .get_key(modifier_state, lock_state)
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
        let modifier_state = ModifierState::new()
            .with_if(Modifiers::LeftShift, shift)
            .with_if(Modifiers::RightShift, shift);
        let lock_state = LockStateKeys::new().with_if(LockState::CapsLock, caps_lock);
        let code_point = self.hid_usage_to_code_point(hid_usage, &modifier_state, &lock_state);
        match code_point {
            Ok(EMPTY_CODEPOINT) => None,
            Ok(c) => Some(c),
            Err(_) => None,
        }
    }
}

/// A trait for something that can be tested for modifier presence.
pub trait ModifierChecker: std::fmt::Debug {
    fn test(&self, value: Modifiers) -> bool;
}

/// Tracks the current state of "significant" modifier keys.
///
/// Currently, a modifier key is "significant" if it affects the mapping of a
/// Fuchsia key to a key meaning.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct ModifierState {
    /// The internal modifier state.
    state: Modifiers,
}

impl Default for ModifierState {
    fn default() -> Self {
        Self { state: Modifiers::from_bits_allow_unknown(0) }
    }
}

impl ModifierChecker for ModifierState {
    /// Test if the modifier state contains the modifiers from `value`.
    fn test(&self, value: Modifiers) -> bool {
        self.state.contains(value)
    }
}

impl ModifierState {
    /// Creates a new [ModifierState] initializes with the default state (no
    /// modifiers are actuated).
    pub fn new() -> Self {
        Default::default()
    }

    /// Unconditionally adds the given modifier into the modifier state as
    /// actuated.
    ///
    /// Returns `Self` for chaining.
    pub fn with(self, value: Modifiers) -> Self {
        let mut state = self.state;
        state.insert(value);

        // For "sided" modifiers, maintain the "side-less" bit invariant too.
        match value {
            Modifiers::LeftShift | Modifiers::RightShift => {
                state.insert(Modifiers::Shift);
            }
            Modifiers::LeftAlt | Modifiers::RightAlt => {
                state.insert(Modifiers::Alt);
            }
            Modifiers::LeftMeta | Modifiers::RightMeta => {
                state.insert(Modifiers::Meta);
            }
            Modifiers::LeftCtrl | Modifiers::RightCtrl => {
                state.insert(Modifiers::Ctrl);
            }
            _ => {}
        }
        Self { state }
    }

    /// Retrieves the modifier state.
    pub fn get_state(&self) -> Modifiers {
        self.state.clone()
    }

    /// Adds `modifier` into the modifier state as actuated, if `set` is true.
    /// Otherwise makes no changes to [ModifierState].
    ///
    /// Returns `Self` for chaining.
    pub fn with_if(self, value: Modifiers, set: bool) -> Self {
        match set {
            false => self,
            true => self.with(value),
        }
    }

    /// Update the modifier tracker state with this event.
    pub fn update(&mut self, event: KeyEventType, key: Key) {
        match event {
            KeyEventType::Pressed => match key {
                Key::CapsLock => self.state.insert(Modifiers::CapsLock),
                Key::NumLock => self.state.insert(Modifiers::NumLock),
                Key::ScrollLock => self.state.insert(Modifiers::ScrollLock),
                // These modifiers are not defined yet in Key.
                // Key::Function
                // Key::Symbol
                // Key::AltGraph

                // For "sided" modifiers, we must also maintain the "side-less"
                // bit. Here, and everywhere below.
                Key::LeftShift => {
                    self.state.insert(Modifiers::LeftShift | Modifiers::Shift);
                }
                Key::RightShift => {
                    self.state.insert(Modifiers::RightShift | Modifiers::Shift);
                }
                Key::LeftAlt => {
                    self.state.insert(Modifiers::LeftAlt | Modifiers::Alt);
                }
                Key::RightAlt => {
                    self.state.insert(Modifiers::RightAlt | Modifiers::Alt);
                }
                Key::LeftMeta => {
                    self.state.insert(Modifiers::LeftMeta | Modifiers::Meta);
                }
                Key::RightMeta => {
                    self.state.insert(Modifiers::RightMeta | Modifiers::Meta);
                }
                Key::LeftCtrl => {
                    self.state.insert(Modifiers::LeftCtrl | Modifiers::Ctrl);
                }
                Key::RightCtrl => {
                    self.state.insert(Modifiers::RightCtrl | Modifiers::Ctrl);
                }
                _ => {}
            },
            KeyEventType::Released => match key {
                Key::CapsLock => {
                    self.state.remove(Modifiers::CapsLock);
                }
                Key::NumLock => self.state.remove(Modifiers::NumLock),
                Key::ScrollLock => self.state.remove(Modifiers::ScrollLock),

                Key::LeftShift => {
                    self.state.remove(Modifiers::LeftShift);
                    if !self.state.contains(Modifiers::RightShift) {
                        self.state.remove(Modifiers::Shift);
                    }
                }
                Key::RightShift => {
                    self.state.remove(Modifiers::RightShift);
                    if !self.test(Modifiers::LeftShift) {
                        self.state.remove(Modifiers::Shift);
                    }
                }
                Key::LeftAlt => {
                    self.state.remove(Modifiers::LeftAlt);
                    if !self.state.contains(Modifiers::RightAlt) {
                        self.state.remove(Modifiers::Alt);
                    }
                }
                Key::RightAlt => {
                    self.state.remove(Modifiers::RightAlt);
                    if !self.test(Modifiers::LeftAlt) {
                        self.state.remove(Modifiers::Alt);
                    }
                }
                Key::LeftMeta => {
                    self.state.remove(Modifiers::LeftMeta);
                    if !self.state.contains(Modifiers::RightMeta) {
                        self.state.remove(Modifiers::Meta);
                    }
                }
                Key::RightMeta => {
                    self.state.remove(Modifiers::RightMeta);
                    if !self.test(Modifiers::LeftMeta) {
                        self.state.remove(Modifiers::Meta);
                    }
                }
                Key::LeftCtrl => {
                    self.state.remove(Modifiers::LeftCtrl);
                    if !self.state.contains(Modifiers::RightCtrl) {
                        self.state.remove(Modifiers::Ctrl);
                    }
                }
                Key::RightCtrl => {
                    self.state.remove(Modifiers::RightCtrl);
                    if !self.test(Modifiers::LeftCtrl) {
                        self.state.remove(Modifiers::Ctrl);
                    }
                }
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
}

// The state of each lock key.
#[derive(Debug, Hash)]
enum State {
    // Lock was pressed.  (aka S1, see below)
    LockPressed,
    // Lock was pressed and released. (aka S2)
    LockPressedAndReleased,
    // Lock was pressed for the second time in a row.  (aka S3)
    LockPressedSecondTime,
}

/// A lock state checker.
pub trait LockStateChecker: std::fmt::Debug {
    fn test(&self, value: LockState) -> bool;
}

/// The lock state of the lock keys.
///
/// Consult the state diagram below for the intended state transition.  The
/// state encoding is given in [State].
///
/// ```ignore
/// Key    """""""\_________/"""""""""""\_________/""""
/// State  -------<    S1  ><     S2   ><    S3   >----
/// ```
///
/// Any other state is not explicitly accounted for: the state of "the lock is
/// not active" is encoded by not having a record of the key state.  This
/// allows us to implement the state machine for an arbitrary number of lock
/// keys.
#[derive(Debug)]
pub struct LockStateKeys {
    state: HashMap<LockState, State>,
}

impl Default for LockStateKeys {
    fn default() -> Self {
        LockStateKeys { state: HashMap::new() }
    }
}

impl LockStateChecker for LockStateKeys {
    /// Returns true if the lock state value is set.
    fn test(&self, value: LockState) -> bool {
        self.state.contains_key(&value)
    }
}

impl LockStateKeys {
    /// Creates a new [LockStateKeys] initializes with the default state (no
    /// modifiers are actuated).
    pub fn new() -> Self {
        Default::default()
    }

    /// Unconditionally adds the given lock state as actuated.
    ///
    /// Returns `Self` for chaining.
    pub fn with(self, value: LockState) -> Self {
        let mut state = self.state;
        state.insert(value, State::LockPressedAndReleased);
        Self { state }
    }

    /// Adds `value` into the modifier state as actuated, if `set` is true.
    /// Otherwise makes no changes to [LockStateKeys].
    ///
    /// Returns `Self` for chaining.
    pub fn with_if(self, value: LockState, set: bool) -> Self {
        match set {
            false => self,
            true => self.with(value),
        }
    }

    /// Update the modifier tracker state with this event.
    pub fn update(&mut self, event: KeyEventType, key: Key) {
        let lock_key = match key {
            Key::CapsLock => LockState::CapsLock,
            Key::NumLock => LockState::NumLock,
            Key::ScrollLock => LockState::ScrollLock,
            // FUNCTION_LOCK
            // SYMBOL_LOCK
            // etc.
            _ => LockState::from_bits_allow_unknown(0),
        };
        if lock_key == LockState::from_bits_allow_unknown(0) {
            return;
        }
        let lock_state = self.state.get(&lock_key).map(|e| e.clone());
        match (event, lock_state) {
            (KeyEventType::Pressed, None) => {
                self.state.insert(lock_key, State::LockPressed);
            }
            (KeyEventType::Pressed, Some(State::LockPressedAndReleased)) => {
                self.state.insert(lock_key, State::LockPressedSecondTime);
            }
            (KeyEventType::Released, Some(State::LockPressed)) => {
                self.state.insert(lock_key, State::LockPressedAndReleased);
            }
            (KeyEventType::Released, Some(State::LockPressedSecondTime)) => {
                self.state.remove(&lock_key);
            }

            // These should not happen.
            (KeyEventType::Pressed, Some(State::LockPressed))
            | (KeyEventType::Pressed, Some(State::LockPressedSecondTime))
            | (KeyEventType::Released, None)
            | (KeyEventType::Released, Some(State::LockPressedAndReleased)) => {
                fx_log_err!(
                    "unexpected state transition: event: {:?}, key: {:?}, state: {:?}",
                    &event,
                    &key,
                    &self.state
                );
            }

            // SYNC and CANCEL don't change the lock state.
            (_, __) => {}
        }
    }

    /// Gets the recorded lock state.
    pub fn get_state(&self) -> LockState {
        self.state
            .keys()
            .map(|k| k.clone())
            .fold(LockState::from_bits_allow_unknown(0), |acc, k| acc | k)
    }
}

/// Tracks the current state of all the keyboard keys.
///
/// This is repetitive code, so perhaps better handle it here.
#[derive(Debug, Clone, Default)]
pub struct KeyState {
    pressed_keys: collections::HashSet<Key>,
}

impl KeyState {
    /// Creates a new [KeyState]
    pub fn new() -> Self {
        KeyState { pressed_keys: collections::HashSet::new() }
    }

    /// Updates the key tracking state with the given key event pair.
    pub fn update(&mut self, event: KeyEventType, key: Key) {
        match event {
            KeyEventType::Pressed | KeyEventType::Sync => {
                self.pressed_keys.insert(key);
            }
            KeyEventType::Released | KeyEventType::Cancel => {
                self.pressed_keys.remove(&key);
            }
        }
    }

    /// Returns true if `key` is noted as pressed.
    pub fn is_pressed(&self, key: &Key) -> bool {
        self.pressed_keys.contains(key)
    }

    /// Returns `true` if at least one key from `keys` is pressed.
    pub fn pressed_any(&self, keys: &[Key]) -> bool {
        keys.iter().any(|k| self.is_pressed(k))
    }

    /// Returns `true` if all keys from `keys` are pressed.
    pub fn pressed_all(&self, keys: &[Key]) -> bool {
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
                &ModifierState::new(),
                &LockStateKeys::new().with(LockState::CapsLock),
            )?
        );
        assert_eq!(
            '!' as u32,
            US_QWERTY.hid_usage_to_code_point(
                HID_USAGE_KEY_1,
                &ModifierState::new().with(Modifiers::LeftShift),
                &LockStateKeys::new(),
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
                &ModifierState::new(),
                &LockStateKeys::new(),
            )?
        );
        assert_eq!(
            'a' as u32,
            US_QWERTY.hid_usage_to_code_point(
                HID_USAGE_KEY_A,
                &ModifierState::new().with(Modifiers::CapsLock),
                &LockStateKeys::new(),
            )?
        );
        assert_eq!(
            'A' as u32,
            US_QWERTY.hid_usage_to_code_point(
                HID_USAGE_KEY_A,
                &ModifierState::new(),
                &LockStateKeys::new().with(LockState::CapsLock),
            )?
        );
        assert_eq!(
            'A' as u32,
            US_QWERTY.hid_usage_to_code_point(
                HID_USAGE_KEY_A,
                &ModifierState::new().with(Modifiers::RightShift),
                &LockStateKeys::new(),
            )?
        );
        assert_eq!(
            'A' as u32,
            US_QWERTY.hid_usage_to_code_point(
                HID_USAGE_KEY_A,
                &ModifierState::new().with(Modifiers::LeftShift),
                &LockStateKeys::new(),
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
                &ModifierState { ..Default::default() },
                &LockStateKeys::new(),
            )?
        );
        assert_eq!(
            'a' as u32,
            US_DVORAK.hid_usage_to_code_point(
                HID_USAGE_KEY_A,
                &ModifierState::new().with(Modifiers::CapsLock),
                &LockStateKeys::new(),
            )?
        );
        assert_eq!(
            'A' as u32,
            US_DVORAK.hid_usage_to_code_point(
                HID_USAGE_KEY_A,
                &ModifierState::new(),
                &LockStateKeys::new().with(LockState::CapsLock),
            )?
        );
        assert_eq!(
            'x' as u32,
            US_DVORAK.hid_usage_to_code_point(
                HID_USAGE_KEY_B,
                &ModifierState::new(),
                &LockStateKeys::new(),
            )?
        );
        assert_eq!(
            'X' as u32,
            US_DVORAK.hid_usage_to_code_point(
                HID_USAGE_KEY_B,
                &ModifierState::new(),
                &LockStateKeys::new().with(LockState::CapsLock),
            )?
        );
        assert_eq!(
            'X' as u32,
            US_DVORAK.hid_usage_to_code_point(
                HID_USAGE_KEY_B,
                &ModifierState::new().with(Modifiers::LeftShift),
                &LockStateKeys::new(),
            )?
        );
        assert_eq!(
            'n' as u32,
            US_DVORAK.hid_usage_to_code_point(
                HID_USAGE_KEY_L,
                &ModifierState::new(),
                &LockStateKeys::new(),
            )?
        );
        assert_eq!(
            'N' as u32,
            US_DVORAK.hid_usage_to_code_point(
                HID_USAGE_KEY_L,
                &ModifierState::new().with(Modifiers::LeftShift),
                &LockStateKeys::new(),
            )?
        );
        assert_eq!(
            'N' as u32,
            US_DVORAK.hid_usage_to_code_point(
                HID_USAGE_KEY_L,
                &ModifierState::new(),
                &LockStateKeys::new().with(LockState::CapsLock),
            )?
        );
        Ok(())
    }

    // CapsLock ______/""""""""\_______/"""""""""\_______
    // Modifier ______/""""""""\_______/"""""""""\______
    #[test_case(Key::CapsLock, Modifiers::CapsLock; "CapsLock")]
    #[test_case(Key::NumLock, Modifiers::NumLock; "NumLock")]
    #[test_case(Key::ScrollLock, Modifiers::ScrollLock; "ScrollLock")]
    // Key::Function
    // Key::Symbol
    // Key::AltGraph
    // Test "sided" modifiers.
    #[test_case(Key::RightShift, Modifiers::RightShift|Modifiers::Shift; "RightShift")]
    #[test_case(Key::LeftShift, Modifiers::LeftShift|Modifiers::Shift; "LeftShift")]
    #[test_case(Key::RightAlt, Modifiers::RightAlt|Modifiers::Alt; "RightAlt")]
    #[test_case(Key::LeftAlt, Modifiers::LeftAlt|Modifiers::Alt; "LeftAlt")]
    #[test_case(Key::RightMeta, Modifiers::RightMeta|Modifiers::Meta; "RightMeta")]
    #[test_case(Key::LeftMeta, Modifiers::LeftMeta|Modifiers::Meta; "LeftMeta")]
    #[test_case(Key::RightCtrl, Modifiers::RightCtrl|Modifiers::Ctrl; "RightCtrl")]
    #[test_case(Key::LeftCtrl, Modifiers::LeftCtrl|Modifiers::Ctrl; "LeftCtrl")]
    fn test_caps_lock_modifier(key: Key, modifier: Modifiers) {
        let mut modifier_state: ModifierState = Default::default();
        assert!(!modifier_state.test(modifier));

        modifier_state.update(KeyEventType::Pressed, key);
        assert!(modifier_state.test(modifier));

        modifier_state.update(KeyEventType::Released, key);
        assert!(!modifier_state.test(modifier));

        modifier_state.update(KeyEventType::Pressed, key);
        assert!(modifier_state.test(modifier));

        modifier_state.update(KeyEventType::Released, key);
        assert!(!modifier_state.test(modifier));
    }

    // Interleaved use of sided modifiers sets and resets the "non-sided"
    // modifier bit correctly.
    //
    // KeyA      """""\____________/""""""""""""""""
    //                :
    // KeyB      """""""""""\_____________/"""""""""
    //                :                   :
    // Modifier  """""\___________________/"""""""""
    //
    // KeyA is the first of the two sided keys, and KeyB is the second of the
    // two sided keys.
    #[test_case(Key::LeftShift, Key::RightShift, Modifiers::Shift; "Shift/LR")]
    #[test_case(Key::RightShift, Key::LeftShift, Modifiers::Shift; "Shift/RL")]
    #[test_case(Key::LeftAlt, Key::RightAlt, Modifiers::Alt; "Alt/LR")]
    #[test_case(Key::RightAlt, Key::LeftAlt, Modifiers::Alt; "Alt/RL")]
    #[test_case(Key::LeftMeta, Key::RightMeta, Modifiers::Meta; "Meta/LR")]
    #[test_case(Key::RightMeta, Key::LeftMeta, Modifiers::Meta; "Meta/RL")]
    #[test_case(Key::RightCtrl, Key::LeftCtrl, Modifiers::Ctrl; "Ctrl/RL")]
    #[test_case(Key::LeftCtrl, Key::RightCtrl, Modifiers::Ctrl; "Ctrl/LR")]
    fn test_sided_keys(key_a: Key, key_b: Key, modifier: Modifiers) {
        let mut modifier_state = ModifierState::new();
        assert!(
            !modifier_state.test(modifier),
            "state: {:?}, key_a: {:?}, key_b: {:?}",
            &modifier_state,
            &key_a,
            &key_b
        );

        modifier_state.update(KeyEventType::Pressed, key_a);
        assert!(
            modifier_state.test(modifier),
            "state: {:?}, key_a: {:?}, key_b: {:?}",
            &modifier_state,
            &key_a,
            &key_b
        );

        modifier_state.update(KeyEventType::Pressed, key_b);
        assert!(
            modifier_state.test(modifier),
            "state: {:?}, key_a: {:?}, key_b: {:?}",
            &modifier_state,
            &key_a,
            &key_b
        );

        modifier_state.update(KeyEventType::Released, key_a);
        assert!(
            modifier_state.test(modifier),
            "state: {:?}, key_a: {:?}, key_b: {:?}",
            &modifier_state,
            &key_a,
            &key_b
        );

        modifier_state.update(KeyEventType::Released, key_b);
        assert!(
            !modifier_state.test(modifier),
            "state: {:?}, key_a: {:?}, key_b: {:?}",
            &modifier_state,
            &key_a,
            &key_b
        );
    }

    // Check that the lock state is set and reset properly.
    //
    // Key       ______/""""""""\_______/"""""""""\_______
    // LockState ______/""""""""""""""""""""""""""\______
    #[test_case(Key::CapsLock, LockState::CapsLock; "CapsLock")]
    #[test_case(Key::NumLock, LockState::NumLock; "NumLock")]
    #[test_case(Key::ScrollLock, LockState::ScrollLock; "ScrollLock")]
    fn test_lock_state(key: Key, lock_state: LockState) {
        let mut state: LockStateKeys = Default::default();
        assert!(!state.test(lock_state));
        assert_eq!(state.get_state(), LockState::from_bits_allow_unknown(0));

        state.update(KeyEventType::Pressed, key);
        assert!(state.test(lock_state), "{:?}", state.get_state());
        assert_eq!(state.get_state(), lock_state);

        state.update(KeyEventType::Released, key);
        assert!(state.test(lock_state), "{:?}", state.get_state());
        assert_eq!(state.get_state(), lock_state);

        state.update(KeyEventType::Pressed, key);
        assert!(state.test(lock_state), "{:?}", state.get_state());
        assert_eq!(state.get_state(), lock_state);

        state.update(KeyEventType::Released, key);
        assert!(!state.test(lock_state), "{:?}", state.get_state());
        assert_eq!(state.get_state(), LockState::from_bits_allow_unknown(0));
    }

    #[test]
    fn test_modifier_tracker() {
        let mut modifier_state: ModifierState = Default::default();
        assert!(!modifier_state.test(Modifiers::Shift));

        modifier_state.update(KeyEventType::Pressed, Key::LeftShift);
        assert!(modifier_state.test(Modifiers::Shift));
        modifier_state.update(KeyEventType::Released, Key::LeftShift);
        assert!(!modifier_state.test(Modifiers::Shift));

        modifier_state.update(KeyEventType::Pressed, Key::RightShift);
        assert!(modifier_state.test(Modifiers::Shift));
        modifier_state.update(KeyEventType::Released, Key::RightShift);
        assert!(!modifier_state.test(Modifiers::Shift));

        modifier_state.update(KeyEventType::Pressed, Key::CapsLock);
        assert!(!modifier_state.test(Modifiers::Shift));
        assert!(modifier_state.test(Modifiers::CapsLock));
        modifier_state.update(KeyEventType::Released, Key::CapsLock);
        assert!(!modifier_state.test(Modifiers::Shift));
        assert!(!modifier_state.test(Modifiers::CapsLock));
        modifier_state.update(KeyEventType::Pressed, Key::CapsLock);
        assert!(!modifier_state.test(Modifiers::Shift));
        assert!(modifier_state.test(Modifiers::CapsLock));
        modifier_state.update(KeyEventType::Released, Key::CapsLock);
        assert!(!modifier_state.test(Modifiers::Shift));
        assert!(!modifier_state.test(Modifiers::CapsLock));
    }

    // CapsLock            ________/""""""""""\_______/"""""\_____
    // LeftShift           ____________/"""""""""""\______________
    // is_shift_active     ____________/"""""""""""\______________
    // is_caps_lock_active ________/""""""""""\_______/"""""\_____
    #[test]
    fn test_interleaved_caps_lock_and_shift() {
        let mut modifier_state: ModifierState = Default::default();
        assert!(!modifier_state.test(Modifiers::Shift));

        modifier_state.update(KeyEventType::Pressed, Key::CapsLock);
        assert!(!modifier_state.test(Modifiers::Shift));
        assert!(modifier_state.test(Modifiers::CapsLock));

        modifier_state.update(KeyEventType::Pressed, Key::LeftShift);
        assert!(modifier_state.test(Modifiers::Shift));
        assert!(modifier_state.test(Modifiers::CapsLock));

        modifier_state.update(KeyEventType::Released, Key::CapsLock);
        assert!(modifier_state.test(Modifiers::Shift));
        assert!(!modifier_state.test(Modifiers::CapsLock));

        modifier_state.update(KeyEventType::Released, Key::LeftShift);
        // Caps Lock is still active...
        assert!(!modifier_state.test(Modifiers::Shift));
        assert!(!modifier_state.test(Modifiers::CapsLock));

        // Press and release Caps Lock again.
        modifier_state.update(KeyEventType::Pressed, Key::CapsLock);
        assert!(!modifier_state.test(Modifiers::Shift));
        assert!(modifier_state.test(Modifiers::CapsLock));

        modifier_state.update(KeyEventType::Released, Key::CapsLock);
        assert!(!modifier_state.test(Modifiers::Shift));
        assert!(!modifier_state.test(Modifiers::CapsLock));
    }

    #[test]
    fn key_state_tracker() {
        let mut t = KeyState::new();
        assert_eq!(false, t.is_pressed(&Key::Space));
        t.update(KeyEventType::Pressed, Key::Space);
        assert_eq!(true, t.is_pressed(&Key::Space));

        t.update(KeyEventType::Released, Key::Space);

        assert_eq!(false, t.is_pressed(&Key::Space));

        t.update(KeyEventType::Sync, Key::Space);
        assert_eq!(true, t.is_pressed(&Key::Space));

        t.update(KeyEventType::Cancel, Key::Space);
        assert_eq!(false, t.is_pressed(&Key::Space));
    }

    #[test]
    fn key_state_tracker_any_and_all() {
        let mut t = KeyState::new();

        assert_eq!(false, t.pressed_any(&vec![]));
        assert_eq!(true, t.pressed_all(&vec![]));

        t.update(KeyEventType::Pressed, Key::Space);
        t.update(KeyEventType::Pressed, Key::Tab);

        assert_eq!(true, t.pressed_any(&vec![Key::LeftShift, Key::Space,]));
        assert_eq!(false, t.pressed_any(&vec![Key::RightShift, Key::LeftShift]));
        assert_eq!(true, t.pressed_all(&vec![Key::Space,]));
        assert_eq!(true, t.pressed_all(&vec![Key::Space, Key::Tab,]));
    }

    #[test_case(
        Key::A,
        ModifierState::new(),
        LockStateKeys::new(),
        Some(KeyMeaning::Codepoint(97));
        "test basic mapping")
    ]
    #[test_case(
        Key::A,
        ModifierState::new().with(Modifiers::LeftShift),
        LockStateKeys::new(),
        Some(KeyMeaning::Codepoint(65));
        "test basic mapping - capital letter")
    ]
    fn test_keymap_apply(
        key: Key,
        modifier_state: ModifierState,
        lock_state: LockStateKeys,
        expected: Option<KeyMeaning>,
    ) {
        let actual = US_QWERTY.apply(key, &modifier_state, &lock_state);
        assert_eq!(expected, actual, "expected: {:?}, actual: {:?}", expected, actual);
    }
}
