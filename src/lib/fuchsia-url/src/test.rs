// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        parse::{PackageName, PackageVariant},
        RelativePackageUrl,
    },
    proptest::prelude::*,
};

/// Valid characters for a Fuchsia package path.  These characters are any unicode character,
/// except for '/', '\0', '.', and '\n'.
// TODO(fxbug.dev/22531) allow newline once meta/contents supports it in blob paths
pub(crate) const ANY_UNICODE_EXCEPT_SLASH_NULL_DOT_OR_NEWLINE: &str = "[^/\0\\.\n]";

prop_compose! {
    pub(crate) fn random_package_segment()
        (s in r"[-0-9a-z\._]{1, 255}"
            .prop_filter(
                "Segments of '.' and '..' are not allowed",
                |s| s != "." && s != ".."
            )
        )
    -> String {
        s
    }
}

prop_compose! {
    pub fn random_package_name()(s in random_package_segment()) -> PackageName {
        s.parse().unwrap()
    }
}

prop_compose! {
    pub fn random_package_variant()(s in random_package_segment()) -> PackageVariant {
        s.parse().unwrap()
    }
}

prop_compose! {
    fn always_valid_resource_path_char()(c in ANY_UNICODE_EXCEPT_SLASH_NULL_DOT_OR_NEWLINE) -> String {
        c
    }
}

prop_compose! {
    pub fn random_relative_package_url()(s in random_package_segment()) -> RelativePackageUrl {
        s.parse().unwrap()
    }
}

prop_compose! {
    pub(crate) fn always_valid_resource_path_chars
        (min: usize, max: usize)
        (s in prop::collection::vec(always_valid_resource_path_char(), min..max)) -> String {
            s.join("")
        }
}

prop_compose! {
    pub fn random_resource_path
        (min: usize, max: usize)
        (s in prop::collection::vec(always_valid_resource_path_chars(1, 4), min..max))
         -> String
    {
        s.join("/")
    }
}

#[cfg(test)]
prop_compose! {
    pub(crate) fn random_resource_path_with_regex_segment_string
        (max_segments: usize, inner: String)
        (vec in prop::collection::vec(
            always_valid_resource_path_chars(1, 3), 3..max_segments),
         inner in prop::string::string_regex(inner.as_str()).unwrap())
        (index in ..vec.len(),
         inner in Just(inner),
         vec in Just(vec))-> String
    {
        let mut vec = vec;
        vec[index] = inner;
        vec.join("/")
    }
}

#[cfg(test)]
prop_compose! {
    pub(crate) fn random_resource_path_with_regex_segment_str
        (max_segments: usize, inner: &'static str)
        (s in random_resource_path_with_regex_segment_string(
            max_segments, inner.to_string())) -> String
    {
        s
    }
}
