// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::collections::HashMap;

use fidl_fuchsia_ui_input::KeyboardReport;

use crate::keymaps;
use crate::usages::Usages;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
enum Shift {
    No,
    Yes,
    DontCare,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
struct KeyStroke {
    usage: u32,
    shift: Shift,
}

/// Lightweight utility for basic keymap conversion of chars to keystrokes.
///
/// This is intended for end-to-end and input testing only; for production use cases and general
/// testing, IME injection should be used instead. Generally a mapping exists only for printable
/// ASCII characters; in particular neither `'\t'` nor `'\n'` is mapped in either of the standard
/// zircon keymaps. Furthermore, IME implementations may themselves override the keymap in a way
/// that invalidates this translation.
///
/// This is an inverse of [hid/hid.h:hid_map_key].
///
/// [hid/hid.h:hid_map_key]: https://fuchsia.googlesource.com/fuchsia/+/ef9c451ba83a3ece22cad66b9dcfb446be291966/zircon/system/ulib/hid/include/hid/hid.h#30
#[derive(Debug)]
pub struct InverseKeymap {
    map: HashMap<char, KeyStroke>,
}

impl InverseKeymap {
    /// Creates an inverse keymap from a specialized `keymap` array. The value of this array at
    /// index `u`, where `u` is the usage, can be:
    ///
    ///  * `None` if the key maps to no `char` (Esc key)
    ///  * `Some((c, None))` if the key maps to `c`, but does not map to any `char` when shift is pressed
    ///  * `Some((c, Some(cs)))` if the key maps to `c` when shift is not pressed and to `cs` when it is
    ///    pressed
    ///
    /// # Examples
    ///
    /// ```
    /// # use crate::inverse_keymap::InverseKeymap;
    /// # use crate::keymaps::US_QWERTY;
    ///
    /// let _keymap = InverseKeymap::new(&US_QWERTY);
    /// ```
    pub fn new(keymap: &keymaps::Keymap<'_>) -> Self {
        let mut map = HashMap::new();

        // A real keymap is not invertible, so multiple keys can produce the same effect.  For
        // example, a key `1` and a numeric keypad `1` will end up inserting the same element in
        // the inverse keymap.  By iterating over the forward keymap in reverse, we give priority
        // to more "conventional" keys, i.e. a key `1` will always be used instead of numeric
        // keypad `1`.  A similar idea is applied in all match arms below.
        for (usage, key_levels) in keymap.as_ref().iter().enumerate().rev() {
            let entry = key_levels.as_ref().map(|kl| (kl.ch, kl.shift_ch));
            match entry {
                Some((ch, Some(shift_ch))) if ch == shift_ch => {
                    map.insert(ch, KeyStroke { usage: usage as u32, shift: Shift::DontCare });
                }
                Some((ch, Some(shift_ch))) => {
                    map.insert(ch, KeyStroke { usage: usage as u32, shift: Shift::No });
                    map.insert(shift_ch, KeyStroke { usage: usage as u32, shift: Shift::Yes });
                }
                Some((ch, None)) => {
                    map.insert(ch, KeyStroke { usage: usage as u32, shift: Shift::No });
                }
                _ => (),
            }
        }

        Self { map }
    }

