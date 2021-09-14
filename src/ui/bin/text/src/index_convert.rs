// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! There are several ways to index into a String: UTF-8 `byte` indices (Rust uses these) or Unicode
//! code points, which are encoding-independent. The `TextField` protocol uses the latter, and so
//! this module has some helper functions to convert indices between these two representations. The
//! legacy `TextInputState` uses UTF-16 code units as indices, so there are also functions that
//! convert one of these `TextInputState`s into one that uses UTF-8 byte indices instead, or
//! vice-versa.

use fidl_fuchsia_ui_input as uii;

/// Converts a char index in a text to a byte index
pub fn char_to_byte(text: &str, chars: i64) -> Option<usize> {
    if text.chars().count() == chars as usize {
        return Some(text.len());
    }
    text.char_indices().map(|(i, _)| i).nth(chars as usize)
}

/// Converts a byte index in a text to a char index
pub fn byte_to_char(text: &str, bytes: usize) -> Option<i64> {
    if text.len() == bytes {
        return Some(text.chars().count() as i64);
    }
    text.char_indices().map(|(i, _)| i).position(|v| v == bytes).map(|v| v as i64)
}

/// Converts a UTF-16 code unit index in a text to a byte index
fn codeunit_to_byte(text: &str, codeunits: i64) -> Option<usize> {
    if codeunits == 0 {
        return Some(0);
    }
    let mut aggregate_codeunit_length: i64 = 0;
    let mut aggregate_byte_length: usize = 0;
    for character in text.chars() {
        aggregate_codeunit_length += character.len_utf16() as i64;
        aggregate_byte_length += character.len_utf8();
        if aggregate_codeunit_length == codeunits {
            return Some(aggregate_byte_length);
        }
    }
    None
}

/// Converts a byte index in a text to a UTF-16 code unit index
fn byte_to_codeunit(text: &str, bytes: usize) -> Option<i64> {
    text.get(0..bytes).map(|substr| Iterator::sum(substr.chars().map(|c| c.len_utf16() as i64)))
}

/// The default uii::TextInputState uses UTF-16 code unit indices for its selections,
/// compositions, etc. Since keeping track of UTF-16 indices is precarious while using
/// UTF-8 strings as storage, this function converts these codeunit indices into bytes.
pub fn text_state_codeunit_to_byte(mut state: uii::TextInputState) -> uii::TextInputState {
    fn convert(txt: &str, idx: &mut i64) {
        *idx = match codeunit_to_byte(txt, *idx) {
            Some(v) => v as i64,
            None => -1,
        };
    }
    convert(&state.text, &mut state.selection.base);
    convert(&state.text, &mut state.selection.extent);
    convert(&state.text, &mut state.composing.start);
    convert(&state.text, &mut state.composing.end);
    state
}

/// The default uii::TextInputState uses UTF-16 code unit indices for its selections,
/// compositions, etc. Since keeping track of UTF-16 indices is precarious while using
/// UTF-8 strings as storage, this function converts a byte representation back into
/// codeunits.
pub fn text_state_byte_to_codeunit(mut state: uii::TextInputState) -> uii::TextInputState {
    fn convert(txt: &str, idx: &mut i64) {
        *idx = match byte_to_codeunit(txt, *idx as usize) {
            Some(v) => v as i64,
            None => -1,
        };
    }
    convert(&state.text, &mut state.selection.base);
    convert(&state.text, &mut state.selection.extent);
    convert(&state.text, &mut state.composing.start);
    convert(&state.text, &mut state.composing.end);
    state
}

#[cfg(test)]
mod test {
    use super::*;
    #[test]
    fn test_codeunit_to_byte() {
        let s = "m😸eow";
        assert_eq!(Some(0), codeunit_to_byte(s, 0));
        assert_eq!(Some(8), codeunit_to_byte(s, 6));
        assert_eq!(Some(5), codeunit_to_byte(s, 3));
        assert_eq!(Some(1), codeunit_to_byte(s, 1));
        assert_eq!(None, codeunit_to_byte(s, 2));
    }

    #[test]
    fn test_byte_to_codeunit() {
        let s = "m😸eow";
        assert_eq!(Some(0), byte_to_codeunit(s, 0));
        assert_eq!(Some(6), byte_to_codeunit(s, 8));
        assert_eq!(Some(3), byte_to_codeunit(s, 5));
        assert_eq!(Some(1), byte_to_codeunit(s, 1));
        assert_eq!(None, codeunit_to_byte(s, 2));
    }

    #[test]
    fn test_char_to_byte() {
        let s = "m😸eow";
        assert_eq!(Some(0), char_to_byte(s, 0));
        assert_eq!(Some(8), char_to_byte(s, 5));
        assert_eq!(Some(5), char_to_byte(s, 2));
        assert_eq!(Some(1), char_to_byte(s, 1));
    }

    #[test]
    fn test_byte_to_char() {
        let s = "m😸eow";
        assert_eq!(Some(0), byte_to_char(s, 0));
        assert_eq!(Some(5), byte_to_char(s, 8));
        assert_eq!(Some(2), byte_to_char(s, 5));
        assert_eq!(Some(1), byte_to_char(s, 1));
        assert_eq!(None, byte_to_char(s, 2));
    }
}
