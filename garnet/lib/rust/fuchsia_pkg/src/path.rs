// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::errors::PackagePathError;

pub const MAX_OBJECT_BYTES: usize = 255;

/// Checks if `input` is a valid path for a file in a Fuchsia package.
/// Fuchsia package paths are Fuchsia object relative paths without the
/// limit on maximum path length.
/// Passes the input through if it is valid.
pub fn check_package_path(input: &str) -> Result<&str, PackagePathError> {
    if input.is_empty() {
        return Err(PackagePathError::PathIsEmpty);
    }
    if input.starts_with('/') {
        return Err(PackagePathError::PathStartsWithSlash);
    }
    if input.ends_with('/') {
        return Err(PackagePathError::PathEndsWithSlash);
    }
    for segment in input.split('/') {
        if segment.contains('\0') {
            return Err(PackagePathError::NameContainsNull);
        }
        if segment == "." {
            return Err(PackagePathError::NameIsDot);
        }
        if segment == ".." {
            return Err(PackagePathError::NameIsDotDot);
        }
        if segment.is_empty() {
            return Err(PackagePathError::NameEmpty);
        }
        if segment.len() > MAX_OBJECT_BYTES {
            return Err(PackagePathError::NameTooLong);
        }
    }
    Ok(input)
}

#[cfg(test)]
mod tests {
    use super::*;
    use proptest::prelude::*;
    use proptest::{
        prop_assert, prop_assert_eq, prop_assume, prop_compose, proptest, proptest_helper,
    };

    const ANY_UNICODE_EXCEPT_SLASH_NULL_OR_DOT: &str = r"[^/\x00\.]";

    // Tests for invalid paths
    #[test]
    fn test_empty_string() {
        assert_eq!(check_package_path(""), Err(PackagePathError::PathIsEmpty));
    }

    prop_compose! {
        fn always_valid_char()(c in ANY_UNICODE_EXCEPT_SLASH_NULL_OR_DOT) -> String {
            c
        }
    }

    prop_compose! {
        fn always_valid_chars
            (min: usize, max: usize)
            (s in prop::collection::vec(always_valid_char(), min..max)) -> String {
            s.join("")
        }
    }

    prop_compose! {
        fn path_with_regex_segment_string
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
        fn path_with_regex_segment_str
            (max_segments: usize, inner: &'static str)
            (s in path_with_regex_segment_string(
                max_segments, inner.to_string())) -> String
        {
            s
        }
    }

    proptest! {
        #[test]
        fn test_reject_empty_object_name(
            ref s in path_with_regex_segment_str(5, "")) {
            prop_assume!(!s.starts_with('/') && !s.ends_with('/'));
            prop_assert_eq!(check_package_path(s), Err(PackagePathError::NameEmpty));
        }

        #[test]
        fn test_reject_long_object_name(
            ref s in path_with_regex_segment_str(5, r"[[[:ascii:]]--\.--/--\x00]{256}")) {
            prop_assert_eq!(check_package_path(s), Err(PackagePathError::NameTooLong));
        }

        #[test]
        fn test_reject_contains_null(
            ref s in path_with_regex_segment_string(
                5, format!(r"{}{{0,3}}\x00{}{{0,3}}",
                           ANY_UNICODE_EXCEPT_SLASH_NULL_OR_DOT,
                           ANY_UNICODE_EXCEPT_SLASH_NULL_OR_DOT))) {
            prop_assert_eq!(check_package_path(s), Err(PackagePathError::NameContainsNull));
        }

        #[test]
        fn test_reject_name_is_dot(
            ref s in path_with_regex_segment_str(5, r"\.")) {
            prop_assert_eq!(check_package_path(s), Err(PackagePathError::NameIsDot));
        }

        #[test]
        fn test_reject_name_is_dot_dot(
            ref s in path_with_regex_segment_str(5, r"\.\.")) {
            prop_assert_eq!(check_package_path(s), Err(PackagePathError::NameIsDotDot));
        }

        #[test]
        fn test_reject_starts_with_slash(
            ref s in format!(
                "/{}{{1,5}}",
                ANY_UNICODE_EXCEPT_SLASH_NULL_OR_DOT).as_str()) {
            prop_assert_eq!(check_package_path(s), Err(PackagePathError::PathStartsWithSlash));
        }

        #[test]
        fn test_reject_ends_with_slash(
            ref s in format!(
                "{}{{1,5}}/",
                ANY_UNICODE_EXCEPT_SLASH_NULL_OR_DOT).as_str()) {
            prop_assert_eq!(check_package_path(s), Err(PackagePathError::PathEndsWithSlash));
        }
    }

    // Tests for valid paths
    proptest! {
        #[test]
        fn test_name_contains_dot(
            ref s in path_with_regex_segment_string(
                5, format!(r"{}{{1,4}}\.{}{{1,4}}",
                           ANY_UNICODE_EXCEPT_SLASH_NULL_OR_DOT,
                           ANY_UNICODE_EXCEPT_SLASH_NULL_OR_DOT)))
        {
            prop_assert_eq!(check_package_path(s), Ok(s.as_str()));
        }

        #[test]
        fn test_name_contains_dot_dot(
            ref s in path_with_regex_segment_string(
                5, format!(r"{}{{1,4}}\.\.{}{{1,4}}",
                           ANY_UNICODE_EXCEPT_SLASH_NULL_OR_DOT,
                           ANY_UNICODE_EXCEPT_SLASH_NULL_OR_DOT)))
        {
            prop_assert_eq!(check_package_path(s), Ok(s.as_str()));
        }

        #[test]
        fn test_single_segment(ref s in always_valid_chars(1, 4)) {
            prop_assert_eq!(check_package_path(s), Ok(s.as_str()));
        }

        #[test]
        fn test_multi_segment(
            ref s in prop::collection::vec(always_valid_chars(1, 4), 1..5))
        {
            let path = s.join("/");
            prop_assert_eq!(check_package_path(&path), Ok(path.as_str()));
        }

        #[test]
        fn test_long_name(
            ref s in path_with_regex_segment_str(
                5, "[[[:ascii:]]--\0--/]{255}"))
        {
            prop_assert_eq!(check_package_path(s), Ok(s.as_str()));
        }
    }
}
