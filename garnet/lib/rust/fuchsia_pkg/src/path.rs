// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::errors::ResourcePathError;

pub const MAX_OBJECT_BYTES: usize = 255;

/// Checks if `input` is a valid path for a file in a Fuchsia package.
/// Fuchsia package resource paths are Fuchsia object relative paths without
/// the limit on maximum path length.
/// Passes the input through if it is valid.
pub fn check_resource_path(input: &str) -> Result<&str, ResourcePathError> {
    if input.is_empty() {
        return Err(ResourcePathError::PathIsEmpty);
    }
    if input.starts_with('/') {
        return Err(ResourcePathError::PathStartsWithSlash);
    }
    if input.ends_with('/') {
        return Err(ResourcePathError::PathEndsWithSlash);
    }
    for segment in input.split('/') {
        if segment.contains('\0') {
            return Err(ResourcePathError::NameContainsNull);
        }
        if segment == "." {
            return Err(ResourcePathError::NameIsDot);
        }
        if segment == ".." {
            return Err(ResourcePathError::NameIsDotDot);
        }
        if segment.is_empty() {
            return Err(ResourcePathError::NameEmpty);
        }
        if segment.len() > MAX_OBJECT_BYTES {
            return Err(ResourcePathError::NameTooLong);
        }
    }
    Ok(input)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::test::*;
    use proptest::prelude::*;
    use proptest::{prop_assert, prop_assert_eq, prop_assume, proptest, proptest_helper};

    // Tests for invalid paths
    #[test]
    fn test_empty_string() {
        assert_eq!(check_resource_path(""), Err(ResourcePathError::PathIsEmpty));
    }

    proptest! {
        #[test]
        fn test_reject_empty_object_name(
            ref s in path_with_regex_segment_str(5, "")) {
            prop_assume!(!s.starts_with('/') && !s.ends_with('/'));
            prop_assert_eq!(check_resource_path(s), Err(ResourcePathError::NameEmpty));
        }

        #[test]
        fn test_reject_long_object_name(
            ref s in path_with_regex_segment_str(5, r"[[[:ascii:]]--\.--/--\x00]{256}")) {
            prop_assert_eq!(check_resource_path(s), Err(ResourcePathError::NameTooLong));
        }

        #[test]
        fn test_reject_contains_null(
            ref s in path_with_regex_segment_string(
                5, format!(r"{}{{0,3}}\x00{}{{0,3}}",
                           ANY_UNICODE_EXCEPT_SLASH_NULL_OR_DOT,
                           ANY_UNICODE_EXCEPT_SLASH_NULL_OR_DOT))) {
            prop_assert_eq!(check_resource_path(s), Err(ResourcePathError::NameContainsNull));
        }

        #[test]
        fn test_reject_name_is_dot(
            ref s in path_with_regex_segment_str(5, r"\.")) {
            prop_assert_eq!(check_resource_path(s), Err(ResourcePathError::NameIsDot));
        }

        #[test]
        fn test_reject_name_is_dot_dot(
            ref s in path_with_regex_segment_str(5, r"\.\.")) {
            prop_assert_eq!(check_resource_path(s), Err(ResourcePathError::NameIsDotDot));
        }

        #[test]
        fn test_reject_starts_with_slash(
            ref s in format!(
                "/{}{{1,5}}",
                ANY_UNICODE_EXCEPT_SLASH_NULL_OR_DOT).as_str()) {
            prop_assert_eq!(check_resource_path(s), Err(ResourcePathError::PathStartsWithSlash));
        }

        #[test]
        fn test_reject_ends_with_slash(
            ref s in format!(
                "{}{{1,5}}/",
                ANY_UNICODE_EXCEPT_SLASH_NULL_OR_DOT).as_str()) {
            prop_assert_eq!(check_resource_path(s), Err(ResourcePathError::PathEndsWithSlash));
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
            prop_assert_eq!(check_resource_path(s), Ok(s.as_str()));
        }

        #[test]
        fn test_name_contains_dot_dot(
            ref s in path_with_regex_segment_string(
                5, format!(r"{}{{1,4}}\.\.{}{{1,4}}",
                           ANY_UNICODE_EXCEPT_SLASH_NULL_OR_DOT,
                           ANY_UNICODE_EXCEPT_SLASH_NULL_OR_DOT)))
        {
            prop_assert_eq!(check_resource_path(s), Ok(s.as_str()));
        }

        #[test]
        fn test_single_segment(ref s in always_valid_chars(1, 4)) {
            prop_assert_eq!(check_resource_path(s), Ok(s.as_str()));
        }

        #[test]
        fn test_multi_segment(
            ref s in prop::collection::vec(always_valid_chars(1, 4), 1..5))
        {
            let path = s.join("/");
            prop_assert_eq!(check_resource_path(&path), Ok(path.as_str()));
        }

        #[test]
        fn test_long_name(
            ref s in path_with_regex_segment_str(
                5, "[[[:ascii:]]--\0--/]{255}"))
        {
            prop_assert_eq!(check_resource_path(s), Ok(s.as_str()));
        }
    }
}
