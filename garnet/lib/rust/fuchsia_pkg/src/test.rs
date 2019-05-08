// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use proptest::prelude::*;
use proptest::{prop_compose, proptest_helper};

/// Helper to assist asserting a single match branch.
///
/// Ex:
///
/// let arg = Arg::Uint(8);
/// assert_matches!(arg, Arg::Uint(x) => assert_eq!(x, 8));
macro_rules! assert_matches(
    ($e:expr, $p:pat => $a:expr) => (
        match $e {
            $p => $a,
            v => panic!("Failed to match '{:?}'", v),
        }
    )
);

pub const ANY_UNICODE_EXCEPT_SLASH_NULL_OR_DOT: &str = r"[^/\x00\.]";

prop_compose! {
    [pub] fn always_valid_char()(c in ANY_UNICODE_EXCEPT_SLASH_NULL_OR_DOT) -> String {
        c
    }
}

prop_compose! {
    [pub] fn always_valid_chars
        (min: usize, max: usize)
        (s in prop::collection::vec(always_valid_char(), min..max)) -> String {
            s.join("")
        }
}

prop_compose! {
    [pub] fn valid_path
        (min: usize, max: usize)
        (s in prop::collection::vec(always_valid_chars(1, 4), min..max))
         -> String
    {
        s.join("/")
    }
}

prop_compose! {
    [pub] fn path_with_regex_segment_string
        (max_segments: usize, inner: String)
        (vec in prop::collection::vec(
            always_valid_chars(1, 3), 3..max_segments),
         inner in prop::string::string_regex(inner.as_str()).unwrap())
        (index in ..vec.len(),
         inner in Just(inner),
         vec in Just(vec))-> String
    {
        let mut vec = vec.clone();
        vec[index] = inner.to_string();
        vec.join("/")
    }
}

prop_compose! {
    [pub] fn path_with_regex_segment_str
        (max_segments: usize, inner: &'static str)
        (s in path_with_regex_segment_string(
            max_segments, inner.to_string())) -> String
    {
        s
    }
}

prop_compose! {
    [pub] fn merkle_hex()(s in "[[:xdigit:]]{64}") -> String {
        s
    }
}
