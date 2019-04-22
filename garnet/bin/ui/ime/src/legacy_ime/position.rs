// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use lazy_static::lazy_static;
use regex::Regex;
use unicode_segmentation::{GraphemeCursor, UnicodeSegmentation};

/// Horizontal motion type for the cursor.
pub enum HorizontalMotion {
    GraphemeLeft(GraphemeTraversal),
    GraphemeRight,
    WordLeft,
    WordRight,
}

/// How the cursor should traverse grapheme clusters.
pub enum GraphemeTraversal {
    /// Move by whole grapheme clusters at a time.
    ///
    /// This traversal mode should be used when using arrow keys, or when deleting forward (with the
    /// <kbd>Delete</kbd> key).
    WholeGrapheme,
    /// Generally move by whole grapheme clusters, but allow moving through individual combining
    /// characters, if present at the end of the grapheme cluster.
    ///
    /// This traversal mode should be used when deleting backward (<kbd>Backspace</kbd>), but not
    /// when deleting forward or using arrow keys.
    ///
    /// This ensures that when a user is typing text and composes a character out of individual
    /// combining diacritics, it should be possible to correct a mistake by pressing
    /// <kbd>Backspace</kbd>. If we were to allow _moving the cursor_ left and right through
    /// diacritics, that would only cause user confusion, as the blinking caret would not move
    /// visibly while within a single grapheme cluster.
    CombiningCharacters,
}

/// Calculates an adjacent cursor position to left or right of the current position.
///
/// * `start`: Starting position in the string, as a byte offset.
/// * `motion`: Whether to go right or left, and whether to allow entering grapheme clusters.
pub fn adjacent_cursor_position(text: &str, start: usize, motion: HorizontalMotion) -> usize {
    match motion {
        HorizontalMotion::GraphemeRight => adjacent_cursor_position_grapheme_right(text, start),
        HorizontalMotion::GraphemeLeft(traversal) => {
            adjacent_cursor_position_grapheme_left(text, start, traversal)
        }
        HorizontalMotion::WordLeft => adjacent_cursor_position_word_left(text, start),
        HorizontalMotion::WordRight => adjacent_cursor_position_word_right(text, start),
    }
}

fn get_grapheme_boundary(text: &str, start: usize, next: bool) -> Option<usize> {
    let text_length = text.len();
    let mut cursor = GraphemeCursor::new(start, text_length, true);
    let result = if next { cursor.next_boundary(&text, 0) } else { cursor.prev_boundary(&text, 0) };
    result.unwrap_or(None)
}

fn adjacent_cursor_position_grapheme_right(text: &str, start: usize) -> usize {
    get_grapheme_boundary(text, start, true).unwrap_or(text.len())
}

fn adjacent_cursor_position_grapheme_left(
    text: &str,
    start: usize,
    traversal: GraphemeTraversal,
) -> usize {
    let prev_boundary = get_grapheme_boundary(text, start, false);
    if let Some(offset) = prev_boundary {
        if let GraphemeTraversal::CombiningCharacters = traversal {
            let grapheme_str = &text[offset..start];
            let last_char_str = match grapheme_str.char_indices().last() {
                Some((last_char_offset, _c)) => Some(&grapheme_str[last_char_offset..]),
                None => None,
            };
            if let Some(last_char_str) = last_char_str {
                lazy_static! {
                    /// A regex that matches combining characters, e.g. accents and other
                    /// diacritics. Rust does not provide a way to check the Unicode categories
                    /// of `char`s directly, so this is the simplest workaround for now.
                    static ref COMBINING_REGEX: Regex = Regex::new(r"\p{M}$").unwrap();
                }
                if COMBINING_REGEX.is_match(last_char_str) {
                    return start - last_char_str.len();
                }
            }
        }
        offset
    } else {
        // Can't go left from the beginning of the string.
        0
    }
}

fn adjacent_cursor_position_word_left(full_text: &str, start: usize) -> usize {
    if start == 0 {
        return 0;
    }
    let text = &full_text[0..start];
    // Find the next word to the left.
    let word = match UnicodeSegmentation::unicode_words(text).rev().next() {
        Some(word) => word,
        // No words - go to the string start.
        None => return 0,
    };
    // Find start of the next word.
    if let Some((pos, _)) = UnicodeSegmentation::split_word_bound_indices(text)
        .rev()
        .find(|(_, next_word)| next_word == &word)
    {
        pos
    } else {
        0
    }
}

fn adjacent_cursor_position_word_right(full_text: &str, start: usize) -> usize {
    let text = &full_text[start..];
    let text_length = text.len();
    if text_length == 0 {
        return start;
    }
    let mut words_iter = UnicodeSegmentation::unicode_words(text);
    // Find the next word to the right.
    let word = match words_iter.next() {
        Some(word) => word,
        // No words - go the end of the string.
        None => return start + text_length,
    };
    let mut word_bound_indices = UnicodeSegmentation::split_word_bound_indices(text);
    // Skip over boundaries until a next word is found.
    let word_bound = word_bound_indices.find(|(_, next_word)| next_word == &word);

    // Return start of the next boundary after the word, if there is one.
    if let Some((next_boundary_pos, _)) = word_bound_indices.next() {
        start + next_boundary_pos
    } else if let Some((pos, next_word)) = word_bound {
        // Last word - go to end of the word.
        start + pos + next_word.len()
    } else {
        // No more words - go to the end of the line.
        start + text_length
    }
}