    /// Converts the `input` string into a key sequence under the provided `InverseKeymap`.
    ///
    /// This is intended for end-to-end and input testing only; for production use cases and general
    /// testing, IME injection should be used instead.
    ///
    /// A translation from `input` to a sequence of keystrokes is not guaranteed to exist. If a
    /// translation does not exist, `None` is returned.
    ///
    /// The sequence does not contain pauses except between repeated keys or to clear a shift state,
    /// though the sequence does terminate with an empty report (no keys pressed). A shift key
    /// transition is sent in advance of each series of keys that needs it.
    ///
    /// Note that there is currently no way to distinguish between particular key releases. As such,
    /// only one key release report is generated even in combinations, e.g. Shift + A.
    ///
    /// # Examples
    ///
    /// ```
    /// # use crate::{keymaps::QWERTY_MAP, inverse_keymap::InverseKeymap};
    /// let keymap = InverseKeymap::new(&QWERTY_MAP);
    /// let key_sequence = keymap.derive_key_sequence("A").unwrap();
    ///
    /// // [shift, A, clear]
    /// assert_eq!(key_sequence.len(), 3);
    /// ```
    pub fn derive_key_sequence(&self, input: &str) -> Option<Vec<KeyboardReport>> {
        let mut reports = vec![];
        let mut shift_pressed = false;
        let mut last_usage = None;

        for ch in input.chars() {
            let key_stroke = self.map.get(&ch)?;

            match key_stroke.shift {
                Shift::Yes if !shift_pressed => {
                    shift_pressed = true;
                    last_usage = Some(0);
                }
                Shift::No if shift_pressed => {
                    shift_pressed = false;
                    last_usage = Some(0);
                }
                _ => {
                    if last_usage == Some(key_stroke.usage) {
                        last_usage = Some(0);
                    }
                }
            }

            if let Some(0) = last_usage {
                reports.push(KeyboardReport {
                    pressed_keys: if shift_pressed {
                        vec![Usages::HidUsageKeyLeftShift as u32]
                    } else {
                        vec![]
                    },
                });
            }

            last_usage = Some(key_stroke.usage);

            reports.push(KeyboardReport {
                pressed_keys: if shift_pressed {
                    vec![key_stroke.usage, Usages::HidUsageKeyLeftShift as u32]
                } else {
                    vec![key_stroke.usage]
                },
            });
        }

        // TODO: In the future, we might want to distinguish between different key releases, instead
        //       of sending one single release report even in the case of key combinations.
        reports.push(KeyboardReport { pressed_keys: vec![] });

        Some(reports)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    macro_rules! reports {
        ( $( [ $( $usages:expr ),* ] ),* $( , )? ) => {
            Some(vec![
                $(
                    KeyboardReport {
                        pressed_keys: vec![$($usages as u32),*]
                    }
                ),*
            ])
        }
    }

    #[test]
    fn shift_map() {
        let keymap = InverseKeymap::new(&keymaps::US_QWERTY);

        assert_eq!(keymap.map[&'a'].shift, Shift::No);
        assert_eq!(keymap.map[&'A'].shift, Shift::Yes);
    }

    #[test]
    fn lowercase() {
        let keymap = InverseKeymap::new(&keymaps::US_QWERTY);

        assert_eq!(
            keymap.derive_key_sequence("lowercase"),
            reports![
                [Usages::HidUsageKeyL],
                [Usages::HidUsageKeyO],
                [Usages::HidUsageKeyW],
                [Usages::HidUsageKeyE],
                [Usages::HidUsageKeyR],
                [Usages::HidUsageKeyC],
                [Usages::HidUsageKeyA],
                [Usages::HidUsageKeyS],
                [Usages::HidUsageKeyE],
                [],
            ]
        );
    }

    #[test]
    fn numerics() {
        let keymap = InverseKeymap::new(&keymaps::US_QWERTY);

        assert_eq!(
            keymap.derive_key_sequence("0123456789"),
            reports![
                [Usages::HidUsageKey0],
                [Usages::HidUsageKey1],
                [Usages::HidUsageKey2],
                [Usages::HidUsageKey3],
                [Usages::HidUsageKey4],
                [Usages::HidUsageKey5],
                [Usages::HidUsageKey6],
                [Usages::HidUsageKey7],
                [Usages::HidUsageKey8],
                [Usages::HidUsageKey9],
                [],
            ]
        );
    }

    #[test]
    fn internet_text_entry() {
        let keymap = InverseKeymap::new(&keymaps::US_QWERTY);

        assert_eq!(
            keymap.derive_key_sequence("http://127.0.0.1:8080"),
            reports![
                [Usages::HidUsageKeyH],
                [Usages::HidUsageKeyT],
                [],
                [Usages::HidUsageKeyT],
                [Usages::HidUsageKeyP],
                // ':'
                // Shift is actuated first on its own, then together with
                // the key.
                [Usages::HidUsageKeyLeftShift],
                [Usages::HidUsageKeySemicolon, Usages::HidUsageKeyLeftShift],
                [],
                [Usages::HidUsageKeySlash],
                [],
                [Usages::HidUsageKeySlash],
                [Usages::HidUsageKey1],
                [Usages::HidUsageKey2],
                [Usages::HidUsageKey7],
                [Usages::HidUsageKeyDot],
                [Usages::HidUsageKey0],
                [Usages::HidUsageKeyDot],
                [Usages::HidUsageKey0],
                [Usages::HidUsageKeyDot],
                [Usages::HidUsageKey1],
                [Usages::HidUsageKeyLeftShift],
                [Usages::HidUsageKeySemicolon, Usages::HidUsageKeyLeftShift],
                [],
                [Usages::HidUsageKey8],
                [Usages::HidUsageKey0],
                [Usages::HidUsageKey8],
                [Usages::HidUsageKey0],
                [],
            ]
        );
    }

    #[test]
    fn sentence() {
        let keymap = InverseKeymap::new(&keymaps::US_QWERTY);

        assert_eq!(
            keymap.derive_key_sequence("Hello, world!"),
            reports![
                [Usages::HidUsageKeyLeftShift],
                [Usages::HidUsageKeyH, Usages::HidUsageKeyLeftShift],
                [],
                [Usages::HidUsageKeyE],
                [Usages::HidUsageKeyL],
                [],
                [Usages::HidUsageKeyL],
                [Usages::HidUsageKeyO],
                [Usages::HidUsageKeyComma],
                [Usages::HidUsageKeySpace],
                [Usages::HidUsageKeyW],
                [Usages::HidUsageKeyO],
                [Usages::HidUsageKeyR],
                [Usages::HidUsageKeyL],
                [Usages::HidUsageKeyD],
                [Usages::HidUsageKeyLeftShift],
                [Usages::HidUsageKey1, Usages::HidUsageKeyLeftShift],
                [],
            ]
        );
    }

    #[test]
    fn hold_shift() {
        let keymap = InverseKeymap::new(&keymaps::US_QWERTY);

        assert_eq!(
            keymap.derive_key_sequence("ALL'S WELL!"),
            reports![
                [Usages::HidUsageKeyLeftShift],
                [Usages::HidUsageKeyA, Usages::HidUsageKeyLeftShift],
                [Usages::HidUsageKeyL, Usages::HidUsageKeyLeftShift],
                [Usages::HidUsageKeyLeftShift],
                [Usages::HidUsageKeyL, Usages::HidUsageKeyLeftShift],
                [],
                [Usages::HidUsageKeyApostrophe],
                [Usages::HidUsageKeyLeftShift],
                [Usages::HidUsageKeyS, Usages::HidUsageKeyLeftShift],
                [Usages::HidUsageKeySpace, Usages::HidUsageKeyLeftShift],
                [Usages::HidUsageKeyW, Usages::HidUsageKeyLeftShift],
                [Usages::HidUsageKeyE, Usages::HidUsageKeyLeftShift],
                [Usages::HidUsageKeyL, Usages::HidUsageKeyLeftShift],
                [Usages::HidUsageKeyLeftShift],
                [Usages::HidUsageKeyL, Usages::HidUsageKeyLeftShift],
                [Usages::HidUsageKey1, Usages::HidUsageKeyLeftShift],
                [],
            ]
        );
    }
}
