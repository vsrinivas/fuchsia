// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{format_err, Result};
use fidl_fuchsia_input::{self, Key};
use fidl_fuchsia_ui_input3::{KeyEventType, KeyMeaning, LockState, Modifiers, NonPrintableKey};
use lazy_static::lazy_static;
use std::collections::{self, HashMap};
use tracing::{debug, error};

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

    /// A US COLEMAK keymap.
    pub static ref US_COLEMAK: Keymap<'static> = Keymap::new(&defs::COLEMAK_MAP);
}

/// Gets a keymap based on the supplied `keymap` selector.  If no keymap is
/// found the fallback is always US QWERTY.
pub fn select_keymap<'a>(keymap: &Option<String>) -> &'a Keymap<'a> {
    match keymap {
        Some(ref k) if k == "FR_AZERTY" => &FR_AZERTY,
        Some(ref k) if k == "US_DVORAK" => &US_DVORAK,
        Some(ref k) if k == "US_COLEMAK" => &US_COLEMAK,
        _ => &US_QWERTY,
    }
}

/// Extracts key meaning in accordance with the Fuchsia key event API specification.
///
/// Key meaning is returned verbatim if defined; otherwise, the US QWERTY keymap is
/// applied to the supplied `key` and the currently active modifiers and lock state.
///
/// These usually come from a `fidl.fuchsia.ui.input3/KeyEvent`, so you can simply
/// pass its components in if you have one. But, a valid `KeyEvent` is not required,
/// and the caller can fill each of the parameters at will.
///
/// If neither the key nor the key meaning are defined, an "unidentified"
/// nonprintable key meaning is returned.
pub fn get_key_meaning(
    key: &Option<Key>,
    key_meaning: &Option<KeyMeaning>,
    lock_state: &Option<LockState>,
    modifiers: &Option<Modifiers>,
) -> KeyMeaning {
    key_meaning.unwrap_or_else(|| {
        // Specification note: If key meaning is unset, then the key meaning
        // must be recovered from the hardware key by applying the US_QWERTY
        // keymap to the hardware key value, using the currently applicable
        // modifier and lock state.
        let lock_state =
            LockStateKeys::new().with(lock_state.unwrap_or(LockState::from_bits_allow_unknown(0)));
        let modifiers =
            ModifierState::new().with(modifiers.unwrap_or(Modifiers::from_bits_allow_unknown(0)));
        let key = key.unwrap_or(Key::Unknown);
        US_QWERTY
            .apply(key, &modifiers, &lock_state)
            .unwrap_or(KeyMeaning::NonPrintableKey(NonPrintableKey::Unidentified))
    })
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

    /// Attaches a fixed [KeyMeaning] to the given [Key] if one exists.
    ///
    /// These are mostly the default key meanings of keys on a US QWERTY keyboard.
    /// As such, we must try the key to key meaning mapping coming from the keymap first,
    /// to allow the keymap the option to move these keys around when this is desired.
    ///
    /// We do not really expect these to happen frequently in standardized keymaps,
    /// but custom keymaps might do this (e.g. mapping Esc to CapsLock), and in
    /// general we should avoid putting arbitrary constraints on key maps if such
    /// are not necessary.
    fn try_into_nonprintable(&self, key: Key) -> Option<KeyMeaning> {
        match key {
            Key::Enter => Some(NonPrintableKey::Enter),
            Key::Tab => Some(NonPrintableKey::Tab),
            Key::Backspace => Some(NonPrintableKey::Backspace),
            Key::Up => Some(NonPrintableKey::Up),
            Key::Down => Some(NonPrintableKey::Down),
            Key::Left => Some(NonPrintableKey::Left),
            Key::Right => Some(NonPrintableKey::Right),
            Key::End => Some(NonPrintableKey::End),
            Key::Home => Some(NonPrintableKey::Home),
            Key::PageUp => Some(NonPrintableKey::PageUp),
            Key::PageDown => Some(NonPrintableKey::PageDown),
            Key::RightAlt => Some(if std::ptr::eq(self, &*FR_AZERTY) {
                // Used for Chromium testing - not yet handled consistently and
                // seriously.
                NonPrintableKey::AltGraph
            } else {
                NonPrintableKey::Alt
            }),
            Key::LeftAlt => Some(NonPrintableKey::Alt),
            Key::RightCtrl => Some(NonPrintableKey::Control),
            Key::LeftCtrl => Some(NonPrintableKey::Control),
            Key::CapsLock => Some(NonPrintableKey::CapsLock),
            Key::LeftShift => Some(NonPrintableKey::Shift),
            Key::RightShift => Some(NonPrintableKey::Shift),
            Key::LeftMeta => Some(NonPrintableKey::Meta),
            Key::RightMeta => Some(NonPrintableKey::Meta),
            Key::NumLock => Some(NonPrintableKey::NumLock),
            Key::ScrollLock => Some(NonPrintableKey::ScrollLock),
            Key::F1 => Some(NonPrintableKey::F1),
            Key::F2 => Some(NonPrintableKey::F2),
            Key::F3 => Some(NonPrintableKey::F3),
            Key::F4 => Some(NonPrintableKey::F4),
            Key::F5 => Some(NonPrintableKey::F5),
            Key::F6 => Some(NonPrintableKey::F6),
            Key::F7 => Some(NonPrintableKey::F7),
            Key::F8 => Some(NonPrintableKey::F8),
            Key::F9 => Some(NonPrintableKey::F9),
            Key::F10 => Some(NonPrintableKey::F10),
            Key::F11 => Some(NonPrintableKey::F11),
            Key::F12 => Some(NonPrintableKey::F12),
            _ => None,
        }
        .map(|k| KeyMeaning::NonPrintableKey(k))
    }

    /// Applies the keymap to the given key.
    pub fn apply(
        &self,
        key: Key,
        modifier_state: &impl ModifierChecker,
        lock_state: &impl LockStateChecker,
    ) -> Option<KeyMeaning> {
        let hid_usage = usages::input3_key_to_hid_usage(key);

        // Try to apply the keymap first. Failing that, try fixed nonprintable
        // keys.
        self.hid_usage_to_code_point(hid_usage, modifier_state, lock_state)
            .ok()
            .and_then(|v| if v == EMPTY_CODEPOINT { None } else { Some(v) })
            .map(KeyMeaning::Codepoint)
            .or_else(|| self.try_into_nonprintable(key))
            .or_else(|| {
                debug!(
                    ?key,
                    ?hid_usage,
                    modifiers = ?modifier_state,
                    ?lock_state,
                    "keymaps::Keymap::apply: no KeyMeaning"
                );
                None
            })
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
            .with_if(Modifiers::LEFT_SHIFT, shift)
            .with_if(Modifiers::RIGHT_SHIFT, shift);
        let lock_state = LockStateKeys::new().with_if(LockState::CAPS_LOCK, caps_lock);
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
            Modifiers::LEFT_SHIFT | Modifiers::RIGHT_SHIFT => {
                state.insert(Modifiers::SHIFT);
            }
            Modifiers::LEFT_ALT | Modifiers::RIGHT_ALT => {
                state.insert(Modifiers::ALT);
            }
            Modifiers::LEFT_META | Modifiers::RIGHT_META => {
                state.insert(Modifiers::META);
            }
            Modifiers::LEFT_CTRL | Modifiers::RIGHT_CTRL => {
                state.insert(Modifiers::CTRL);
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
            // PRESSED event is a regular key press.
            // SYNC event is an update to key presses after a focus regain.
            KeyEventType::Pressed | KeyEventType::Sync => match key {
                Key::CapsLock => self.state.insert(Modifiers::CAPS_LOCK),
                Key::NumLock => self.state.insert(Modifiers::NUM_LOCK),
                Key::ScrollLock => self.state.insert(Modifiers::SCROLL_LOCK),
                // These modifiers are not defined in Key.
                // Key::Function
                // Key::Symbol

                // For "sided" modifiers, we must also maintain the "side-less"
                // bit. Here, and everywhere below.
                Key::LeftShift => {
                    self.state.insert(Modifiers::LEFT_SHIFT | Modifiers::SHIFT);
                }
                Key::RightShift => {
                    self.state.insert(Modifiers::RIGHT_SHIFT | Modifiers::SHIFT);
                }
                Key::LeftAlt => {
                    self.state.insert(Modifiers::LEFT_ALT | Modifiers::ALT);
                }
                Key::RightAlt => {
                    self.state.insert(Modifiers::RIGHT_ALT | Modifiers::ALT);
                }
                Key::LeftMeta => {
                    self.state.insert(Modifiers::LEFT_META | Modifiers::META);
                }
                Key::RightMeta => {
                    self.state.insert(Modifiers::RIGHT_META | Modifiers::META);
                }
                Key::LeftCtrl => {
                    self.state.insert(Modifiers::LEFT_CTRL | Modifiers::CTRL);
                }
                Key::RightCtrl => {
                    self.state.insert(Modifiers::RIGHT_CTRL | Modifiers::CTRL);
                }
                _ => {}
            },
            // PRESSED event is a regular key release.
            // CANCEL event is an update to key presses after a focus loss.
            KeyEventType::Released | KeyEventType::Cancel => match key {
                Key::CapsLock => {
                    self.state.remove(Modifiers::CAPS_LOCK);
                }
                Key::NumLock => self.state.remove(Modifiers::NUM_LOCK),
                Key::ScrollLock => self.state.remove(Modifiers::SCROLL_LOCK),

                Key::LeftShift => {
                    self.state.remove(Modifiers::LEFT_SHIFT);
                    if !self.state.contains(Modifiers::RIGHT_SHIFT) {
                        self.state.remove(Modifiers::SHIFT);
                    }
                }
                Key::RightShift => {
                    self.state.remove(Modifiers::RIGHT_SHIFT);
                    if !self.test(Modifiers::LEFT_SHIFT) {
                        self.state.remove(Modifiers::SHIFT);
                    }
                }
                Key::LeftAlt => {
                    self.state.remove(Modifiers::LEFT_ALT);
                    if !self.state.contains(Modifiers::RIGHT_ALT) {
                        self.state.remove(Modifiers::ALT);
                    }
                }
                Key::RightAlt => {
                    self.state.remove(Modifiers::RIGHT_ALT);
                    if !self.test(Modifiers::LEFT_ALT) {
                        self.state.remove(Modifiers::ALT);
                    }
                }
                Key::LeftMeta => {
                    self.state.remove(Modifiers::LEFT_META);
                    if !self.state.contains(Modifiers::RIGHT_META) {
                        self.state.remove(Modifiers::META);
                    }
                }
                Key::RightMeta => {
                    self.state.remove(Modifiers::RIGHT_META);
                    if !self.test(Modifiers::LEFT_META) {
                        self.state.remove(Modifiers::META);
                    }
                }
                Key::LeftCtrl => {
                    self.state.remove(Modifiers::LEFT_CTRL);
                    if !self.state.contains(Modifiers::RIGHT_CTRL) {
                        self.state.remove(Modifiers::CTRL);
                    }
                }
                Key::RightCtrl => {
                    self.state.remove(Modifiers::RIGHT_CTRL);
                    if !self.test(Modifiers::LEFT_CTRL) {
                        self.state.remove(Modifiers::CTRL);
                    }
                }
                _ => {}
            },
        }
    }

    /// Update the modifier tracker with this event.
    pub fn update_with_key_meaning(&mut self, event: KeyEventType, key_meaning: KeyMeaning) {
        match event {
            KeyEventType::Pressed | KeyEventType::Sync => match key_meaning {
                KeyMeaning::NonPrintableKey(NonPrintableKey::AltGraph) => {
                    self.state.insert(Modifiers::ALT_GRAPH)
                }
                _ => {}
            },
            KeyEventType::Released | KeyEventType::Cancel => match key_meaning {
                KeyMeaning::NonPrintableKey(NonPrintableKey::AltGraph) => {
                    self.state.remove(Modifiers::ALT_GRAPH)
                }
                _ => {}
            },
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
            Key::CapsLock => LockState::CAPS_LOCK,
            Key::NumLock => LockState::NUM_LOCK,
            Key::ScrollLock => LockState::SCROLL_LOCK,
            // FUNCTION_LOCK
            // SYMBOL_LOCK
            // etc.
            _ => LockState::from_bits_allow_unknown(0),
        };
        if lock_key == LockState::from_bits_allow_unknown(0) {
            return;
        }
        let lock_state = self.state.get(&lock_key);
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
                error!(?event, ?key, state = ?self.state, "unexpected state transition");
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
/// This is repetitive code, so perhaps better handle it here.  You can feed
/// the keyboard events into its [KeyState::update] method.
///
/// [KeyState] keeps track of the ordering the keys were pressed, and you can
/// call [KeyState::get_ordered_keys] to get a sequence of currently pressed
/// keys, in the order they were pressed.  This is useful for emitting keyboard
/// events that must happen in a very particular ordering.
#[derive(Debug, Clone, Default)]
pub struct KeyState {
    // Using BTreeSet for deterministic key iteration ordering.
    /// A collection that predictably iterates over all the keys in the order
    /// they were pressed.
    // Must iterate through key-value pairs in the order of increasing keys.
    ordinal_to_key: collections::BTreeMap<u64, Key>,
    /// Backwards map for quickly finding the ordinal of a specific key.
    // A bidirectional map could work here, but we don't use one yet on Fuchsia,
    // avoiding a new dependency with two maps.
    key_to_ordinal: collections::HashMap<Key, u64>,
    /// The next new pressed key will get this ordinal.
    // The ordinal increments with each unique key, but is reset back to zero
    // when no keys are pressed.  This should be enough to not overflow in any
    // realistic scenarios.
    next_ordinal: u64,
}

impl KeyState {
    /// Creates a new [KeyState]
    pub fn new() -> Self {
        KeyState {
            next_ordinal: 0,
            key_to_ordinal: collections::HashMap::new(),
            ordinal_to_key: collections::BTreeMap::new(),
        }
    }

    /// Updates the key tracking state with the given key event pair.
    pub fn update(&mut self, event: KeyEventType, key: Key) {
        match event {
            KeyEventType::Pressed | KeyEventType::Sync => {
                if let None = self.key_to_ordinal.insert(key, self.next_ordinal) {
                    // Only if the inserted key was not in the set of pressed
                    // keys.
                    self.ordinal_to_key.insert(self.next_ordinal, key);
                    self.next_ordinal = self.next_ordinal + 1;
                }
            }
            KeyEventType::Released | KeyEventType::Cancel => {
                if let Some(ordinal) = self.key_to_ordinal.remove(&key) {
                    self.ordinal_to_key.remove_entry(&ordinal);
                }
                //  If no keys remain in the pressed set, reset the ordinal.
                if self.key_to_ordinal.is_empty() {
                    assert!(self.ordinal_to_key.is_empty());
                    self.next_ordinal = 0;
                }
            }
        }
    }

    /// Returns true if `key` is noted as pressed.
    pub fn is_pressed(&self, key: &Key) -> bool {
        self.key_to_ordinal.contains_key(key)
    }

    /// Returns `true` if at least one key from `keys` is pressed.
    pub fn pressed_any(&self, keys: &[Key]) -> bool {
        keys.iter().any(|k| self.is_pressed(k))
    }

    /// Returns `true` if all keys from `keys` are pressed.
    pub fn pressed_all(&self, keys: &[Key]) -> bool {
        keys.iter().all(|k| self.is_pressed(k))
    }

    /// Gets all the keys from the set.
    pub fn get_set(&self) -> collections::BTreeSet<Key> {
        let mut ret = collections::BTreeSet::new();
        let _k = self.key_to_ordinal.keys().for_each(|k| {
            ret.insert(*k);
        });
        ret
    }

    /// Gets the list of all currently pressed keys, in the order they were
    /// pressed.
    pub fn get_ordered_keys(&self) -> Vec<Key> {
        // Iteration MUST produce keys in a strictly increasing sequence.
        self.ordinal_to_key.iter().map(|(_, key)| *key).collect()
    }

    /// Clears the state of [Self], and expunges any stored key presses.
    pub fn clear(&mut self) {
        self.next_ordinal = 0;
        self.ordinal_to_key.clear();
        self.key_to_ordinal.clear();
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use test_case::test_case;

    const HID_USAGE_KEY_A: u32 = 0x04;
    const HID_USAGE_KEY_B: u32 = 0x05;
    const HID_USAGE_KEY_C: u32 = 0x06;
    const HID_USAGE_KEY_K: u32 = 0x0e;
    const HID_USAGE_KEY_L: u32 = 0x0f;
    const HID_USAGE_KEY_M: u32 = 0x10;
    const HID_USAGE_KEY_N: u32 = 0x11;
    const HID_USAGE_KEY_U: u32 = 0x18;
    const HID_USAGE_KEY_1: u32 = 0x1e;
    const HID_USAGE_KEY_SEMICOLON: u32 = 0x33;

    // The effects of Shift and CapsLock on keys are different for non-letters.
    #[test]
    fn caps_lock_effect() -> Result<()> {
        assert_eq!(
            '1' as u32,
            US_QWERTY.hid_usage_to_code_point(
                HID_USAGE_KEY_1,
                &ModifierState::new(),
                &LockStateKeys::new().with(LockState::CAPS_LOCK),
            )?
        );
        assert_eq!(
            '!' as u32,
            US_QWERTY.hid_usage_to_code_point(
                HID_USAGE_KEY_1,
                &ModifierState::new().with(Modifiers::LEFT_SHIFT),
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
                &ModifierState::new().with(Modifiers::CAPS_LOCK),
                &LockStateKeys::new(),
            )?
        );
        assert_eq!(
            'A' as u32,
            US_QWERTY.hid_usage_to_code_point(
                HID_USAGE_KEY_A,
                &ModifierState::new(),
                &LockStateKeys::new().with(LockState::CAPS_LOCK),
            )?
        );
        assert_eq!(
            'A' as u32,
            US_QWERTY.hid_usage_to_code_point(
                HID_USAGE_KEY_A,
                &ModifierState::new().with(Modifiers::RIGHT_SHIFT),
                &LockStateKeys::new(),
            )?
        );
        assert_eq!(
            'A' as u32,
            US_QWERTY.hid_usage_to_code_point(
                HID_USAGE_KEY_A,
                &ModifierState::new().with(Modifiers::LEFT_SHIFT),
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
                &ModifierState::new().with(Modifiers::CAPS_LOCK),
                &LockStateKeys::new(),
            )?
        );
        assert_eq!(
            'A' as u32,
            US_DVORAK.hid_usage_to_code_point(
                HID_USAGE_KEY_A,
                &ModifierState::new(),
                &LockStateKeys::new().with(LockState::CAPS_LOCK),
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
                &LockStateKeys::new().with(LockState::CAPS_LOCK),
            )?
        );
        assert_eq!(
            'X' as u32,
            US_DVORAK.hid_usage_to_code_point(
                HID_USAGE_KEY_B,
                &ModifierState::new().with(Modifiers::LEFT_SHIFT),
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
                &ModifierState::new().with(Modifiers::LEFT_SHIFT),
                &LockStateKeys::new(),
            )?
        );
        assert_eq!(
            'N' as u32,
            US_DVORAK.hid_usage_to_code_point(
                HID_USAGE_KEY_L,
                &ModifierState::new(),
                &LockStateKeys::new().with(LockState::CAPS_LOCK),
            )?
        );
        Ok(())
    }

    #[test]
    fn spotcheck_us_colemak_keymap() -> Result<()> {
        assert_eq!(
            'c' as u32,
            US_COLEMAK.hid_usage_to_code_point(
                HID_USAGE_KEY_C,
                &ModifierState::new().with(Modifiers::CAPS_LOCK),
                &LockStateKeys::new(),
            )?
        );
        assert_eq!(
            'O' as u32,
            US_COLEMAK.hid_usage_to_code_point(
                HID_USAGE_KEY_SEMICOLON,
                &ModifierState::new(),
                &LockStateKeys::new().with(LockState::CAPS_LOCK),
            )?
        );
        assert_eq!(
            'L' as u32,
            US_COLEMAK.hid_usage_to_code_point(
                HID_USAGE_KEY_U,
                &ModifierState::new(),
                &LockStateKeys::new().with(LockState::CAPS_LOCK),
            )?
        );
        assert_eq!(
            'e' as u32,
            US_COLEMAK.hid_usage_to_code_point(
                HID_USAGE_KEY_K,
                &ModifierState::new(),
                &LockStateKeys::new(),
            )?
        );
        assert_eq!(
            'M' as u32,
            US_COLEMAK.hid_usage_to_code_point(
                HID_USAGE_KEY_M,
                &ModifierState::new(),
                &LockStateKeys::new().with(LockState::CAPS_LOCK),
            )?
        );
        assert_eq!(
            'A' as u32,
            US_COLEMAK.hid_usage_to_code_point(
                HID_USAGE_KEY_A,
                &ModifierState::new().with(Modifiers::LEFT_SHIFT),
                &LockStateKeys::new(),
            )?
        );
        assert_eq!(
            'k' as u32,
            US_COLEMAK.hid_usage_to_code_point(
                HID_USAGE_KEY_N,
                &ModifierState::new(),
                &LockStateKeys::new(),
            )?
        );
        Ok(())
    }

    // CapsLock ______/""""""""\_______/"""""""""\_______
    // Modifier ______/""""""""\_______/"""""""""\______
    #[test_case(Key::CapsLock, Modifiers::CAPS_LOCK; "CapsLock")]
    #[test_case(Key::NumLock, Modifiers::NUM_LOCK; "NumLock")]
    #[test_case(Key::ScrollLock, Modifiers::SCROLL_LOCK; "ScrollLock")]
    // Key::Function
    // Key::Symbol
    // Key::AltGraph
    // Test "sided" modifiers.
    #[test_case(Key::RightShift, Modifiers::RIGHT_SHIFT|Modifiers::SHIFT; "RightShift")]
    #[test_case(Key::LeftShift, Modifiers::LEFT_SHIFT|Modifiers::SHIFT; "LeftShift")]
    #[test_case(Key::RightAlt, Modifiers::RIGHT_ALT|Modifiers::ALT; "RightAlt")]
    #[test_case(Key::LeftAlt, Modifiers::LEFT_ALT|Modifiers::ALT; "LeftAlt")]
    #[test_case(Key::RightMeta, Modifiers::RIGHT_META|Modifiers::META; "RightMeta")]
    #[test_case(Key::LeftMeta, Modifiers::LEFT_META|Modifiers::META; "LeftMeta")]
    #[test_case(Key::RightCtrl, Modifiers::RIGHT_CTRL|Modifiers::CTRL; "RightCtrl")]
    #[test_case(Key::LeftCtrl, Modifiers::LEFT_CTRL|Modifiers::CTRL; "LeftCtrl")]
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
    #[test_case(Key::LeftShift, Key::RightShift, Modifiers::SHIFT; "Shift/LR")]
    #[test_case(Key::RightShift, Key::LeftShift, Modifiers::SHIFT; "Shift/RL")]
    #[test_case(Key::LeftAlt, Key::RightAlt, Modifiers::ALT; "Alt/LR")]
    #[test_case(Key::RightAlt, Key::LeftAlt, Modifiers::ALT; "Alt/RL")]
    #[test_case(Key::LeftMeta, Key::RightMeta, Modifiers::META; "Meta/LR")]
    #[test_case(Key::RightMeta, Key::LeftMeta, Modifiers::META; "Meta/RL")]
    #[test_case(Key::RightCtrl, Key::LeftCtrl, Modifiers::CTRL; "Ctrl/RL")]
    #[test_case(Key::LeftCtrl, Key::RightCtrl, Modifiers::CTRL; "Ctrl/LR")]
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
    #[test_case(Key::CapsLock, LockState::CAPS_LOCK; "CapsLock")]
    #[test_case(Key::NumLock, LockState::NUM_LOCK; "NumLock")]
    #[test_case(Key::ScrollLock, LockState::SCROLL_LOCK; "ScrollLock")]
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
        assert!(!modifier_state.test(Modifiers::SHIFT));

        modifier_state.update(KeyEventType::Pressed, Key::LeftShift);
        assert!(modifier_state.test(Modifiers::SHIFT));
        modifier_state.update(KeyEventType::Released, Key::LeftShift);
        assert!(!modifier_state.test(Modifiers::SHIFT));

        modifier_state.update(KeyEventType::Pressed, Key::RightShift);
        assert!(modifier_state.test(Modifiers::SHIFT));
        modifier_state.update(KeyEventType::Released, Key::RightShift);
        assert!(!modifier_state.test(Modifiers::SHIFT));

        modifier_state.update(KeyEventType::Pressed, Key::CapsLock);
        assert!(!modifier_state.test(Modifiers::SHIFT));
        assert!(modifier_state.test(Modifiers::CAPS_LOCK));
        modifier_state.update(KeyEventType::Released, Key::CapsLock);
        assert!(!modifier_state.test(Modifiers::SHIFT));
        assert!(!modifier_state.test(Modifiers::CAPS_LOCK));
        modifier_state.update(KeyEventType::Pressed, Key::CapsLock);
        assert!(!modifier_state.test(Modifiers::SHIFT));
        assert!(modifier_state.test(Modifiers::CAPS_LOCK));
        modifier_state.update(KeyEventType::Released, Key::CapsLock);
        assert!(!modifier_state.test(Modifiers::SHIFT));
        assert!(!modifier_state.test(Modifiers::CAPS_LOCK));
    }

    #[test]
    fn test_key_meaning_modifier_tracker() {
        let mut modifier_state: ModifierState = Default::default();
        assert!(!modifier_state.test(Modifiers::ALT_GRAPH));

        modifier_state.update_with_key_meaning(
            KeyEventType::Pressed,
            KeyMeaning::NonPrintableKey(NonPrintableKey::AltGraph),
        );
        assert!(modifier_state.test(Modifiers::ALT_GRAPH));
        modifier_state.update_with_key_meaning(
            KeyEventType::Released,
            KeyMeaning::NonPrintableKey(NonPrintableKey::AltGraph),
        );
        assert!(!modifier_state.test(Modifiers::ALT_GRAPH));
    }

    // CapsLock            ________/""""""""""\_______/"""""\_____
    // LeftShift           ____________/"""""""""""\______________
    // is_shift_active     ____________/"""""""""""\______________
    // is_caps_lock_active ________/""""""""""\_______/"""""\_____
    #[test]
    fn test_interleaved_caps_lock_and_shift() {
        let mut modifier_state: ModifierState = Default::default();
        assert!(!modifier_state.test(Modifiers::SHIFT));

        modifier_state.update(KeyEventType::Pressed, Key::CapsLock);
        assert!(!modifier_state.test(Modifiers::SHIFT));
        assert!(modifier_state.test(Modifiers::CAPS_LOCK));

        modifier_state.update(KeyEventType::Pressed, Key::LeftShift);
        assert!(modifier_state.test(Modifiers::SHIFT));
        assert!(modifier_state.test(Modifiers::CAPS_LOCK));

        modifier_state.update(KeyEventType::Released, Key::CapsLock);
        assert!(modifier_state.test(Modifiers::SHIFT));
        assert!(!modifier_state.test(Modifiers::CAPS_LOCK));

        modifier_state.update(KeyEventType::Released, Key::LeftShift);
        // Caps Lock is still active...
        assert!(!modifier_state.test(Modifiers::SHIFT));
        assert!(!modifier_state.test(Modifiers::CAPS_LOCK));

        // Press and release Caps Lock again.
        modifier_state.update(KeyEventType::Pressed, Key::CapsLock);
        assert!(!modifier_state.test(Modifiers::SHIFT));
        assert!(modifier_state.test(Modifiers::CAPS_LOCK));

        modifier_state.update(KeyEventType::Released, Key::CapsLock);
        assert!(!modifier_state.test(Modifiers::SHIFT));
        assert!(!modifier_state.test(Modifiers::CAPS_LOCK));
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

        let keys = t.get_set();
        assert_eq!(true, t.pressed_all(&vec![Key::Space, Key::Tab,]));

        let mut expected = collections::BTreeSet::new();
        expected.insert(Key::Space);
        expected.insert(Key::Tab);
        assert_eq!(keys, expected, "want: {:?} was: {:?}", &expected, &keys);
    }

    #[test_case(
        &US_QWERTY,
        Key::A,
        ModifierState::new(),
        LockStateKeys::new(),
        Some(KeyMeaning::Codepoint(97));
        "test basic mapping")
    ]
    #[test_case(
        &US_QWERTY,
        Key::A,
        ModifierState::new().with(Modifiers::LEFT_SHIFT),
        LockStateKeys::new(),
        Some(KeyMeaning::Codepoint(65));
        "test basic mapping - capital letter")
    ]
    #[test_case(
        // This test case is needed for Chromium integration.
        // See fxbug.dev/109987.
        &FR_AZERTY,
        Key::RightAlt,
        ModifierState::new(),
        LockStateKeys::new(),
        Some(KeyMeaning::NonPrintableKey(NonPrintableKey::AltGraph));
        "test FR AZERTY right Alt mapping to AltGr")
    ]
    #[test_case(
        &US_QWERTY,
        Key::RightAlt,
        ModifierState::new(),
        LockStateKeys::new(),
        Some(KeyMeaning::NonPrintableKey(NonPrintableKey::Alt));
        "test US QWERTY right Alt mapping")
    ]
    fn test_keymap_apply(
        keymap: &Keymap<'_>,
        key: Key,
        modifier_state: ModifierState,
        lock_state: LockStateKeys,
        expected: Option<KeyMeaning>,
    ) {
        let actual = keymap.apply(key, &modifier_state, &lock_state);
        assert_eq!(expected, actual, "expected: {:?}, actual: {:?}", expected, actual);
    }

    // Test that the keys are ordered in the sequence they were pressed.
    //
    // A ____/"""""""""""""""
    // B _______/"""""""\____
    // C __/"""""""""""""""""
    // D __________/"""""""""
    //
    // KeyState::get_ordered_keys() => [C, A, D]
    //
    // since out of the keys that are still pressed, C, A and D were actuated
    // in that order.
    #[test]
    fn key_ordering() {
        let mut t = KeyState::new();

        t.update(KeyEventType::Pressed, Key::C);
        t.update(KeyEventType::Pressed, Key::A);
        t.update(KeyEventType::Pressed, Key::B);
        t.update(KeyEventType::Pressed, Key::D);
        // Repeated
        t.update(KeyEventType::Pressed, Key::A);

        t.update(KeyEventType::Released, Key::B);

        assert_eq!(vec![Key::C, Key::A, Key::D], t.get_ordered_keys());

        t.clear();
        let expected: Vec<Key> = vec![];
        assert_eq!(expected, t.get_ordered_keys());
    }

    #[test]
    fn key_ordering_with_reset() {
        let mut t = KeyState::new();

        t.update(KeyEventType::Pressed, Key::A);
        t.update(KeyEventType::Pressed, Key::B);
        t.update(KeyEventType::Released, Key::B);
        t.update(KeyEventType::Released, Key::A);

        let expected: Vec<Key> = vec![];
        assert_eq!(expected, t.get_ordered_keys());

        t.update(KeyEventType::Pressed, Key::C);

        assert_eq!(vec![Key::C], t.get_ordered_keys());

        t.update(KeyEventType::Pressed, Key::A);

        assert_eq!(vec![Key::C, Key::A], t.get_ordered_keys());
    }

    #[test]
    fn key_ordering_misuse() {
        let mut t = KeyState::new();

        t.update(KeyEventType::Released, Key::B);
        t.update(KeyEventType::Pressed, Key::A);
        t.update(KeyEventType::Released, Key::A);

        let expected: Vec<Key> = vec![];
        assert_eq!(expected, t.get_ordered_keys());

        t.update(KeyEventType::Pressed, Key::C);

        assert_eq!(vec![Key::C], t.get_ordered_keys());

        t.update(KeyEventType::Pressed, Key::A);

        assert_eq!(vec![Key::C, Key::A], t.get_ordered_keys());

        t.update(KeyEventType::Pressed, Key::A);
        t.update(KeyEventType::Pressed, Key::B);
        t.update(KeyEventType::Pressed, Key::C);

        assert_eq!(vec![Key::C, Key::A, Key::B], t.get_ordered_keys());
    }
}
