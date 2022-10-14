// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! # Old-style keymaps definitions.
//!
//! The keymaps defined in this module are statically defined and compiled in.
//! This is useful during the transition towards a more configurable keymap
//! support.

use crate::{LockStateChecker, ModifierChecker};
use fidl_fuchsia_ui_input3::{LockState, Modifiers};
use lazy_static::lazy_static;
use std::convert::Into;

lazy_static! {
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
    pub(crate) static ref QWERTY_MAP: Vec<Option<KeyLevels>> = vec![
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

    /// Standard [dvorak] keymap.
    ///
    /// [dvorak]: https://en.wikipedia.org/wiki/Dvorak_keyboard_layout
    pub(crate) static ref DVORAK_MAP: Vec<Option<KeyLevels>> = vec![
        // 0x00
        None,
        None,
        None,
        None,
        // HID_USAGE_KEY_A
        Some(('a', Some('A'), true).into()),
        Some(('x', Some('X'), true).into()),
        Some(('j', Some('J'), true).into()),
        Some(('e', Some('E'), true).into()),
        // 0x08
        Some(('.', Some('>')).into()),
        Some(('u', Some('U'), true).into()),
        Some(('i', Some('I'), true).into()),
        Some(('d', Some('D'), true).into()),
        // 0x0c
        Some(('c', Some('C'), true).into()),
        Some(('h', Some('H'), true).into()),
        Some(('t', Some('T'), true).into()),
        Some(('n', Some('N'), true).into()),
        // 0x10
        Some(('m', Some('M'), true).into()),
        Some(('b', Some('B'), true).into()),
        Some(('r', Some('R'), true).into()),
        Some(('l', Some('L'), true).into()),
        // 0x14
        Some(('\'', Some('"')).into()),
        Some(('p', Some('P'), true).into()),
        Some(('o', Some('O'), true).into()),
        Some(('y', Some('Y'), true).into()),
        // 0x18
        Some(('g', Some('G'), true).into()),
        Some(('k', Some('K'), true).into()),
        Some((',', Some('<')).into()),
        Some(('q', Some('Q'), true).into()),
        // 0x1c
        Some(('f', Some('F'), true).into()),
        Some((';', Some(':')).into()),
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
        Some(('[', Some('{')).into()),
        Some((']', Some('}')).into()),
        Some(('/', Some('?')).into()),
        // 0x30
        Some(('=', Some('+')).into()),
        Some(('\\', Some('|')).into()),
        None,
        Some(('s', Some('S'), true).into()),
        // 0x34
        Some(('-', Some('_')).into()),
        Some(('`', Some('~')).into()),
        Some(('w', Some('W'), true).into()),
        Some(('v', Some('V'), true).into()),
        // 0x38
        Some(('z', Some('Z'), true).into()),
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
    pub(crate) static ref FR_AZERTY_MAP: Vec<Option<KeyLevels>> = vec![
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

    /// Standard [colemak] keymap.
    ///
    /// [colemak]: https://en.wikipedia.org/wiki/Keyboard_layout#Colemak
    pub(crate) static ref COLEMAK_MAP: Vec<Option<KeyLevels>> = vec![
        // 0x00
        None,
        None,
        None,
        None,
        // HID_USAGE_KEY_A
        Some(('a', Some('A'), true).into()),
        Some(('b', Some('B'), true).into()),
        Some(('c', Some('C'), true).into()),
        Some(('s', Some('S'), true).into()),
        // 0x08
        Some(('f', Some('F'), true).into()),
        Some(('t', Some('T'), true).into()),
        Some(('d', Some('D'), true).into()),
        Some(('h', Some('H'), true).into()),
        // 0x0c
        Some(('u', Some('U'), true).into()),
        Some(('n', Some('N'), true).into()),
        Some(('e', Some('E'), true).into()),
        Some(('i', Some('I'), true).into()),
        // 0x10
        Some(('m', Some('M'), true).into()),
        Some(('k', Some('K'), true).into()),
        Some(('y', Some('Y'), true).into()),
        Some((';', Some(':')).into()),
        // 0x14
        Some(('q', Some('Q'), true).into()),
        Some(('p', Some('P'), true).into()),
        Some(('r', Some('R'), true).into()),
        Some(('g', Some('G'), true).into()),
        // 0x18
        Some(('l', Some('L'), true).into()),
        Some(('v', Some('V'), true).into()),
        Some(('w', Some('W'), true).into()),
        Some(('x', Some('X'), true).into()),
        // 0x1c
        Some(('j', Some('J'), true).into()),
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
        Some(('o', Some('O'), true).into()),
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
}

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
    pub fn get_key(&self, m: &impl ModifierChecker, l: &impl LockStateChecker) -> Option<char> {
        if l.test(LockState::CAPS_LOCK) && self.is_letter || m.test(Modifiers::SHIFT) {
            return self.shift_ch;
        }
        Some(self.ch)
    }
}
