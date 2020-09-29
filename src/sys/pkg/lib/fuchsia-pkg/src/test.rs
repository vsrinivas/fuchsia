// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{CreationManifest, PackagePath},
    proptest::prelude::*,
};

/// Helper to assist asserting a single match branch.
///
/// Ex:
///
/// let arg = Arg::Uint(8);
/// assert_matches!(arg, Arg::Uint(x) => assert_eq!(x, 8));
#[cfg(test)]
macro_rules! assert_matches(
    ($e:expr, $p:pat => $a:expr) => (
        match $e {
            $p => $a,
            v => panic!("Failed to match '{:?}'", v),
        }
    )
);

// TODO(fxbug.dev/22531) allow newline once meta/contents supports it in blob paths
pub(crate) const ANY_UNICODE_EXCEPT_SLASH_NULL_DOT_OR_NEWLINE: &str = "[^/\0\\.\n]";

prop_compose! {
    pub fn always_valid_resource_path_char()(c in ANY_UNICODE_EXCEPT_SLASH_NULL_DOT_OR_NEWLINE) -> String {
        c
    }
}

prop_compose! {
    pub fn always_valid_resource_path_chars
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

prop_compose! {
    pub fn random_resource_path_with_regex_segment_string
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

prop_compose! {
    pub fn random_resource_path_with_regex_segment_str
        (max_segments: usize, inner: &'static str)
        (s in random_resource_path_with_regex_segment_string(
            max_segments, inner.to_string())) -> String
    {
        s
    }
}

prop_compose! {
    pub fn random_external_resource_path()
        (s in random_resource_path(1, 4)
         .prop_filter(
             "External package contents cannot be in the 'meta/' directory",
             |s| !s.starts_with("meta/"))
        ) -> String
    {
        s
    }
}

prop_compose! {
    pub fn random_far_resource_path()
        (s in random_resource_path(1, 4)) -> String
    {
        format!("meta/{}", s)
    }
}

prop_compose! {
    pub fn random_hash()(s: [u8; 32]) -> fuchsia_merkle::Hash {
        s.into()
    }
}

prop_compose! {
    pub fn random_merkle_hex()(s in "[[:xdigit:]]{64}") -> String {
        s
    }
}

prop_compose! {
    pub fn random_package_name()(s in r"[-0-9a-z\.]{1, 100}") -> String {
        s
    }
}

prop_compose! {
    pub fn random_package_variant()(s in r"[-0-9a-z\.]{1, 100}") -> String {
        s
    }
}

prop_compose! {
    pub fn random_creation_manifest()
        (external_content in prop::collection::btree_map(
            random_external_resource_path(), random_resource_path(1, 2), 1..4),
         far_content in prop::collection::btree_map(
             random_far_resource_path(), random_resource_path(1, 2), 1..4),)
         -> CreationManifest
    {
        CreationManifest::from_external_and_far_contents(
            external_content, far_content)
            .unwrap()
    }
}

prop_compose! {
    pub fn random_package_path()(
        name in random_package_name(),
        variant in random_package_variant()
    ) -> PackagePath {
        PackagePath::from_name_and_variant(name, variant).unwrap()
    }
}
