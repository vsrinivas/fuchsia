// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::collections::HashMap;

use fidl_fuchsia_ui_input::KeyboardReport;

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
    /// let qwerty_map = &[
    ///     // ...
    ///     Some(('a', Some('A'))),
    ///     Some(('b', Some('B'))),
    ///     Some(('c', Some('C'))),
    ///     // ...
    /// ];
    ///
    /// let _keymap = InverseKeymap::new(&qwerty_map);
    /// ```
    pub fn new(keymap: &[Option<(char, Option<char>)>]) -> Self {
        let mut map = HashMap::new();

        for (usage, entry) in keymap.iter().enumerate() {
            match entry {
                Some((ch, Some(shift_ch))) if ch == shift_ch => {
                    map.insert(*ch, KeyStroke { usage: usage as u32, shift: Shift::DontCare });
                }
                Some((ch, Some(shift_ch))) => {
                    map.insert(*ch, KeyStroke { usage: usage as u32, shift: Shift::No });
                    map.insert(*shift_ch, KeyStroke { usage: usage as u32, shift: Shift::Yes });
                }
                Some((ch, None)) => {
                    map.insert(*ch, KeyStroke { usage: usage as u32, shift: Shift::No });
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
    use crate::keymaps::QWERTY_MAP;

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
        let keymap = InverseKeymap::new(QWERTY_MAP);

        assert_eq!(keymap.map[&'a'].shift, Shift::No);
        assert_eq!(keymap.map[&'A'].shift, Shift::Yes);
    }

    #[test]
    fn lowercase() {
        let keymap = InverseKeymap::new(QWERTY_MAP);

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
    fn sentence() {
        let keymap = InverseKeymap::new(QWERTY_MAP);

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
        let keymap = InverseKeymap::new(QWERTY_MAP);

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
