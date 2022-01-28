// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::collections::HashMap;

use crate as keymaps;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Shift {
    No,
    Yes,
    DontCare,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct KeyStroke {
    pub usage: u32,
    pub shift: Shift,
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
    /// # use crate::US_QWERTY;
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

        // Additionally, insert some convenience keystrokes.  This is not intended to be
        // complete, but to allow simple keyboard navigation such as confirming text input
        // or focus change to the next element.

        // Based on //sdk/fidl/fuchsia.input/key.fidl.
        // Assumes the usage page is 0x7.
        map.insert('\n', KeyStroke { usage: 0x28, shift: Shift::No });
        map.insert('\t', KeyStroke { usage: 0x2b, shift: Shift::No });

        Self { map }
    }

    pub fn get(&self, c: &char) -> Option<&KeyStroke> {
        self.map.get(c)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use assert_matches::assert_matches;

    #[test]
    fn returns_correct_shift_level() {
        let keymap = InverseKeymap::new(&keymaps::US_QWERTY);
        assert_matches!(keymap.get(&'a'), Some(KeyStroke { shift: Shift::No, .. }));
        assert_matches!(keymap.get(&'A'), Some(KeyStroke { shift: Shift::Yes, .. }));
    }

    #[test]
    fn returns_expected_usage() {
        let keymap = InverseKeymap::new(&keymaps::US_QWERTY);
        assert_matches!(keymap.get(&'a'), Some(KeyStroke { usage: 0x04, .. }));
        // Numeric character: maps to main keyboard, not keypad.
        assert_matches!(keymap.get(&'1'), Some(KeyStroke { usage: 0x1e, .. }));
    }

    #[test]
    fn returns_special_keys() {
        let keymap = InverseKeymap::new(&keymaps::US_QWERTY);
        assert_matches!(keymap.get(&'\n'), Some(KeyStroke { usage: 0x28, shift: Shift::No }));
        assert_matches!(keymap.get(&'\t'), Some(KeyStroke { usage: 0x2b, shift: Shift::No }));
    }
}
