// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO use str::strip_{prefix,suffix} when the str_strip feature stabilizes.
pub(crate) fn strip_prefix<'a>(s: &'a str, prefix: &str) -> Option<&'a str> {
    if s.starts_with(prefix) {
        let (_, s) = s.split_at(prefix.len());
        Some(s)
    } else {
        None
    }
}

pub(crate) fn strip_suffix<'a>(s: &'a str, suffix: &str) -> Option<&'a str> {
    if s.ends_with(suffix) {
        let (s, _) = s.split_at(s.len() - suffix.len());
        Some(s)
    } else {
        None
    }
}

#[cfg(test)]
mod tests {
    use {super::*, proptest::prelude::*};

    #[test]
    fn strip_suffix_works() {
        assert_eq!(strip_suffix("ab", "b"), Some("a"));
        assert_eq!(strip_suffix("ab", "ab"), Some(""));
        assert_eq!(strip_suffix("abb", "b"), Some("ab"));
        assert_eq!(strip_suffix("ab", "a"), None);
    }

    #[test]
    fn strip_prefix_works() {
        assert_eq!(strip_prefix("ab", "a"), Some("b"));
        assert_eq!(strip_prefix("ab", "ab"), Some(""));
        assert_eq!(strip_prefix("aab", "a"), Some("ab"));
        assert_eq!(strip_prefix("ab", "b"), None);
    }

    proptest! {
        #[test]
        fn strip_suffix_returns_some_if_ends_with_suffix(base: String, needle: String) {
            let haystack = base.clone() + &needle;
            assert_eq!(strip_suffix(&haystack, &needle), Some(base.as_str()))
        }

        #[test]
        fn strip_prefix_returns_some_if_starts_with_prefix(base: String, needle: String) {
            let haystack = needle.clone() + &base;
            assert_eq!(strip_prefix(&haystack, &needle), Some(base.as_str()))
        }

        #[test]
        fn reversed_strip_prefix_is_strip_suffix_of_reversed_inputs(
            haystack: String,
            needle: String,
        ) {
            fn rev(s: &str) -> String {
                s.chars().rev().collect()
            }
            let no_prefix = strip_prefix(&haystack, &needle).map(rev);
            let no_suffix = strip_suffix(&rev(&haystack), &rev(&needle)).map(ToOwned::to_owned);

            prop_assert_eq!(no_prefix, no_suffix);
        }
    }
}
